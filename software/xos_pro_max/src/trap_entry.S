/*------------------------------------------------------------------------------
 * xOS Trap Entry - LoongArch32R Assembly
 *
 * This is the low-level exception/interrupt entry point.
 * CSR_EENTRY points here. This code:
 *   1. Saves all registers to stack (trap frame)
 *   2. Calls C handler trap_dispatch()
 *   3. Restores all registers
 *   4. Returns from exception (ertn)
 *----------------------------------------------------------------------------*/

    .section .exception, "ax"
    .globl trap_entry
    .type trap_entry, @function
    .align 4

/*============================================================================
 * Trap Frame Layout (matches trap_frame_t in trap.h)
 * Total size: 35 * 4 = 140 bytes
 *============================================================================*/
#define TF_RA       0
#define TF_TP       4
#define TF_SP       8
#define TF_A0       12
#define TF_A1       16
#define TF_A2       20
#define TF_A3       24
#define TF_A4       28
#define TF_A5       32
#define TF_A6       36
#define TF_A7       40
#define TF_T0       44
#define TF_T1       48
#define TF_T2       52
#define TF_T3       56
#define TF_T4       60
#define TF_T5       64
#define TF_T6       68
#define TF_T7       72
#define TF_T8       76
#define TF_R21      80
#define TF_FP       84
#define TF_S0       88
#define TF_S1       92
#define TF_S2       96
#define TF_S3       100
#define TF_S4       104
#define TF_S5       108
#define TF_S6       112
#define TF_S7       116
#define TF_S8       120
#define TF_ERA      124
#define TF_PRMD     128
#define TF_ESTAT    132
#define TF_SIZE     140

/*============================================================================
 * CSR Numbers
 *============================================================================*/
#define CSR_CRMD    0x0
#define CSR_PRMD    0x1
#define CSR_ESTAT   0x5
#define CSR_ERA     0x6
#define CSR_SAVE0   0x30
#define CSR_SAVE1   0x31

trap_entry:
    /*
     * Use SAVE0/SAVE1 as scratch registers to save sp and t0
     * This allows us to set up the trap frame without corrupting any registers
     */
    csrwr   $t0, CSR_SAVE0      /* Save t0 to SAVE0 */

    csrwr   $sp, CSR_SAVE1      /* Save sp to SAVE1 */
    csrrd   $sp, CSR_SAVE1      /* restore sp */

    /* Allocate trap frame on stack */
    addi.w  $sp, $sp, -TF_SIZE

    /* Save general purpose registers */
    st.w    $ra,  $sp, TF_RA
    st.w    $tp,  $sp, TF_TP
    /* sp will be saved later from SAVE1 */
    st.w    $a0,  $sp, TF_A0
    st.w    $a1,  $sp, TF_A1
    st.w    $a2,  $sp, TF_A2
    st.w    $a3,  $sp, TF_A3
    st.w    $a4,  $sp, TF_A4
    st.w    $a5,  $sp, TF_A5
    st.w    $a6,  $sp, TF_A6
    st.w    $a7,  $sp, TF_A7
    /* t0 will be saved later from SAVE0 */
    st.w    $t1,  $sp, TF_T1
    st.w    $t2,  $sp, TF_T2
    st.w    $t3,  $sp, TF_T3
    st.w    $t4,  $sp, TF_T4
    st.w    $t5,  $sp, TF_T5
    st.w    $t6,  $sp, TF_T6
    st.w    $t7,  $sp, TF_T7
    st.w    $t8,  $sp, TF_T8
    st.w    $r21, $sp, TF_R21
    st.w    $fp,  $sp, TF_FP
    st.w    $s0,  $sp, TF_S0
    st.w    $s1,  $sp, TF_S1
    st.w    $s2,  $sp, TF_S2
    st.w    $s3,  $sp, TF_S3
    st.w    $s4,  $sp, TF_S4
    st.w    $s5,  $sp, TF_S5
    st.w    $s6,  $sp, TF_S6
    st.w    $s7,  $sp, TF_S7
    st.w    $s8,  $sp, TF_S8

    /* Restore and save t0 from SAVE0 */
    csrrd   $t0, CSR_SAVE0
    st.w    $t0,  $sp, TF_T0

    /* Restore and save original sp from SAVE1 */
    csrrd   $t0, CSR_SAVE1
    st.w    $t0,  $sp, TF_SP

    /* Save CSR values */
    csrrd   $t0, CSR_ERA
    st.w    $t0,  $sp, TF_ERA

    csrrd   $t0, CSR_PRMD
    st.w    $t0,  $sp, TF_PRMD

    csrrd   $t0, CSR_ESTAT
    st.w    $t0,  $sp, TF_ESTAT

    /*
     * Call C trap handler
     * Argument: $a0 = pointer to trap frame (current sp)
     */
    move    $a0, $sp
    bl      trap_dispatch

    /*
     * Restore CSR values
     */
    ld.w    $t0,  $sp, TF_ERA
    csrwr   $t0, CSR_ERA

    ld.w    $t0,  $sp, TF_PRMD
    csrwr   $t0, CSR_PRMD

    /*
     * Restore general purpose registers
     */
    ld.w    $ra,  $sp, TF_RA
    ld.w    $tp,  $sp, TF_TP
    /* Don't restore sp yet - we need it for the frame */
    ld.w    $a0,  $sp, TF_A0
    ld.w    $a1,  $sp, TF_A1
    ld.w    $a2,  $sp, TF_A2
    ld.w    $a3,  $sp, TF_A3
    ld.w    $a4,  $sp, TF_A4
    ld.w    $a5,  $sp, TF_A5
    ld.w    $a6,  $sp, TF_A6
    ld.w    $a7,  $sp, TF_A7
    ld.w    $t0,  $sp, TF_T0
    ld.w    $t1,  $sp, TF_T1
    ld.w    $t2,  $sp, TF_T2
    ld.w    $t3,  $sp, TF_T3
    ld.w    $t4,  $sp, TF_T4
    ld.w    $t5,  $sp, TF_T5
    ld.w    $t6,  $sp, TF_T6
    ld.w    $t7,  $sp, TF_T7
    ld.w    $t8,  $sp, TF_T8
    ld.w    $r21, $sp, TF_R21
    ld.w    $fp,  $sp, TF_FP
    ld.w    $s0,  $sp, TF_S0
    ld.w    $s1,  $sp, TF_S1
    ld.w    $s2,  $sp, TF_S2
    ld.w    $s3,  $sp, TF_S3
    ld.w    $s4,  $sp, TF_S4
    ld.w    $s5,  $sp, TF_S5
    ld.w    $s6,  $sp, TF_S6
    ld.w    $s7,  $sp, TF_S7
    ld.w    $s8,  $sp, TF_S8

    /* Restore sp (deallocate trap frame) */
    addi.w  $sp, $sp, TF_SIZE

    /* Return from exception */
    ertn

    .size trap_entry, . - trap_entry
