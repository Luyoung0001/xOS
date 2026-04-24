/*
 * jit_simple.c - 单指令直译 JIT（正确性优先、轻量级缓存）
 *
 * 设计：
 *  - 每条 6502 指令翻译成一小段 LoongArch 代码（不跨块、不链跳）。
 *  - 所有访存统一走 memory_readb/memory_writeb，确保副作用一致。
 *  - PC 自增写回 cpu.PC，cycles 返回指令基础周期（不做额外减法）。
 *  - 直映缓存 pc&mask -> {pc,len,fn}。写 PRG 区或代码区时清空缓存。
 *  - 目标：正确性>性能，先跑通画面与 jittest；后续可优化。
 */

#include "la_emit.h"
#include "litenes/cpu-internal.h"
#include "litenes/cpu-internal.h" // for cpu_stack_pushb/popb
#include "litenes/cpu.h"          // for cpu_step_interp
#include "litenes/jit.h"
#include "litenes/memory.h"
#include <string.h>

// 配置：每指令最大机器码槽（以 32bit 指令计）
#define JIT_CODE_PER_ENTRY 64 // 留足栈保存/复杂寻址

typedef int (*jit_func_t)(void); // 返回 cycles，内部已更新 cpu.PC

// 直接用 PC 索引的平铺表（64K 条目）
static jit_func_t pc_func[1 << 16];
static uint8_t pc_len[1 << 16];
// 为每个 PC 提供固定代码槽（JIT 生成的机器码）
static uint32_t code_pool[1 << 16][JIT_CODE_PER_ENTRY];

// 统计
bool jit_enabled = false;
bool difftest_enabled = false;
int difftest_pass_count = 0;
int difftest_fail_count = 0;
static int stat_hits = 0, stat_misses = 0, stat_invalid = 0;
static int stat_calls = 0;

// 当前正在执行的 PC，供 C 级 handler 使用
static uint16_t jit_cur_pc;

static inline void flush_icache(void *start, void *end) {
    __builtin___clear_cache((char *)start, (char *)end);
}

// 读取/写回 cpu 结构体字段偏移
#define OFF_A 3
#define OFF_X 4
#define OFF_Y 5
#define OFF_P 6
#define OFF_SP 2
#define OFF_PC 0

// 寄存器约定：
//   a0(r4) 返回值 / 参数
//   s0(r23) 保存 cpu*（callee-saved，进入模板装载）
//   其他临时用 caller-saved
#define REG_CPU 23

// 通用序言/尾声：保存 s0、装载 cpu*、退出恢复
static inline int emit_prologue(uint32_t *buf, int p) {
    // addi.w sp, sp, -24
    buf[p++] = la_addi_w(3, 3, -24);
    // st.w ra(r1), sp, 0
    buf[p++] = la_st_w(1, 3, 0);
    // st.w s0, sp, 4
    buf[p++] = la_st_w(REG_CPU, 3, 4);
    // s0 = &cpu
    p = emit_li32(buf, p, REG_CPU, (uint32_t)(uintptr_t)&cpu);
    return p;
}

static inline int emit_epilogue(uint32_t *buf, int p) {
    // ld.w ra, sp, 0
    buf[p++] = la_ld_w(1, 3, 0);
    // ld.w s0, sp, 4
    buf[p++] = la_ld_w(REG_CPU, 3, 4);
    // addi.w sp, sp, 24
    buf[p++] = la_addi_w(3, 3, 24);
    // ret
    buf[p++] = la_jirl(0, 1, 0);
    return p;
}

// LoongArch 模板构建辅助：推进 PC
static inline int emit_pc_advance(uint32_t *buf, int p, uint8_t len) {
    buf[p++] = la_ld_bu(15, REG_CPU, OFF_PC);     // lo
    buf[p++] = la_ld_bu(16, REG_CPU, OFF_PC + 1); // hi
    buf[p++] = la_addi_w(15, 15, len);
    buf[p++] = la_andi(17, 15, 0x100);
    buf[p++] = la_srl_w(17, 17, 8);
    buf[p++] = la_add_w(16, 16, 17);
    buf[p++] = la_st_b(15, REG_CPU, OFF_PC);
    buf[p++] = la_st_b(16, REG_CPU, OFF_PC + 1);
    return p;
}

// Z/N 旗标更新：输入值在 a0(r4)
static inline int emit_set_zn(uint32_t *buf, int p) {
    buf[p++] = la_ld_bu(14, REG_CPU, OFF_P);
    buf[p++] = la_andi(14, 14, 0x7D); // 清 N/Z
    buf[p++] = la_andi(15, 4, 0x80);  // N = val & 0x80
    buf[p++] = la_or(14, 14, 15);
    buf[p++] = la_sltui(15, 4, 1);  // val==0 ?1:0
    buf[p++] = la_sll_w(15, 15, 1); // -> bit1
    buf[p++] = la_or(14, 14, 15);
    buf[p++] = la_st_b(14, REG_CPU, OFF_P);
    return p;
}

// 结束并返回周期数
static inline int emit_ret_cycles(uint32_t *buf, int p, int cycles) {
    buf[p++] = la_ori(4, 0, cycles);
    return p;
}

// ---------------- C 级 handler（加载 / 存储） ----------------

#define SET_ZN(val_expr)                                                       \
    do {                                                                       \
        uint8_t __v = (uint8_t)(val_expr);                                     \
        cpu.P &= 0x7D;                                                         \
        if (__v == 0)                                                          \
            cpu.P |= 0x02;                                                     \
        cpu.P |= (__v & 0x80);                                                 \
    } while (0)

// 取操作数（共用）辅助
static inline uint16_t fetch_abs(uint16_t pc) {
    return memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
}
static inline uint16_t fetch_zp(uint16_t pc) { return memory_readb(pc + 1); }
static inline uint16_t fetch_zp_idx(uint16_t pc, uint8_t idx) {
    return (uint8_t)(memory_readb(pc + 1) + idx);
}
static inline uint16_t fetch_ind_x(uint16_t pc) {
    uint8_t t = (uint8_t)(memory_readb(pc + 1) + cpu.X);
    uint16_t lo = memory_readb(t);
    uint16_t hi = memory_readb((uint8_t)(t + 1));
    return lo | (hi << 8);
}
static inline uint16_t fetch_ind_y(uint16_t pc) {
    uint8_t t = memory_readb(pc + 1);
    uint16_t lo = memory_readb(t);
    uint16_t hi = memory_readb((uint8_t)(t + 1));
    return (lo | (hi << 8)) + cpu.Y;
}
static inline int8_t fetch_rel(uint16_t pc) {
    return (int8_t)memory_readb(pc + 1);
}

// 标志辅助
static inline void set_carry(bool v) {
    if (v)
        cpu.P |= 0x01;
    else
        cpu.P &= ~0x01;
}
static inline void set_overflow(bool v) {
    if (v)
        cpu.P |= 0x40;
    else
        cpu.P &= ~0x40;
}

// ---------------- 算术/逻辑 ----------------
static int h_adc_imm(void) {
    uint8_t v = memory_readb(jit_cur_pc + 1);
    int sum = cpu.A + v + ((cpu.P & 1) ? 1 : 0);
    set_carry(sum > 0xFF);
    set_overflow((~(cpu.A ^ v) & (cpu.A ^ sum) & 0x80));
    cpu.A = sum & 0xFF;
    SET_ZN(cpu.A);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_adc_mem(uint16_t a, int base_cycles) {
    uint8_t v = memory_readb(a);
    int sum = cpu.A + v + ((cpu.P & 1) ? 1 : 0);
    set_carry(sum > 0xFF);
    set_overflow((~(cpu.A ^ v) & (cpu.A ^ sum) & 0x80));
    cpu.A = sum & 0xFF;
    SET_ZN(cpu.A);
    return base_cycles;
}
#define ADC_ZP                                                                 \
    {                                                                          \
        uint16_t a = fetch_zp(jit_cur_pc);                                     \
        int c = h_adc_mem(a, 3);                                               \
        cpu.PC = jit_cur_pc + 2;                                               \
        return c;                                                              \
    }
#define ADC_ZPX                                                                \
    {                                                                          \
        uint16_t a = fetch_zp_idx(jit_cur_pc, cpu.X);                          \
        int c = h_adc_mem(a, 4);                                               \
        cpu.PC = jit_cur_pc + 2;                                               \
        return c;                                                              \
    }
#define ADC_ABS                                                                \
    {                                                                          \
        uint16_t a = fetch_abs(jit_cur_pc);                                    \
        int c = h_adc_mem(a, 4);                                               \
        cpu.PC = jit_cur_pc + 3;                                               \
        return c;                                                              \
    }
#define ADC_ABSX                                                               \
    {                                                                          \
        uint16_t a = fetch_abs(jit_cur_pc) + cpu.X;                            \
        int c = h_adc_mem(a, 4);                                               \
        cpu.PC = jit_cur_pc + 3;                                               \
        return c;                                                              \
    }
#define ADC_ABSY                                                               \
    {                                                                          \
        uint16_t a = fetch_abs(jit_cur_pc) + cpu.Y;                            \
        int c = h_adc_mem(a, 4);                                               \
        cpu.PC = jit_cur_pc + 3;                                               \
        return c;                                                              \
    }
#define ADC_INDX                                                               \
    {                                                                          \
        uint16_t a = fetch_ind_x(jit_cur_pc);                                  \
        int c = h_adc_mem(a, 6);                                               \
        cpu.PC = jit_cur_pc + 2;                                               \
        return c;                                                              \
    }
#define ADC_INDY                                                               \
    {                                                                          \
        uint16_t a = fetch_ind_y(jit_cur_pc);                                  \
        int c = h_adc_mem(a, 5);                                               \
        cpu.PC = jit_cur_pc + 2;                                               \
        return c;                                                              \
    }

static int h_adc_zp() { ADC_ZP }
static int h_adc_zpx() { ADC_ZPX }
static int h_adc_abs() { ADC_ABS }
static int h_adc_absx() { ADC_ABSX }
static int h_adc_absy() { ADC_ABSY }
static int h_adc_indx() { ADC_INDX }
static int h_adc_indy() { ADC_INDY }

// SBC
static int h_sbc_imm(void) {
    uint8_t v = memory_readb(jit_cur_pc + 1);
    int diff = cpu.A - v - ((cpu.P & 1) ? 0 : 1);
    set_carry(!(diff & 0x100));
    set_overflow(((cpu.A ^ v) & (cpu.A ^ diff) & 0x80));
    cpu.A = diff & 0xFF;
    SET_ZN(cpu.A);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_sbc_mem(uint16_t a, int base_cycles) {
    uint8_t v = memory_readb(a);
    int diff = cpu.A - v - ((cpu.P & 1) ? 0 : 1);
    set_carry(!(diff & 0x100));
    set_overflow(((cpu.A ^ v) & (cpu.A ^ diff) & 0x80));
    cpu.A = diff & 0xFF;
    SET_ZN(cpu.A);
    return base_cycles;
}
#define SBC_ZP                                                                 \
    {                                                                          \
        uint16_t a = fetch_zp(jit_cur_pc);                                     \
        int c = h_sbc_mem(a, 3);                                               \
        cpu.PC = jit_cur_pc + 2;                                               \
        return c;                                                              \
    }
#define SBC_ZPX                                                                \
    {                                                                          \
        uint16_t a = fetch_zp_idx(jit_cur_pc, cpu.X);                          \
        int c = h_sbc_mem(a, 4);                                               \
        cpu.PC = jit_cur_pc + 2;                                               \
        return c;                                                              \
    }
#define SBC_ABS                                                                \
    {                                                                          \
        uint16_t a = fetch_abs(jit_cur_pc);                                    \
        int c = h_sbc_mem(a, 4);                                               \
        cpu.PC = jit_cur_pc + 3;                                               \
        return c;                                                              \
    }
#define SBC_ABSX                                                               \
    {                                                                          \
        uint16_t a = fetch_abs(jit_cur_pc) + cpu.X;                            \
        int c = h_sbc_mem(a, 4);                                               \
        cpu.PC = jit_cur_pc + 3;                                               \
        return c;                                                              \
    }
#define SBC_ABSY                                                               \
    {                                                                          \
        uint16_t a = fetch_abs(jit_cur_pc) + cpu.Y;                            \
        int c = h_sbc_mem(a, 4);                                               \
        cpu.PC = jit_cur_pc + 3;                                               \
        return c;                                                              \
    }
#define SBC_INDX                                                               \
    {                                                                          \
        uint16_t a = fetch_ind_x(jit_cur_pc);                                  \
        int c = h_sbc_mem(a, 6);                                               \
        cpu.PC = jit_cur_pc + 2;                                               \
        return c;                                                              \
    }
#define SBC_INDY                                                               \
    {                                                                          \
        uint16_t a = fetch_ind_y(jit_cur_pc);                                  \
        int c = h_sbc_mem(a, 5);                                               \
        cpu.PC = jit_cur_pc + 2;                                               \
        return c;                                                              \
    }
static int h_sbc_zp() { SBC_ZP }
static int h_sbc_zpx() { SBC_ZPX }
static int h_sbc_abs() { SBC_ABS }
static int h_sbc_absx() { SBC_ABSX }
static int h_sbc_absy() { SBC_ABSY }
static int h_sbc_indx() { SBC_INDX }
static int h_sbc_indy() { SBC_INDY }

// AND/ORA/EOR
static int h_and_imm(void) {
    cpu.A &= memory_readb(jit_cur_pc + 1);
    SET_ZN(cpu.A);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_ora_imm(void) {
    cpu.A |= memory_readb(jit_cur_pc + 1);
    SET_ZN(cpu.A);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_eor_imm(void) {
    cpu.A ^= memory_readb(jit_cur_pc + 1);
    SET_ZN(cpu.A);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
#define LOGIC_MEM(opname, opfn, addr, cyc)                                     \
    {                                                                          \
        uint8_t v = memory_readb(addr);                                        \
        cpu.A = opfn;                                                          \
        SET_ZN(cpu.A);                                                         \
        cpu.PC = jit_cur_pc + (cyc <= 3 ? 2 : 3);                              \
        return cyc;                                                            \
    }
static int h_and_zp() { LOGIC_MEM(and, cpu.A & v, fetch_zp(jit_cur_pc), 3); }
static int h_and_zpx() {
    LOGIC_MEM(and, cpu.A & v, fetch_zp_idx(jit_cur_pc, cpu.X), 4);
}
static int h_and_abs() { LOGIC_MEM(and, cpu.A & v, fetch_abs(jit_cur_pc), 4); }
static int h_and_absx() {
    LOGIC_MEM(and, cpu.A & v, fetch_abs(jit_cur_pc) + cpu.X, 4);
}
static int h_and_absy() {
    LOGIC_MEM(and, cpu.A & v, fetch_abs(jit_cur_pc) + cpu.Y, 4);
}
static int h_and_indx() {
    LOGIC_MEM(and, cpu.A & v, fetch_ind_x(jit_cur_pc), 6);
}
static int h_and_indy() {
    LOGIC_MEM(and, cpu.A & v, fetch_ind_y(jit_cur_pc), 5);
}

static int h_ora_zp() { LOGIC_MEM(ora, cpu.A | v, fetch_zp(jit_cur_pc), 3); }
static int h_ora_zpx() {
    LOGIC_MEM(ora, cpu.A | v, fetch_zp_idx(jit_cur_pc, cpu.X), 4);
}
static int h_ora_abs() { LOGIC_MEM(ora, cpu.A | v, fetch_abs(jit_cur_pc), 4); }
static int h_ora_absx() {
    LOGIC_MEM(ora, cpu.A | v, fetch_abs(jit_cur_pc) + cpu.X, 4);
}
static int h_ora_absy() {
    LOGIC_MEM(ora, cpu.A | v, fetch_abs(jit_cur_pc) + cpu.Y, 4);
}
static int h_ora_indx() {
    LOGIC_MEM(ora, cpu.A | v, fetch_ind_x(jit_cur_pc), 6);
}
static int h_ora_indy() {
    LOGIC_MEM(ora, cpu.A | v, fetch_ind_y(jit_cur_pc), 5);
}

static int h_eor_zp() { LOGIC_MEM(eor, cpu.A ^ v, fetch_zp(jit_cur_pc), 3); }
static int h_eor_zpx() {
    LOGIC_MEM(eor, cpu.A ^ v, fetch_zp_idx(jit_cur_pc, cpu.X), 4);
}
static int h_eor_abs() { LOGIC_MEM(eor, cpu.A ^ v, fetch_abs(jit_cur_pc), 4); }
static int h_eor_absx() {
    LOGIC_MEM(eor, cpu.A ^ v, fetch_abs(jit_cur_pc) + cpu.X, 4);
}
static int h_eor_absy() {
    LOGIC_MEM(eor, cpu.A ^ v, fetch_abs(jit_cur_pc) + cpu.Y, 4);
}
static int h_eor_indx() {
    LOGIC_MEM(eor, cpu.A ^ v, fetch_ind_x(jit_cur_pc), 6);
}
static int h_eor_indy() {
    LOGIC_MEM(eor, cpu.A ^ v, fetch_ind_y(jit_cur_pc), 5);
}

// CMP/CPX/CPY
static inline void do_cmp(uint8_t reg, uint8_t v) {
    int diff = reg - v;
    set_carry(diff >= 0);
    SET_ZN(diff & 0xFF);
}
static int h_cmp_imm(void) {
    uint8_t v = memory_readb(jit_cur_pc + 1);
    do_cmp(cpu.A, v);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_cmp_zp(void) {
    uint8_t v = memory_readb(fetch_zp(jit_cur_pc));
    do_cmp(cpu.A, v);
    cpu.PC = jit_cur_pc + 2;
    return 3;
}
static int h_cmp_zpx(void) {
    uint8_t v = memory_readb(fetch_zp_idx(jit_cur_pc, cpu.X));
    do_cmp(cpu.A, v);
    cpu.PC = jit_cur_pc + 2;
    return 4;
}
static int h_cmp_abs(void) {
    uint8_t v = memory_readb(fetch_abs(jit_cur_pc));
    do_cmp(cpu.A, v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}
static int h_cmp_absx(void) {
    uint8_t v = memory_readb(fetch_abs(jit_cur_pc) + cpu.X);
    do_cmp(cpu.A, v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}
static int h_cmp_absy(void) {
    uint8_t v = memory_readb(fetch_abs(jit_cur_pc) + cpu.Y);
    do_cmp(cpu.A, v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}
static int h_cmp_indx(void) {
    uint8_t v = memory_readb(fetch_ind_x(jit_cur_pc));
    do_cmp(cpu.A, v);
    cpu.PC = jit_cur_pc + 2;
    return 6;
}
static int h_cmp_indy(void) {
    uint8_t v = memory_readb(fetch_ind_y(jit_cur_pc));
    do_cmp(cpu.A, v);
    cpu.PC = jit_cur_pc + 2;
    return 5;
}

static int h_cpx_imm(void) {
    uint8_t v = memory_readb(jit_cur_pc + 1);
    do_cmp(cpu.X, v);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_cpx_zp(void) {
    uint8_t v = memory_readb(fetch_zp(jit_cur_pc));
    do_cmp(cpu.X, v);
    cpu.PC = jit_cur_pc + 2;
    return 3;
}
static int h_cpx_abs(void) {
    uint8_t v = memory_readb(fetch_abs(jit_cur_pc));
    do_cmp(cpu.X, v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}

static int h_cpy_imm(void) {
    uint8_t v = memory_readb(jit_cur_pc + 1);
    do_cmp(cpu.Y, v);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_cpy_zp(void) {
    uint8_t v = memory_readb(fetch_zp(jit_cur_pc));
    do_cmp(cpu.Y, v);
    cpu.PC = jit_cur_pc + 2;
    return 3;
}
static int h_cpy_abs(void) {
    uint8_t v = memory_readb(fetch_abs(jit_cur_pc));
    do_cmp(cpu.Y, v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}

// 分支
static inline int branch_if(bool cond) {
    int8_t off = fetch_rel(jit_cur_pc);
    uint16_t old = jit_cur_pc + 2;
    if (!cond) {
        cpu.PC = old;
        return 2;
    }
    cpu.PC = old + off;
    // 跨页加 1 周期
    int cyc = 3;
    if ((old & 0xFF00) != (cpu.PC & 0xFF00))
        cyc++;
    return cyc;
}
static int h_beq(void) { return branch_if(cpu.P & 0x02); }
static int h_bne(void) { return branch_if(!(cpu.P & 0x02)); }
static int h_bcs(void) { return branch_if(cpu.P & 0x01); }
static int h_bcc(void) { return branch_if(!(cpu.P & 0x01)); }
static int h_bmi(void) { return branch_if(cpu.P & 0x80); }
static int h_bpl(void) { return branch_if(!(cpu.P & 0x80)); }
static int h_bvs(void) { return branch_if(cpu.P & 0x40); }
static int h_bvc(void) { return branch_if(!(cpu.P & 0x40)); }

// JMP/JSR/RTS
static int h_jmp_abs(void) {
    cpu.PC = fetch_abs(jit_cur_pc);
    return 3;
}
static int h_jmp_ind(void) {
    uint16_t addr = fetch_abs(jit_cur_pc);
    uint16_t lo = memory_readb(addr);
    uint16_t hi =
        memory_readb((addr & 0xFF00) | ((addr + 1) & 0x00FF)); // 6502 bug wrap
    cpu.PC = lo | (hi << 8);
    return 5;
}
static int h_jsr(void) {
    uint16_t target = fetch_abs(jit_cur_pc);
    uint16_t ret = jit_cur_pc + 2;
    cpu_stack_pushb((ret >> 8) & 0xFF);
    cpu_stack_pushb(ret & 0xFF);
    cpu.PC = target;
    return 6;
}
static int h_rts(void) {
    uint16_t lo = cpu_stack_popb();
    uint16_t hi = cpu_stack_popb();
    cpu.PC = (hi << 8 | lo) + 1;
    return 6;
}

// LDA 家族
static int h_lda_imm(void) {
    uint8_t v = memory_readb(jit_cur_pc + 1);
    cpu.A = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_lda_zp(void) {
    uint16_t a = fetch_zp(jit_cur_pc);
    uint8_t v = memory_readb(a);
    cpu.A = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 3;
}
static int h_lda_zpx(void) {
    uint16_t a = fetch_zp_idx(jit_cur_pc, cpu.X);
    uint8_t v = memory_readb(a);
    cpu.A = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 4;
}
static int h_lda_abs(void) {
    uint16_t a = fetch_abs(jit_cur_pc);
    uint8_t v = memory_readb(a);
    cpu.A = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}
static int h_lda_absx(void) {
    uint16_t a = fetch_abs(jit_cur_pc) + cpu.X;
    uint8_t v = memory_readb(a);
    cpu.A = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}
static int h_lda_absy(void) {
    uint16_t a = fetch_abs(jit_cur_pc) + cpu.Y;
    uint8_t v = memory_readb(a);
    cpu.A = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}
static int h_lda_indx(void) {
    uint16_t a = fetch_ind_x(jit_cur_pc);
    uint8_t v = memory_readb(a);
    cpu.A = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 6;
}
static int h_lda_indy(void) {
    uint16_t a = fetch_ind_y(jit_cur_pc);
    uint8_t v = memory_readb(a);
    cpu.A = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 5;
}

// LDX
static int h_ldx_imm(void) {
    uint8_t v = memory_readb(jit_cur_pc + 1);
    cpu.X = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_ldx_zp(void) {
    uint16_t a = fetch_zp(jit_cur_pc);
    uint8_t v = memory_readb(a);
    cpu.X = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 3;
}
static int h_ldx_zpy(void) {
    uint16_t a = fetch_zp_idx(jit_cur_pc, cpu.Y);
    uint8_t v = memory_readb(a);
    cpu.X = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 4;
}
static int h_ldx_abs(void) {
    uint16_t a = fetch_abs(jit_cur_pc);
    uint8_t v = memory_readb(a);
    cpu.X = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}
static int h_ldx_absy(void) {
    uint16_t a = fetch_abs(jit_cur_pc) + cpu.Y;
    uint8_t v = memory_readb(a);
    cpu.X = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}

// LDY
static int h_ldy_imm(void) {
    uint8_t v = memory_readb(jit_cur_pc + 1);
    cpu.Y = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 2;
}
static int h_ldy_zp(void) {
    uint16_t a = fetch_zp(jit_cur_pc);
    uint8_t v = memory_readb(a);
    cpu.Y = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 3;
}
static int h_ldy_zpx(void) {
    uint16_t a = fetch_zp_idx(jit_cur_pc, cpu.X);
    uint8_t v = memory_readb(a);
    cpu.Y = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 2;
    return 4;
}
static int h_ldy_abs(void) {
    uint16_t a = fetch_abs(jit_cur_pc);
    uint8_t v = memory_readb(a);
    cpu.Y = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}
static int h_ldy_absx(void) {
    uint16_t a = fetch_abs(jit_cur_pc) + cpu.X;
    uint8_t v = memory_readb(a);
    cpu.Y = v;
    SET_ZN(v);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}

// STA
static int h_sta_zp(void) {
    uint16_t a = fetch_zp(jit_cur_pc);
    memory_writeb(a, cpu.A);
    cpu.PC = jit_cur_pc + 2;
    return 3;
}
static int h_sta_zpx(void) {
    uint16_t a = fetch_zp_idx(jit_cur_pc, cpu.X);
    memory_writeb(a, cpu.A);
    cpu.PC = jit_cur_pc + 2;
    return 4;
}
static int h_sta_abs(void) {
    uint16_t a = fetch_abs(jit_cur_pc);
    memory_writeb(a, cpu.A);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}
static int h_sta_absx(void) {
    uint16_t a = fetch_abs(jit_cur_pc) + cpu.X;
    memory_writeb(a, cpu.A);
    cpu.PC = jit_cur_pc + 3;
    return 5;
}
static int h_sta_absy(void) {
    uint16_t a = fetch_abs(jit_cur_pc) + cpu.Y;
    memory_writeb(a, cpu.A);
    cpu.PC = jit_cur_pc + 3;
    return 5;
}
static int h_sta_indx(void) {
    uint16_t a = fetch_ind_x(jit_cur_pc);
    memory_writeb(a, cpu.A);
    cpu.PC = jit_cur_pc + 2;
    return 6;
}
static int h_sta_indy(void) {
    uint16_t a = fetch_ind_y(jit_cur_pc);
    memory_writeb(a, cpu.A);
    cpu.PC = jit_cur_pc + 2;
    return 6;
}

// STX
static int h_stx_zp(void) {
    uint16_t a = fetch_zp(jit_cur_pc);
    memory_writeb(a, cpu.X);
    cpu.PC = jit_cur_pc + 2;
    return 3;
}
static int h_stx_zpy(void) {
    uint16_t a = fetch_zp_idx(jit_cur_pc, cpu.Y);
    memory_writeb(a, cpu.X);
    cpu.PC = jit_cur_pc + 2;
    return 4;
}
static int h_stx_abs(void) {
    uint16_t a = fetch_abs(jit_cur_pc);
    memory_writeb(a, cpu.X);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}

// STY
static int h_sty_zp(void) {
    uint16_t a = fetch_zp(jit_cur_pc);
    memory_writeb(a, cpu.Y);
    cpu.PC = jit_cur_pc + 2;
    return 3;
}
static int h_sty_zpx(void) {
    uint16_t a = fetch_zp_idx(jit_cur_pc, cpu.X);
    memory_writeb(a, cpu.Y);
    cpu.PC = jit_cur_pc + 2;
    return 4;
}
static int h_sty_abs(void) {
    uint16_t a = fetch_abs(jit_cur_pc);
    memory_writeb(a, cpu.Y);
    cpu.PC = jit_cur_pc + 3;
    return 4;
}

// fallback：调用解释器单步
static int h_interp(void) { return cpu_step_interp(jit_cur_pc); }

// ---------------- JIT 编译：生成机器码模板 ----------------

// 编译并安装到缓存
static jit_func_t jit_compile(uint16_t pc, uint8_t *len_out) {
    static int dbg_compile = 0;
    uint8_t op = memory_readb(pc);
    uint8_t len = 1;
    uint32_t *buf = code_pool[pc];
    int p = 0;
    p = emit_prologue(buf, p);
    switch (op) {
    // ---------------- LDA ----------------
    case 0xA9: { // LDA #imm
        uint8_t imm = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        buf[p++] = la_ori(4, 0, imm); // a0 = imm
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 2);
        len = 2;
        break;
    }
    case 0xA5: { // LDA zp
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_jirl(1, 18, 0); // r4 = mem[addr]
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 3);
        len = 2;
        break;
    }
    case 0xB5: { // LDA zp,X
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X); // X
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF); // wrap zp
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 4);
        len = 2;
        break;
    }
    case 0xAD: { // LDA abs
        uint16_t addr = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 4, addr);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0xBD: { // LDA abs,X
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X); // X
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16); // mask 16-bit
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0xB9: { // LDA abs,Y
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_Y); // Y
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0xA1: { // LDA (zp,X) — 指针运行时读取
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X); // X
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF); // t = (zp+X)&0xFF
        buf[p++] = la_jirl(1, 18, 0);   // lo = mem[t]
        buf[p++] = la_or(14, 4, 0);     // save t
        buf[p++] = la_addi_w(4, 4, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi = mem[(t+1)&0xFF]
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 14);   // addr
        buf[p++] = la_jirl(1, 18, 0); // value = mem[addr]
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 6);
        len = 2;
        break;
    }
    case 0xB1: { // LDA (zp),Y — 指针运行时读取
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(13, 0, zp); // base
        buf[p++] = la_or(4, 13, 0);
        buf[p++] = la_jirl(1, 18, 0); // lo = mem[base]
        buf[p++] = la_or(15, 4, 0);   // save lo
        buf[p++] = la_addi_w(4, 13, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi = mem[(base+1)&0xFF]
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 15);         // addr = hi<<8 | lo
        buf[p++] = la_ld_bu(14, REG_CPU, OFF_Y); // Y
        buf[p++] = la_add_w(4, 4, 14);
        buf[p++] = la_sll_w(4, 4, 16); // mask 16-bit
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0); // value
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 5);
        len = 2;
        break;
    }

    // ---------------- LDX ----------------
    case 0xA2: { // LDX #imm
        uint8_t imm = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        buf[p++] = la_ori(4, 0, imm);
        buf[p++] = la_st_b(4, REG_CPU, OFF_X);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 2);
        len = 2;
        break;
    }
    case 0xA6: { // LDX zp
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_X);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 3);
        len = 2;
        break;
    }
    case 0xB6: { // LDX zp,Y
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_Y);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_X);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 4);
        len = 2;
        break;
    }
    case 0xAE: { // LDX abs
        uint16_t addr = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 4, addr);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_X);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0xBE: { // LDX abs,Y
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_Y);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_X);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }

    // ---------------- LDY ----------------
    case 0xA0: { // LDY #imm
        uint8_t imm = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        buf[p++] = la_ori(4, 0, imm);
        buf[p++] = la_st_b(4, REG_CPU, OFF_Y);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 2);
        len = 2;
        break;
    }
    case 0xA4: { // LDY zp
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_Y);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 3);
        len = 2;
        break;
    }
    case 0xB4: { // LDY zp,X
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_Y);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 4);
        len = 2;
        break;
    }
    case 0xAC: { // LDY abs
        uint16_t addr = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 4, addr);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_Y);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0xBC: { // LDY abs,X
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_st_b(4, REG_CPU, OFF_Y);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }

    // ---------------- STA ----------------
    case 0x85: { // STA zp
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A); // value
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 3);
        len = 2;
        break;
    }
    case 0x95: { // STA zp,X
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 4);
        len = 2;
        break;
    }
    case 0x8D: { // STA abs
        uint16_t addr = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        p = emit_li32(buf, p, 4, addr);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x9D: { // STA abs,X
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 5);
        len = 3;
        break;
    }
    case 0x99: { // STA abs,Y
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_Y);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 5);
        len = 3;
        break;
    }
    case 0x81: { // STA (zp,X)
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF); // tmp
        buf[p++] = la_jirl(1, 18, 0);   // lo
        buf[p++] = la_or(14, 4, 0);
        buf[p++] = la_addi_w(4, 4, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 14); // addr
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 6);
        len = 2;
        break;
    }
    case 0x91: { // STA (zp),Y
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ori(13, 0, zp);
        buf[p++] = la_or(4, 13, 0);
        buf[p++] = la_jirl(1, 18, 0); // lo
        buf[p++] = la_or(15, 4, 0);
        buf[p++] = la_addi_w(4, 13, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 15);         // addr
        buf[p++] = la_ld_bu(14, REG_CPU, OFF_Y); // Y
        buf[p++] = la_add_w(4, 4, 14);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 6);
        len = 2;
        break;
    }

    // ---------------- STX ----------------
    case 0x86: { // STX zp
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_X);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 3);
        len = 2;
        break;
    }
    case 0x96: { // STX zp,Y
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_Y);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_X);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 4);
        len = 2;
        break;
    }
    case 0x8E: { // STX abs
        uint16_t addr = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        p = emit_li32(buf, p, 4, addr);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_X);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }

    // ---------------- STY ----------------
    case 0x84: { // STY zp
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_Y);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 3);
        len = 2;
        break;
    }
    case 0x94: { // STY zp,X
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_Y);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 4);
        len = 2;
        break;
    }
    case 0x8C: { // STY abs
        uint16_t addr = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 19, (uint32_t)(uintptr_t)memory_writeb);
        p = emit_li32(buf, p, 4, addr);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_Y);
        buf[p++] = la_jirl(1, 19, 0);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }

    // ---------------- AND ----------------
    case 0x29: { // AND #imm
        uint8_t imm = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        buf[p++] = la_ld_bu(4, REG_CPU, OFF_A); // A
        buf[p++] = la_ori(5, 0, imm);
        buf[p++] = la_and(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 2);
        len = 2;
        break;
    }
    case 0x25: { // AND zp
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_jirl(1, 18, 0); // r4 = mem
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_and(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 3);
        len = 2;
        break;
    }
    case 0x35: { // AND zp,X
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_and(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 4);
        len = 2;
        break;
    }
    case 0x2D: { // AND abs
        uint16_t addr = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 4, addr);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_and(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x3D: { // AND abs,X
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_and(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x39: { // AND abs,Y
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_Y);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_and(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x21: { // AND (zp,X)
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF); // t = zp+x
        buf[p++] = la_jirl(1, 18, 0);   // lo
        buf[p++] = la_or(14, 4, 0);
        buf[p++] = la_addi_w(4, 4, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 14);   // addr
        buf[p++] = la_jirl(1, 18, 0); // value
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_and(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 6);
        len = 2;
        break;
    }
    case 0x31: { // AND (zp),Y
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(13, 0, zp);
        buf[p++] = la_or(4, 13, 0);
        buf[p++] = la_jirl(1, 18, 0); // lo
        buf[p++] = la_or(15, 4, 0);
        buf[p++] = la_addi_w(4, 13, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 15);         // addr
        buf[p++] = la_ld_bu(14, REG_CPU, OFF_Y); // Y
        buf[p++] = la_add_w(4, 4, 14);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0); // value
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_and(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 5);
        len = 2;
        break;
    }

    // ---------------- ORA ----------------
    case 0x09: { // ORA #imm
        uint8_t imm = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        buf[p++] = la_ld_bu(4, REG_CPU, OFF_A);
        buf[p++] = la_ori(5, 0, imm);
        buf[p++] = la_or(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 2);
        len = 2;
        break;
    }
    case 0x05: { // ORA zp
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_or(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 3);
        len = 2;
        break;
    }
    case 0x15: { // ORA zp,X
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_or(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 4);
        len = 2;
        break;
    }
    case 0x0D: { // ORA abs
        uint16_t addr = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 4, addr);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_or(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x1D: { // ORA abs,X
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_or(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x19: { // ORA abs,Y
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_Y);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_or(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x01: { // ORA (zp,X)
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // lo
        buf[p++] = la_or(14, 4, 0);
        buf[p++] = la_addi_w(4, 4, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 14);
        buf[p++] = la_jirl(1, 18, 0); // value
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_or(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 6);
        len = 2;
        break;
    }
    case 0x11: { // ORA (zp),Y
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(13, 0, zp);
        buf[p++] = la_or(4, 13, 0);
        buf[p++] = la_jirl(1, 18, 0); // lo
        buf[p++] = la_or(15, 4, 0);
        buf[p++] = la_addi_w(4, 13, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 15);         // addr
        buf[p++] = la_ld_bu(14, REG_CPU, OFF_Y); // Y
        buf[p++] = la_add_w(4, 4, 14);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0); // value
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_or(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 5);
        len = 2;
        break;
    }

    // ---------------- EOR ----------------
    case 0x49: { // EOR #imm
        uint8_t imm = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        buf[p++] = la_ld_bu(4, REG_CPU, OFF_A);
        buf[p++] = la_ori(5, 0, imm);
        buf[p++] = la_xor(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 2);
        len = 2;
        break;
    }
    case 0x45: { // EOR zp
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_xor(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 3);
        len = 2;
        break;
    }
    case 0x55: { // EOR zp,X
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_xor(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 4);
        len = 2;
        break;
    }
    case 0x4D: { // EOR abs
        uint16_t addr = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        p = emit_li32(buf, p, 4, addr);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_xor(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x5D: { // EOR abs,X
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_xor(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x59: { // EOR abs,Y
        uint16_t base = memory_readb(pc + 1) | (memory_readb(pc + 2) << 8);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_Y);
        p = emit_li32(buf, p, 14, base);
        buf[p++] = la_add_w(4, 14, 13);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0);
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_xor(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 3);
        p = emit_ret_cycles(buf, p, 4);
        len = 3;
        break;
    }
    case 0x41: { // EOR (zp,X)
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ld_bu(13, REG_CPU, OFF_X);
        buf[p++] = la_ori(4, 0, zp);
        buf[p++] = la_add_w(4, 4, 13);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // lo
        buf[p++] = la_or(14, 4, 0);
        buf[p++] = la_addi_w(4, 4, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 14);
        buf[p++] = la_jirl(1, 18, 0); // value
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_xor(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 6);
        len = 2;
        break;
    }
    case 0x51: { // EOR (zp),Y
        uint8_t zp = memory_readb(pc + 1);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)&cpu);
        p = emit_li32(buf, p, 18, (uint32_t)(uintptr_t)memory_readb);
        buf[p++] = la_ori(13, 0, zp);
        buf[p++] = la_or(4, 13, 0);
        buf[p++] = la_jirl(1, 18, 0); // lo
        buf[p++] = la_or(15, 4, 0);
        buf[p++] = la_addi_w(4, 13, 1);
        buf[p++] = la_andi(4, 4, 0xFF);
        buf[p++] = la_jirl(1, 18, 0); // hi
        buf[p++] = la_sll_w(4, 4, 8);
        buf[p++] = la_or(4, 4, 15);         // addr
        buf[p++] = la_ld_bu(14, REG_CPU, OFF_Y); // Y
        buf[p++] = la_add_w(4, 4, 14);
        buf[p++] = la_sll_w(4, 4, 16);
        buf[p++] = la_srl_w(4, 4, 16);
        buf[p++] = la_jirl(1, 18, 0); // value
        buf[p++] = la_ld_bu(5, REG_CPU, OFF_A);
        buf[p++] = la_xor(4, 4, 5);
        buf[p++] = la_st_b(4, REG_CPU, OFF_A);
        p = emit_set_zn(buf, p);
        p = emit_pc_advance(buf, p, 2);
        p = emit_ret_cycles(buf, p, 5);
        len = 2;
        break;
    }

    // ---------------- ADC/SBC/CMP/CPX/CPY (call helpers) ----------------
    case 0x69: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_adc_imm);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0x65: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_adc_zp);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0x75: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_adc_zpx);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0x6D: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_adc_abs);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }
    case 0x7D: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_adc_absx);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }
    case 0x79: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_adc_absy);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }
    case 0x61: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_adc_indx);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0x71: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_adc_indy);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }

    case 0xE9: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_sbc_imm);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xE5: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_sbc_zp);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xF5: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_sbc_zpx);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xED: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_sbc_abs);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }
    case 0xFD: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_sbc_absx);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }
    case 0xF9: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_sbc_absy);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }
    case 0xE1: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_sbc_indx);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xF1: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_sbc_indy);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }

    case 0xC9: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cmp_imm);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xC5: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cmp_zp);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xD5: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cmp_zpx);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xCD: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cmp_abs);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }
    case 0xDD: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cmp_absx);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }
    case 0xD9: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cmp_absy);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }
    case 0xC1: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cmp_indx);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xD1: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cmp_indy);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }

    case 0xE0: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cpx_imm);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xE4: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cpx_zp);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xEC: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cpx_abs);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }

    case 0xC0: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cpy_imm);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xC4: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cpy_zp);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 2;
        break;
    }
    case 0xCC: {
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)h_cpy_abs);
        buf[p++] = la_jirl(1, 12, 0);
        buf[p++] = la_jirl(0, 1, 0);
        len = 3;
        break;
    }

    default:
        // fallback stub -> cpu_step_interp(pc)
        p = emit_li32(buf, p, 4, pc);
        p = emit_li32(buf, p, 12, (uint32_t)(uintptr_t)cpu_step_interp);
        buf[p++] = la_jirl(1, 12, 0);
        // epilogue handles ret
        break;
    }
    p = emit_epilogue(buf, p);
    flush_icache(buf, buf + p);
    if (dbg_compile < 4) {
        printf("[JITCODE pc=%04x len=%d words=%d]\\n", pc, len, p);
        int limit = p < 12 ? p : 12;
        for (int i = 0; i < limit; i++) {
            printf("  [%02d] %08x\\n", i, buf[i]);
        }
    }
    pc_len[pc] = len;
    *len_out = len;
    return (jit_func_t)buf;
}

void jit_init(void) {
    memset(pc_func, 0, sizeof(pc_func));
    memset(pc_len, 0, sizeof(pc_len));
    stat_hits = stat_misses = stat_invalid = stat_calls = 0;
}

void jit_invalidate(uint16_t addr, uint16_t addr_end) {
    if (addr_end < addr)
        addr_end = addr;
    for (uint32_t a = addr; a <= addr_end; a++) {
        pc_func[a] = NULL;
        pc_len[a] = 0;
    }
    stat_invalid++;
}

int jit_run(uint16_t pc) {
    if (!jit_enabled)
        return 0;
    stat_calls++;
    static int dbg_compile = 0;
    jit_func_t fn = pc_func[pc];
    if (!fn) {
        stat_misses++;
        uint8_t len = 0;
        fn = jit_compile(pc, &len);
        if (dbg_compile < 5) {
            uint8_t op = memory_readb(pc);
            printf("[JITDBG] miss pc=%04x op=%02x len=%d\n", pc, op, len);
        }
        if (!fn)
            return 0;
        pc_func[pc] = fn;
    } else {
        stat_hits++;
        if (dbg_compile < 5) {
            uint8_t op = memory_readb(pc);
            printf("[JITDBG] hit pc=%04x op=%02x\n", pc, op);
        }
    }
    dbg_compile++;
    jit_cur_pc = pc;
    if (dbg_compile < 8) {
        printf("[JITDBG-CALL] pc=%04x fn=%p\n", pc, fn);
        uint32_t *w = (uint32_t *)fn;
        for (int i = 0; i < 8; i++) printf("  W[%d]=%08x\n", i, w[i]);
    }
    int ret = fn();
    if (dbg_compile < 8) {
        printf("[JITDBG-RUN] pc=%04x ret=%d\n", pc, ret);
    }
    return ret;
}

// 单步模式：不写入缓存，供 DiffTest 使用，返回 cycles
int jit_run_single(uint16_t pc) {
    // 如果已经有缓存可直接用，但不会写新缓存
    stat_calls++;
    jit_func_t fn = pc_func[pc];
    if (!fn) {
        uint8_t len = 0;
        fn = jit_compile(pc, &len);
        if (!fn)
            return 0;
    } else {
        // 使用缓存的 fn 但不记录命中
    }
    jit_cur_pc = pc;
    return fn();
}

void jit_print_stats(void) {
    int total = stat_hits + stat_misses;
    int rate100 = total ? (stat_hits * 10000 + total / 2) / total : 0; // 放大100倍取整
    printf("[JIT] calls=%d hits=%d misses=%d invalid=%d hit-rate=%d.%02d%%\n",
           stat_calls, stat_hits, stat_misses, stat_invalid,
           rate100 / 100, rate100 % 100);
}

void jit_reset_stats(void) { jit_init(); }

void jit_dump_code(void) {
    printf("[JIT] simple mode: load/store + AND/ORA/EOR opcodes are native; "
           "ADC/SBC/CMP/CPX/CPY call mini C helpers; others fall back to "
           "interpreter\n");
}
