/*------------------------------------------------------------------------------
 * xOS timer.h
 *
 * LoongArch32R Timer Handling
 *----------------------------------------------------------------------------*/

#ifndef __TIMER_H__
#define __TIMER_H__

#include <stdint.h>

/**
 * Initialize timer (50ms periodic, but not started)
 */
void timer_init(void);

/**
 * Start the timer
 * Call this after creating tasks
 */
void timer_start(void);

/**
 * Get current uptime in microseconds
 * Based on hardware CSR_TVAL register
 */
uint64_t timer_get_uptime_us(void);


#endif /* __TIMER_H__ */

