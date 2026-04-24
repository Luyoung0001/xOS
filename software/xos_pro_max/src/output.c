/*------------------------------------------------------------------------------
 * xOS Output Redirection
 *
 * Redirect printf output to HDMI terminal and task buffers
 *----------------------------------------------------------------------------*/

#include <hdmi_terminal.h>
#include <output.h>
#include <sched.h>
#include <uart.h>

/* Default to both HDMI and UART */
output_target_t output_target = OUTPUT_BOTH;

/*
 * Override putchar for printf redirection
 * This function is called by printf to output each character
 *
 * IMPORTANT: This must be defined with strong linkage to override
 * the weak symbol in BSP library
 */
int putchar(int c) {
    int tid = get_current_task();

#ifdef SIMULATION
    bsp_uart_putc(0, (char)c);
    return c;
#endif

    /* Output to HDMI if enabled */
    // this process may be slow
    if (get_output_target() & OUTPUT_HDMI) {
        if (tid >= 0) {
            terminal_putchar(tid, (char)c);
        } else {
            // No task context (early boot) - output directly to HDMI
            terminal_putc((char)c);
        }
    }
    /* Output to UART if enabled */
    if (get_output_target() & OUTPUT_UART) {
        // if (tid >= 0) {
        //     /* Task context - use output redirection */
        //     task_output_putc(tid, (char)c);
        // } else {
        //     /* No task context (early boot) - output directly to UART */
        //     bsp_uart_putc(0, (char)c);
        // }
        bsp_uart_putc(0, (char)c);
    }

    return c;
}
/*
 * Set output target (UART, HDMI, or both)
 */
void set_output_target(output_target_t target) {
    if (target >= OUTPUT_UART && target <= OUTPUT_BOTH) {
        output_target = target;
    }
}

/*
 * Get current output target
 */
output_target_t get_output_target(void) { return output_target; }
