/*------------------------------------------------------------------------------
 * JIT Compiler Demo for LoongArch32R
 *
 * 这个demo展示JIT编译的核心原理：
 * 1. 定义一个简单的虚拟机（3条指令）
 * 2. 实现解释器版本
 * 3. 实现JIT编译器版本
 * 4. 对比两者的执行方式
 *----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <timer.h>

/*============================================================================
 * 简单虚拟机定义
 *
 * 虚拟机有：
 * - 1个寄存器 A（累加器）
 * - 3条指令：
 *   0x01 nn    LOAD n   : A = n
 *   0x02 nn    ADD n    : A = A + n
 *   0x03 aa    JMP addr : PC = addr
 *   0x00       HALT     : 停止执行，返回A的值
 *============================================================================*/

#define OP_HALT  0x00
#define OP_LOAD  0x01
#define OP_ADD   0x02
#define OP_JMP   0x03

/*============================================================================
 * 方法1：解释器（Interpreter）
 *
 * 每次执行都要：取指令 -> switch判断 -> 执行 -> 循环
 *============================================================================*/
int vm_interpret(uint8_t *code, int max_cycles) {
    int pc = 0;
    int reg_a = 0;
    int cycles = 0;

    while (cycles < max_cycles) {
        uint8_t op = code[pc];

        switch (op) {
        case OP_HALT:
            return reg_a;

        case OP_LOAD:
            reg_a = code[pc + 1];
            pc += 2;
            break;

        case OP_ADD:
            reg_a += code[pc + 1];
            pc += 2;
            break;

        case OP_JMP:
            pc = code[pc + 1];
            break;

        default:
            printf("Unknown opcode: 0x%02x at pc=%d\n", op, pc);
            return -1;
        }

        cycles++;
    }

    printf("Max cycles reached!\n");
    return reg_a;
}

/*============================================================================
 * 方法2：JIT编译器
 *
 * 核心思想：把虚拟机代码翻译成LoongArch32R本机代码
 *
 * LoongArch32R 指令编码（32位）：
 * - addi.w rd, rj, imm12  : 0x00800000 | (imm12 << 10) | (rj << 5) | rd
 * - b offset              : 0x50000000 | (offset >> 2)
 * - jirl rd, rj, offset   : 0x4C000000 | (offset << 10) | (rj << 5) | rd
 *
 * 寄存器分配：
 * - $t0 ($r12) : 虚拟机的寄存器A
 * - $ra ($r1)  : 返回地址
 *============================================================================*/

// JIT代码缓冲区（存放生成的本机代码）
static uint32_t jit_code_buffer[256];
static int jit_code_size = 0;

// 生成一条LoongArch32R指令
static void emit(uint32_t instruction) {
    jit_code_buffer[jit_code_size++] = instruction;
}

// LoongArch32R 指令编码函数
// ori rd, rj, imm12 : rd = rj | imm12
// 编码: 0x03800000 | (imm12 << 10) | (rj << 5) | rd
static uint32_t encode_ori(int rd, int rj, int imm12) {
    return 0x03800000 | ((imm12 & 0xFFF) << 10) | (rj << 5) | rd;
}

// addi.w rd, rj, imm12 : rd = rj + imm12
// 编码: 0x02800000 | (imm12 << 10) | (rj << 5) | rd
static uint32_t encode_addi_w(int rd, int rj, int imm12) {
    return 0x02800000 | ((imm12 & 0xFFF) << 10) | (rj << 5) | rd;
}

// jirl rd, rj, offset : rd = pc + 4; pc = rj + offset
// 编码: 0x4C000000 | (offset >> 2 << 10) | (rj << 5) | rd
static uint32_t encode_jirl(int rd, int rj, int offset) {
    return 0x4C000000 | (((offset >> 2) & 0xFFFF) << 10) | (rj << 5) | rd;
}

// b offset : pc = pc + offset
// 编码: 0x50000000 | (offset >> 2)
static uint32_t encode_b(int offset) {
    int off = offset >> 2;
    // b指令的offset分为两部分: [25:16]和[15:0]
    return 0x50000000 | ((off & 0xFFFF) | ((off >> 16) << 16));
}

// 寄存器编号
#define REG_ZERO  0   // $r0 = 0
#define REG_RA    1   // $r1 = return address
#define REG_T0    12  // $r12 = $t0 (我们用来存虚拟机的A寄存器)
#define REG_A0    4   // $r4 = $a0 (函数返回值)

/*
 * JIT编译：把虚拟机代码翻译成本机代码
 *
 * 输入：虚拟机字节码
 * 输出：可执行的本机代码函数指针
 */

// jit_func_t : int (*)(void)
typedef int (*jit_func_t)(void);

jit_func_t jit_compile(uint8_t *code, int code_len) {
    jit_code_size = 0;

    // 记录每个虚拟机地址对应的 loongarch 代码位置
    int addr_map[256];
    memset(addr_map, -1, sizeof(addr_map));

    // 需要回填的跳转指令
    struct {
        int native_pos;   // 本机代码位置
        int target_addr;  // 目标虚拟机地址
    } fixups[64];
    int num_fixups = 0;

    // 第一遍：生成代码
    int pc = 0;
    while (pc < code_len) {

        // 记录 6502 指令和 loongarch 指令缓存的对应关系
        // addr[0] = 0;
        // addr[2] = 1;
        // addr[4] = 2;
        addr_map[pc] = jit_code_size;

        uint8_t op = code[pc];

        switch (op) {
        case OP_HALT:
            // 返回：把$t0的值复制到$a0，然后返回
            // move $a0, $t0  =>  or $a0, $t0, $zero
            emit(encode_ori(REG_A0, REG_T0, 0));
            // jirl $zero, $ra, 0  (返回)
            emit(encode_jirl(REG_ZERO, REG_RA, 0));
            pc += 1;
            break;

        case OP_LOAD:
            // LOAD n: A = n
            // ori $t0, $zero, n
            emit(encode_ori(REG_T0, REG_ZERO, code[pc + 1]));
            pc += 2;
            break;

        case OP_ADD:
            // ADD n: A = A + n
            // addi.w $t0, $t0, n
            emit(encode_addi_w(REG_T0, REG_T0, code[pc + 1]));
            pc += 2;
            break;

        case OP_JMP:
            // JMP addr: 跳转到指定地址

            // native_pos 指明本条指令在 loongarch 代码中的位置
            fixups[num_fixups].native_pos = jit_code_size;
            // target_addr 指明跳转目标的虚拟机地址，pc+1 中存放的是跳转指令的操作数
            fixups[num_fixups].target_addr = code[pc + 1];
            num_fixups++;
            emit(0);  // 占位，稍后回填
            pc += 2;
            break;

        default:
            printf("JIT: Unknown opcode 0x%02x at pc=%d\n", op, pc);
            return NULL;
        }
    }

    // 第二遍：回填跳转地址
    for (int i = 0; i < num_fixups; i++) {
        int native_pos = fixups[i].native_pos;
        int target_addr = fixups[i].target_addr;
        // 反向查询到 loongarch 的位置
        int target_native = addr_map[target_addr];

        if (target_native < 0) {
            printf("JIT: Invalid jump target %d\n", target_addr);
            return NULL;
        }

        // 计算偏移量（字节）
        // 这是 loongarch b 指令的偏移量，单位是字节
        int offset = (target_native - native_pos) * 4;
        jit_code_buffer[native_pos] = encode_b(offset);
    }

    // 返回函数指针
    // 把数组地址强制转换为函数指针，太秒了
    return (jit_func_t)jit_code_buffer;
}

/*============================================================================
 * 打印生成的本机代码（用于调试）
 *============================================================================*/
void jit_dump(void) {
    printf("\n=== JIT Generated Code ===\n");
    printf("Address      Hex          Instruction\n");
    printf("------------ ------------ -----------\n");

    for (int i = 0; i < jit_code_size; i++) {
        uint32_t inst = jit_code_buffer[i];
        printf("0x%08x:  0x%08x   ", (uint32_t)&jit_code_buffer[i], inst);

        // 简单的反汇编
        if ((inst & 0xFFC00000) == 0x03800000) {
            // ori
            int rd = inst & 0x1F;
            int rj = (inst >> 5) & 0x1F;
            int imm = (inst >> 10) & 0xFFF;
            printf("ori $r%d, $r%d, %d", rd, rj, imm);
        } else if ((inst & 0xFFC00000) == 0x02800000) {
            // addi.w
            int rd = inst & 0x1F;
            int rj = (inst >> 5) & 0x1F;
            int imm = (inst >> 10) & 0xFFF;
            printf("addi.w $r%d, $r%d, %d", rd, rj, imm);
        } else if ((inst & 0xFC000000) == 0x4C000000) {
            // jirl
            int rd = inst & 0x1F;
            int rj = (inst >> 5) & 0x1F;
            printf("jirl $r%d, $r%d, ...", rd, rj);
        } else if ((inst & 0xFC000000) == 0x50000000) {
            // b
            printf("b ...");
        } else {
            printf("???");
        }
        printf("\n");
    }
    printf("==========================\n\n");
}

/*============================================================================
 * Demo: 对比解释器和JIT
 *============================================================================*/
void jit_demo(void) {
    printf("\n");
    printf("==========================================\n");
    printf("  JIT Compiler Demo for LoongArch32R\n");
    printf("==========================================\n\n");

    // 测试程序：简单计算 1 + 2 + 3 = 6
    uint8_t prog1[] = {
        OP_LOAD, 1,     // A = 1
        OP_ADD,  2,     // A = A + 2 = 3
        OP_ADD,  3,     // A = A + 3 = 6
        OP_HALT         // return A
    };

    printf("Program: LOAD 1; ADD 2; ADD 3; HALT\n");
    printf("Expected result: 6\n\n");

    // 先验证正确性
    int interp_result = vm_interpret(prog1, 100);
    jit_func_t fn = jit_compile(prog1, sizeof(prog1));
    int jit_result = fn ? fn() : -1;

    printf("Interpreter result: %d\n", interp_result);
    printf("JIT result: %d\n\n", jit_result);

    // 性能测试
    #define TEST_ITERATIONS 10000000

    printf("------------------------------------------\n");
    printf("Performance Test: %d iterations\n", TEST_ITERATIONS);
    printf("------------------------------------------\n\n");

    // 测试解释器
    uint32_t start_interp = timer_get_uptime_us();
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        vm_interpret(prog1, 100);
    }
    uint32_t end_interp = timer_get_uptime_us();
    uint32_t time_interp = end_interp - start_interp;

    // 测试 JIT
    uint32_t start_jit = timer_get_uptime_us();
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        fn();
    }
    uint32_t end_jit = timer_get_uptime_us();
    uint32_t time_jit = end_jit - start_jit;

    // 输出结果
    printf("[Interpreter]\n");
    printf("  Time: %u us\n\n", time_interp);

    printf("[JIT]\n");
    printf("  Time: %u us\n\n", time_jit);

    // 计算加速比
    if (time_jit > 0) {
        uint32_t speedup = time_interp / time_jit;
        printf("Speedup: JIT is ~%ux faster\n\n", speedup);
    }

    printf("==========================================\n");
    printf("  Demo Complete!\n");
    printf("==========================================\n");
}

/*============================================================================
 * Shell命令入口
 *============================================================================*/
int cmd_jit_demo(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    jit_demo();
    return 0;
}
