/*------------------------------------------------------------------------------
 * xOS - Simple Operating System for LoongArch32R SoC
 *
 * Main entry point
 *----------------------------------------------------------------------------*/

#include <hdmi_terminal.h>
#include <heap.h>
#include <ps2.h>
#include <sched.h>
#include <shell.h>
#include <stdio.h>
#include <timer.h>
#include <trap.h>
#include <uart.h>
#include <uart_display.h>

#ifdef QEMU_RUN
#include <qemu_fb.h>
#endif
/*============================================================================
 * Keyboard Ring Buffer (多消费者模式)
 * PS2 中断只修改 head，每个任务维护自己的 tail
 *============================================================================*/
#define KB_BUF_SIZE 64
static volatile uint8_t kb_buffer[KB_BUF_SIZE];
static volatile int kb_head = 0;
static int kb_tail[MAX_TASKS] = {0}; // 每个任务一个 tail

/*============================================================================
 * PS2 Keyboard Interrupt Handler
 *============================================================================*/
void ps2_irq_handler(trap_frame_t *tf) {
    (void)tf;

    while (bsp_ps2_data_available()) {
        uint8_t scancode = (uint8_t)bsp_ps2_read();
        int next = (kb_head + 1) % KB_BUF_SIZE;
        // 只要 buffer 没满就写入（不检查任何 tail）
        if (next != kb_head) {
            kb_buffer[kb_head] = scancode;
            kb_head = next;
        }
    }
}

/*============================================================================
 * Get Scancode from Buffer (每个任务独立消费)
 *============================================================================*/
int kb_get_scancode(void) {
    int tid = get_current_task();
    if (tid < 0 || tid >= MAX_TASKS) {
        tid = 0;
    }

    if (kb_head == kb_tail[tid]) {
        return -1;
    }
    uint8_t code = kb_buffer[kb_tail[tid]];
    kb_tail[tid] = (kb_tail[tid] + 1) % KB_BUF_SIZE;
    return code;
}

/*============================================================================
 * Shell Task (runs as task 0)
 *============================================================================*/
void shell_task(void) {
    /* Initialize shell */
    shell_init();

    /* Run shell (infinite loop) */
    shell_run();

    /* Should never reach here */
    task_exit();
}

/*============================================================================
 * System Initialization
 *============================================================================*/
static void system_init(void) {

#if !defined(SIMULATION) && !defined(QEMU_RUN)
    /* Initialize HDMI terminal first (so we can see boot messages) */
    terminal_init();
#endif

    /* Initialize heap memory manager */
    heap_init();

    /* Initialize trap/interrupt system */
    trap_init();

    /* Initialize scheduler */
    sched_init();

#ifndef QEMU_RUN
    /* Register PS2 interrupt handler (not available in QEMU) */
    irq_register(IRQ_PS2, ps2_irq_handler);

    /* Initialize PS2 keyboard with interrupt enabled */
    bsp_ps2_init(1); /* 1 = enable interrupt */
#endif

    /* Initialize timer (50ms periodic) */
    timer_init();

    /* Enable global interrupts */
    irq_global_enable();

    printf("[INIT] System initialized\n");
#ifdef QEMU_RUN
    printf("[QEMU] Running in QEMU mode\n");
    // 测试 QEMU framebuffer 初始化
    printf("[QEMU] Testing framebuffer init...\n");
    if (qemu_fb_init() == 0) {
        printf("[QEMU] Framebuffer OK, drawing test pattern...\n");
        qemu_fb_clear(0x00FF0000);  // 红色
    }
#endif
}

/*============================================================================
 * Main Entry Point
 *============================================================================*/
int main(void) {
    /* Initialize UART for serial output */
    bsp_uart_init(0, BAUDRATE);

    /* Initialize all subsystems */
    system_init();

    /* Create shell task (task 0) */
    task_create(shell_task, "shell");

    // once we have created a task, then we can turn on the timer intr
    irq_enable(IRQ_TIMER);

    /* Start timer now that we're running */
    timer_start();

    // and we don't need to start our shell mannully, cause scheduller will do
    // it. but here appears a question, once scheduled the first task.
    //
    // and the mother task's context is lost forever.

    /* Should never reach here */
    while (1)
        ;

    return 0;
}
