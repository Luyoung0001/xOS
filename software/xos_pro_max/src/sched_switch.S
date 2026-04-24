/*------------------------------------------------------------------------------
 * xOS Context Switch (LoongArch32R Assembly)
 *
 * switch_to(task_t *task)
 *   - Restores context from TCB and jumps to task
 *   - Never returns
 *----------------------------------------------------------------------------*/

.section .text
.global switch_to

/*
 * switch_to(task_t *task)
 * a0 = pointer to task_t
 *
 * Task structure layout (must match task_t in sched.h):
 *   offset 0-127:   regs[32]  (32 registers × 4 bytes)
 *   offset 128:     era
 *   offset 132:     prmd
 */
switch_to:
    /* a0 points to task_t, which starts with regs[32] */

    /* Restore general purpose registers from TCB */
    /* Note: We restore most registers, but skip $r0 (always 0) */

    ld.w    $r1,  $a0, 4       # $ra
    ld.w    $r2,  $a0, 8       # $tp
    ld.w    $r3,  $a0, 12      # $sp (critical!)

    /* Save a0 temporarily, we need it to access TCB */
    move    $t0, $a0

    ld.w    $r4,  $t0, 16      # $a0
    ld.w    $r5,  $t0, 20      # $a1
    ld.w    $r6,  $t0, 24      # $a2
    ld.w    $r7,  $t0, 28      # $a3
    ld.w    $r8,  $t0, 32      # $a4
    ld.w    $r9,  $t0, 36      # $a5
    ld.w    $r10, $t0, 40      # $a6
    ld.w    $r11, $t0, 44      # $a7

    /* Restore temporary registers (t2-t8, skip t0 and t1 for now) */
    ld.w    $r14, $t0, 56      # $t2
    ld.w    $r15, $t0, 60      # $t3
    ld.w    $r16, $t0, 64      # $t4
    ld.w    $r17, $t0, 68      # $t5
    ld.w    $r18, $t0, 72      # $t6
    ld.w    $r19, $t0, 76      # $t7
    ld.w    $r20, $t0, 80      # $t8

    /* Restore r21 and saved registers */
    ld.w    $r21, $t0, 84      # $r21
    ld.w    $r22, $t0, 88      # $fp
    ld.w    $r23, $t0, 92      # $s0
    ld.w    $r24, $t0, 96      # $s1
    ld.w    $r25, $t0, 100     # $s2
    ld.w    $r26, $t0, 104     # $s3
    ld.w    $r27, $t0, 108     # $s4
    ld.w    $r28, $t0, 112     # $s5
    ld.w    $r29, $t0, 116     # $s6
    ld.w    $r30, $t0, 120     # $s7
    ld.w    $r31, $t0, 124     # $s8

    /* Restore CSR registers (use $t1 as temp before restoring it) */
    /* ERA is at offset 128 (32 regs × 4 bytes) */
    ld.w    $t1, $t0, 128      # Load ERA
    csrwr   $t1, 0x6           # Write to CSR_ERA

    /* PRMD is at offset 132 */
    ld.w    $t1, $t0, 132      # Load PRMD
    csrwr   $t1, 0x1           # Write to CSR_PRMD
    
    /* Now restore $t1 and $t0 (must be last) */
    ld.w    $r13, $t0, 52      # Restore $t1
    ld.w    $r12, $t0, 48      # Restore $t0 (last!)

    /* Return to task using ertn */
    /* This will:
     *   - Jump to address in ERA
     *   - Restore privilege level from PRMD
     *   - Enable interrupts if PRMD.IE was set
     */
    ertn

.end
