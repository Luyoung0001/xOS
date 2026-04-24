#include <stdint.h>
#include <stdio.h>
#include <timer.h>
#include <trap.h>

// CPU 频率配置（请根据实际 FPGA 频率修改）
// 如果是 75MHz，设置为 75000000
// 如果是 50MHz，设置为 50000000
// 如果是 20MHz，设置为 20000000
#define CPU_FREQ_HZ 75000000ULL // 假设 75MHz

// 全局变量：记录定时器溢出次数
static volatile uint64_t timer_overflow_count = 0;
static uint32_t timer_init_val = 0;

/*============================================================================
 * Timer Initialization
 *============================================================================*/
void timer_init(void) {
    /*
     * Configure timer for 100ms periodic interrupt (减少中断频率)
     * System clock: 75MHz
     * 100ms = 0.1s
     * Clock cycles = 75,000,000 * 0.1 = 7,500,000
     *
     * TCFG format:
     *   TCFG[31:2] = InitVal (7,500,000 >> 2 = 1,875,000)
     *   TCFG[1] = Periodic (1)
     *   TCFG[0] = Enable (0) - NOT enabled yet
     */
    timer_init_val = 1875000; /* 100ms @ 75MHz: 7,500,000 / 4 */
    uint32_t tcfg = (timer_init_val << TCFG_INITVAL_SHIFT) | TCFG_PERIODIC;

    /* Reset overflow counter */
    timer_overflow_count = 0;

    /* Write TCFG */
    csr_write(CSR_TCFG, tcfg);

    /* Register timer interrupt handler */
    irq_register(IRQ_TIMER, timer_irq_handler);

    printf("[TIMER] Configured: 10ms periodic @ 75MHz (not started yet)\n");
}

void timer_start(void) {
    /* Read current TCFG */
    uint32_t tcfg = csr_read(CSR_TCFG);

    /* Set enable bit */
    tcfg |= TCFG_EN;

    /* Write back to start timer */
    csr_write(CSR_TCFG, tcfg);

    printf("[TIMER] Started\n");
}

/*============================================================================
 * Get Real-Time Uptime
 *============================================================================*/

// 定时器中断回调 - 在 trap.c 的 timer_irq_handler 中调用
void timer_overflow_callback(void) { timer_overflow_count++; }

uint64_t timer_get_uptime_us(void) {
    // 读取 CSR_TVAL，不依赖中断
    uint32_t tval = csr_read(CSR_TVAL);

    // 计算已经过的周期数
    uint32_t elapsed_cycles;
    if (tval <= timer_init_val) {
        elapsed_cycles = (timer_init_val - tval) << 2;
    } else {
        elapsed_cycles = 0;
    }

    // 简化：直接用移位代替除法
    // 75MHz: 1us = 75 cycles
    // (x >> 6) - (x >> 9)：
    //   - 1/64 - 1/512 = 0.015625 - 0.00195 = 0.01367
    //   - 误差：(0.01367 - 0.01333) / 0.01333 = 2.5%
    uint32_t current_us = (elapsed_cycles >> 6) - (elapsed_cycles >> 9);

    // 加上溢出次数（如果中断被禁用，这个值不会增加）
    uint64_t overflow_us = timer_overflow_count * 100000ULL;

    return overflow_us + current_us;
}
