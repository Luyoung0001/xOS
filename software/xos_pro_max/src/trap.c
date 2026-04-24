/*------------------------------------------------------------------------------
 * xOS Trap Handler Implementation
 *
 * C-level exception and interrupt handling
 *----------------------------------------------------------------------------*/

#include <trap.h>
#include <sched.h>
#include <ps2.h>
#include <uart.h>
#include <uart_display.h>
#include <stdio.h>

/*============================================================================
 * Interrupt Handler Table
 *============================================================================*/
irq_handler_t irq_handlers[IRQ_MAX] = {0};

/*============================================================================
 * Timer Interrupt Counter (for testing)
 *============================================================================*/
static volatile uint32_t timer_tick_count = 0;

/*============================================================================
 * Default Handlers
 *============================================================================*/

void default_irq_handler(trap_frame_t *tf) {
    (void)tf;  /* Unused */
    /* Do nothing - unhandled interrupt */
    // printf("now in %s\n", __func__);
}

/*============================================================================
 * Software Interrupt 0 Handler (for task_yield)
 *============================================================================*/
void swi0_irq_handler(trap_frame_t *tf) {
    /* 清除 SWI0 中断 */
    uint32_t estat = csr_read(CSR_ESTAT);
    estat &= ~ECFG_LIE_SWI0;
    csr_write(CSR_ESTAT, estat);

    /* 触发调度 */
    schedule(tf);
}

/*============================================================================
 * Timer Interrupt Handler
 *============================================================================*/
void timer_irq_handler(trap_frame_t *tf) {
    /* Increment tick counter */
    timer_tick_count++;

    /* Update timer overflow counter for uptime tracking */
    extern void timer_overflow_callback(void);
    timer_overflow_callback();

    /* Clear timer interrupt */
    csr_write(CSR_TICLR, 0x1);

    /* Trigger scheduler (never returns) */
    schedule(tf);
}

void exception_handler(trap_frame_t *tf) {

    uint32_t ecode = (tf->estat >> ESTAT_ECODE_SHIFT) & ESTAT_ECODE_MASK;

    printf("\n!!! Exception !!!\n");
    printf("ECODE: 0x%x\n", ecode);
    printf("ERA:   0x%08x\n", tf->era);

    switch (ecode) {
    case ECODE_SYS:
        printf("System Call\n");
        /* Skip the syscall instruction */
        tf->era += 4;
        break;

    case ECODE_BRK:
        printf("Breakpoint\n");
        tf->era += 4;
        break;

    case ECODE_ADEF:
        printf("Address Error (Fetch)\n");
        break;

    case ECODE_ALE:
        printf("Address Error (Load/Store)\n");
        break;

    case ECODE_INE:
        printf("Instruction Not Exist\n");
        break;

    default:
        printf("Unknown exception, halting...\n");
        while (1)
            ;
    }
}

/*============================================================================
 * Interrupt Dispatch
 *============================================================================*/

void interrupt_handler(trap_frame_t *tf) {
    uint32_t estat = tf->estat;
    uint32_t ecfg = read_csr_ecfg();
    uint32_t pending = estat & ecfg & ESTAT_IS_MASK;

    /* Check each interrupt source */
    for (int i = 0; i < IRQ_MAX; i++) {
        if (pending & (1 << i)) {
            if (irq_handlers[i]) {
                irq_handlers[i](tf);  /* Pass trap_frame */
            }
        }
    }
}

/*============================================================================
 * Main Trap Dispatch (called from trap.S)
 *============================================================================*/

void trap_dispatch(trap_frame_t *tf) {
    uint32_t ecode = (tf->estat >> ESTAT_ECODE_SHIFT) & ESTAT_ECODE_MASK;
    if (ecode == ECODE_INT) {
        /* Hardware/software interrupt */
        interrupt_handler(tf);
    } else {
        /* Exception */
        exception_handler(tf);
    }
}

/*============================================================================
 * API Implementation
 *============================================================================*/

void trap_init(void) {
    /* Initialize all handlers to default */
    for (int i = 0; i < IRQ_MAX; i++) {
        irq_handlers[i] = default_irq_handler;
    }

    /* Register SWI0 handler for task_yield */
    irq_handlers[IRQ_SWI0] = swi0_irq_handler;

    /* Get address of trap_entry using la.local (bypass GOT) */
    uint32_t entry_addr;
    asm volatile ("la.local %0, trap_entry" : "=r"(entry_addr));

    /* Set exception entry point */
    write_csr_eentry(entry_addr);

    /* Enable SWI0, UART and PS2 interrupts in ECFG */
    uint32_t ecfg = ECFG_LIE_SWI0 | ECFG_LIE_HWI1 | ECFG_LIE_HWI2;
    write_csr_ecfg(ecfg);

    printf("[TRAP] Initialized, eentry=0x%08x\n", entry_addr);
}

void irq_register(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < IRQ_MAX) {
        irq_handlers[irq] = handler ? handler : default_irq_handler;
    }
    printf("irq: %d   handler: 0x%0x\n", irq, handler);

}

void irq_enable(int irq) {
    if (irq >= 0 && irq < IRQ_MAX) {
        uint32_t ecfg = read_csr_ecfg();
        ecfg |= (1 << irq);
        write_csr_ecfg(ecfg);
    }
}

void irq_disable(int irq) {
    if (irq >= 0 && irq < IRQ_MAX) {
        uint32_t ecfg = read_csr_ecfg();
        ecfg &= ~(1 << irq);
        write_csr_ecfg(ecfg);
    }
}

void irq_global_enable(void) {
    uint32_t crmd = read_csr_crmd();
    crmd |= CRMD_IE;
    write_csr_crmd(crmd);
}

void irq_global_disable(void) {

    uint32_t crmd = read_csr_crmd();
    crmd &= ~CRMD_IE;
    write_csr_crmd(crmd);
}
