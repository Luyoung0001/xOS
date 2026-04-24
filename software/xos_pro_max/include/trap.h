/*------------------------------------------------------------------------------
 * xOS Trap/Interrupt Handler
 *
 * LoongArch32R Exception and Interrupt Handling
 *----------------------------------------------------------------------------*/

#ifndef __TRAP_H__
#define __TRAP_H__

#include <stdint.h>

/*============================================================================
 * LoongArch32 CSR Addresses
 *============================================================================*/
#define CSR_CRMD        0x0     /* Current Mode */
#define CSR_PRMD        0x1     /* Pre-exception Mode */
#define CSR_ECFG        0x4     /* Exception Configuration */
#define CSR_ESTAT       0x5     /* Exception Status */
#define CSR_ERA         0x6     /* Exception Return Address */
#define CSR_BADV        0x7     /* Bad Virtual Address */
#define CSR_EENTRY      0xC     /* Exception Entry Address */
#define CSR_SAVE0       0x30    /* Scratch Register 0 */
#define CSR_SAVE1       0x31    /* Scratch Register 1 */
#define CSR_SAVE2       0x32    /* Scratch Register 2 */
#define CSR_SAVE3       0x33    /* Scratch Register 3 */
#define CSR_TID         0x40    /* Timer ID */
#define CSR_TCFG        0x41    /* Timer Configuration */
#define CSR_TVAL        0x42    /* Timer Value */
#define CSR_TICLR       0x44    /* Timer Interrupt Clear */

/*============================================================================
 * CRMD Bits
 *============================================================================*/
#define CRMD_PLV_MASK   0x3     /* Privilege Level */
#define CRMD_IE         (1 << 2) /* Interrupt Enable */
#define CRMD_DA         (1 << 3) /* Direct Address translation */
#define CRMD_PG         (1 << 4) /* Paging enable */

/*============================================================================
 * ECFG Bits - Local Interrupt Enable
 *============================================================================*/
#define ECFG_LIE_SWI0   (1 << 0)  /* Software Interrupt 0 */
#define ECFG_LIE_SWI1   (1 << 1)  /* Software Interrupt 1 */
#define ECFG_LIE_HWI0   (1 << 2)  /* Hardware Interrupt 0 (unused) */
#define ECFG_LIE_HWI1   (1 << 3)  /* Hardware Interrupt 1 - UART */
#define ECFG_LIE_HWI2   (1 << 4)  /* Hardware Interrupt 2 - PS2 */
#define ECFG_LIE_HWI3   (1 << 5)  /* Hardware Interrupt 3 */
#define ECFG_LIE_HWI4   (1 << 6)  /* Hardware Interrupt 4 */
#define ECFG_LIE_HWI5   (1 << 7)  /* Hardware Interrupt 5 */
#define ECFG_LIE_HWI6   (1 << 8)  /* Hardware Interrupt 6 */
#define ECFG_LIE_HWI7   (1 << 9)  /* Hardware Interrupt 7 */
#define ECFG_LIE_PMI    (1 << 10) /* Performance Monitor Interrupt */
#define ECFG_LIE_TI     (1 << 11) /* Timer Interrupt */
#define ECFG_LIE_IPI    (1 << 12) /* Inter-Processor Interrupt */

/*============================================================================
 * ESTAT Bits
 *============================================================================*/
#define ESTAT_IS_MASK   0x1FFF    /* Interrupt Status [12:0] */
#define ESTAT_ECODE_SHIFT 16
#define ESTAT_ECODE_MASK  0x3F    /* Exception Code [21:16] */

/*============================================================================
 * TCFG Bits - Timer Configuration
 *============================================================================*/
#define TCFG_EN         (1 << 0)  /* Timer Enable */
#define TCFG_PERIODIC   (1 << 1)  /* Periodic Mode */
#define TCFG_INITVAL_SHIFT 2      /* InitVal starts at bit 2 */

/*============================================================================
 * Exception Codes (ECODE)
 *============================================================================*/
#define ECODE_INT       0x0     /* Interrupt */
#define ECODE_PIL       0x1     /* Page Invalid (Load) */
#define ECODE_PIS       0x2     /* Page Invalid (Store) */
#define ECODE_PIF       0x3     /* Page Invalid (Fetch) */
#define ECODE_PME       0x4     /* Page Modification */
#define ECODE_PPI       0x7     /* Page Privilege Invalid */
#define ECODE_ADEF      0x8     /* Address Error (Fetch) */
#define ECODE_ALE       0x9     /* Address Error (Load/Store) */
#define ECODE_SYS       0xB     /* System Call */
#define ECODE_BRK       0xC     /* Breakpoint */
#define ECODE_INE       0xD     /* Instruction Not Exist */
#define ECODE_IPE       0xE     /* Instruction Privilege Error */
#define ECODE_TLBR      0x3F    /* TLB Refill */

/*============================================================================
 * Interrupt Numbers (for handler registration)
 * These MUST match the bit positions in ECFG/ESTAT registers
 *============================================================================*/
#define IRQ_SWI0        0       /* Software Interrupt 0 */
#define IRQ_SWI1        1       /* Software Interrupt 1 */
#define IRQ_HWI0        2       /* Hardware Interrupt 0 (unused) */
#define IRQ_UART        3       /* UART interrupt (HWI1, bit 3) */
#define IRQ_PS2         4       /* PS2 keyboard interrupt (HWI2, bit 4) */
#define IRQ_HWI3        5       /* Hardware Interrupt 3 */
#define IRQ_HWI4        6       /* Hardware Interrupt 4 */
#define IRQ_HWI5        7       /* Hardware Interrupt 5 */
#define IRQ_HWI6        8       /* Hardware Interrupt 6 */
#define IRQ_HWI7        9       /* Hardware Interrupt 7 */
#define IRQ_PMI         10      /* Performance Monitor Interrupt */
#define IRQ_TIMER       11      /* Timer interrupt */
#define IRQ_IPI         12      /* Inter-Processor Interrupt */
#define IRQ_MAX         13      /* Maximum IRQ number */

/*============================================================================
 * Trap Frame - saved on stack during exception
 *============================================================================*/
typedef struct {
    uint32_t ra;        /* $r1  - Return Address */
    uint32_t tp;        /* $r2  - Thread Pointer */
    uint32_t sp;        /* $r3  - Stack Pointer */
    uint32_t a0;        /* $r4  - Argument 0 */
    uint32_t a1;        /* $r5  - Argument 1 */
    uint32_t a2;        /* $r6  - Argument 2 */
    uint32_t a3;        /* $r7  - Argument 3 */
    uint32_t a4;        /* $r8  - Argument 4 */
    uint32_t a5;        /* $r9  - Argument 5 */
    uint32_t a6;        /* $r10 - Argument 6 */
    uint32_t a7;        /* $r11 - Argument 7 */
    uint32_t t0;        /* $r12 - Temporary 0 */
    uint32_t t1;        /* $r13 - Temporary 1 */
    uint32_t t2;        /* $r14 - Temporary 2 */
    uint32_t t3;        /* $r15 - Temporary 3 */
    uint32_t t4;        /* $r16 - Temporary 4 */
    uint32_t t5;        /* $r17 - Temporary 5 */
    uint32_t t6;        /* $r18 - Temporary 6 */
    uint32_t t7;        /* $r19 - Temporary 7 */
    uint32_t t8;        /* $r20 - Temporary 8 */
    uint32_t r21;       /* $r21 - Reserved */
    uint32_t fp;        /* $r22 - Frame Pointer */
    uint32_t s0;        /* $r23 - Saved 0 */
    uint32_t s1;        /* $r24 - Saved 1 */
    uint32_t s2;        /* $r25 - Saved 2 */
    uint32_t s3;        /* $r26 - Saved 3 */
    uint32_t s4;        /* $r27 - Saved 4 */
    uint32_t s5;        /* $r28 - Saved 5 */
    uint32_t s6;        /* $r29 - Saved 6 */
    uint32_t s7;        /* $r30 - Saved 7 */
    uint32_t s8;        /* $r31 - Saved 8 */
    /* CSR values */
    uint32_t era;       /* Exception Return Address */
    uint32_t prmd;      /* Pre-exception Mode */
    uint32_t estat;     /* Exception Status */
} trap_frame_t;

/*============================================================================
 * Interrupt Handler Type
 *============================================================================*/
typedef void (*irq_handler_t)(trap_frame_t *tf);

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * Initialize trap/interrupt system
 * Sets up CSR_EENTRY and enables interrupts
 */
void trap_init(void);

/**
 * Register an interrupt handler
 * @param irq: IRQ number (IRQ_UART, IRQ_PS2, etc.)
 * @param handler: Handler function pointer
 */
void irq_register(int irq, irq_handler_t handler);

/**
 * Enable a specific interrupt
 * @param irq: IRQ number
 */
void irq_enable(int irq);

/**
 * Disable a specific interrupt
 * @param irq: IRQ number
 */
void irq_disable(int irq);

/**
 * Enable global interrupts
 */
void irq_global_enable(void);

/**
 * Disable global interrupts
 */
void irq_global_disable(void);


/*============================================================================
 * CSR Access Inline Functions
 *============================================================================*/

static inline uint32_t csr_read(uint32_t csr) {
    uint32_t val;
    asm volatile (
        "csrrd %0, %1"
        : "=r"(val)
        : "i"(csr)
    );
    return val;
}

static inline void csr_write(uint32_t csr, uint32_t val) {
    // +r tells the compiler that val is both input and output
    // so when the asm block is done, val will have the updated value
    // when we use val later, it will have the new value, so that is not right.
    // so compiler will give us a protected copy of val before asm block
    asm volatile (
        "csrwr %0, %1"
        : "+r"(val)
        : "i"(csr)
    );
}

static inline uint32_t csr_xchg(uint32_t csr, uint32_t val, uint32_t mask) {
    uint32_t old;
    asm volatile (
        "csrxchg %0, %1, %2"
        : "=r"(old)
        : "r"(val), "i"(csr), "0"(mask)
    );
    return old;
}



/* Macros for specific CSR access */
#define read_csr_crmd()     csr_read(CSR_CRMD)
#define read_csr_prmd()     csr_read(CSR_PRMD)
#define read_csr_ecfg()     csr_read(CSR_ECFG)
#define read_csr_estat()    csr_read(CSR_ESTAT)
#define read_csr_era()      csr_read(CSR_ERA)
#define read_csr_eentry()   csr_read(CSR_EENTRY)

#define write_csr_crmd(v)   csr_write(CSR_CRMD, v)
#define write_csr_prmd(v)   csr_write(CSR_PRMD, v)
#define write_csr_ecfg(v)   csr_write(CSR_ECFG, v)
#define write_csr_estat(v)  csr_write(CSR_ESTAT, v)
#define write_csr_era(v)    csr_write(CSR_ERA, v)
#define write_csr_eentry(v) csr_write(CSR_EENTRY, v)

/*============================================================================
 * Assembly Entry Point (defined in trap.S)
 *============================================================================*/
extern void trap_entry(void);

void __attribute__((noinline)) ps2_irq_handler(trap_frame_t *tf);

void __attribute__((noinline)) timer_irq_handler(trap_frame_t *tf);

void __attribute__((noinline)) default_irq_handler(trap_frame_t *tf);

#endif /* __TRAP_H__ */
