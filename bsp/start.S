/*
 * start.S - LoongArch32 裸机启动代码
 *
 * 功能:
 *   1. 设置栈指针 (使用链接脚本定义的 __stack_top)
 *   2. 清零 BSS 段
 *   3. 调用 main 函数
 *   4. main 返回后进入死循环
 */

    .section .text.start
    .globl _start
    .type _start, @function

_start:
    /* ======== 1. 设置栈指针 ======== */
    la.local    $sp, __stack_top

    /* ======== 2. 清零 BSS 段 ======== */
    la.local    $t0, __bss_start
    la.local    $t1, __bss_end

.L_clear_bss:
    beq         $t0, $t1, .L_bss_done
    st.w        $zero, $t0, 0
    addi.w      $t0, $t0, 4
    b           .L_clear_bss

.L_bss_done:
    /* ======== 3. 调用 main ======== */
    bl          main

    /* ======== 4. main 返回后死循环 ======== */
.L_halt:
    b           .L_halt

    .size _start, . - _start
