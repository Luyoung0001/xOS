/*------------------------------------------------------------------------------
 * PS2 Keyboard Driver for LoongArch32R SoC
 *
 * Board Support Package (BSP)
 *----------------------------------------------------------------------------*/

#ifndef __BSP_PS2_H__
#define __BSP_PS2_H__

#include <stdint.h>

/*============================================================================
 * Register Addresses
 *============================================================================*/
#ifdef QEMU_RUN
// QEMU 模式：PS2 不可用，使用虚拟地址（不会被访问）
#define PS2_BASE        0x0FD0F050
#define PS2_DATA_REG    (*(volatile uint32_t *)0x0FD0F050)
#define PS2_STATUS_REG  (*(volatile uint32_t *)0x0FD0F054)
#define PS2_CTRL_REG    (*(volatile uint32_t *)0x0FD0F058)
#else
// 真实硬件模式
#define PS2_BASE        0x1FD0F050
#define PS2_DATA_REG    (*(volatile uint32_t *)0x1FD0F050)
#define PS2_STATUS_REG  (*(volatile uint32_t *)0x1FD0F054)
#define PS2_CTRL_REG    (*(volatile uint32_t *)0x1FD0F058)
#endif

#define PS2_DATA        (PS2_BASE + 0x00)
#define PS2_STATUS      (PS2_BASE + 0x04)
#define PS2_CTRL        (PS2_BASE + 0x08)

/*============================================================================
 * Status Register Bits (PS2_STATUS)
 *============================================================================*/
#define PS2_STATUS_VALID       (1 << 0)  /* New data available */
#define PS2_STATUS_PARITY_ERR  (1 << 1)  /* Parity error */
#define PS2_STATUS_FRAME_ERR   (1 << 2)  /* Frame error */
#define PS2_STATUS_OVERFLOW    (1 << 3)  /* Buffer overflow */

/*============================================================================
 * Control Register Bits (PS2_CTRL)
 *============================================================================*/
#define PS2_CTRL_CLEAR_ERR     (1 << 0)  /* Clear error flags (auto-clear) */
#define PS2_CTRL_ENABLE        (1 << 1)  /* PS2 receiver enable */
#define PS2_CTRL_INT_ENABLE    (1 << 2)  /* Interrupt enable */

/*============================================================================
 * Special Scancodes
 *============================================================================*/
#define PS2_BREAK_CODE         0xF0      /* Key release prefix */
#define PS2_EXTENDED_CODE      0xE0      /* Extended key prefix */

/*============================================================================
 * Interrupt
 *============================================================================*/
#define PS2_INT_BIT            2         /* CPU interrupt bit 2 */

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * Initialize PS2 keyboard controller
 * @param enable_int: 1 = enable interrupt, 0 = polling mode
 * @return 0 on success
 */
int bsp_ps2_init(int enable_int);

/**
 * Check if data is available
 * @return 1 if data available, 0 otherwise
 */
int bsp_ps2_data_available(void);

/**
 * Read scancode (non-blocking)
 * @return scancode (0-255) or -1 if no data
 */
int bsp_ps2_read(void);

/**
 * Read scancode (blocking)
 * @return scancode (0-255)
 */
uint8_t bsp_ps2_read_blocking(void);

/**
 * Get and clear error status
 * @return error flags (PS2_STATUS_* bits)
 */
uint32_t bsp_ps2_get_errors(void);

/**
 * Clear error flags
 */
void bsp_ps2_clear_errors(void);

/**
 * Enable/disable PS2 controller
 * @param enable: 1 = enable, 0 = disable
 */
void bsp_ps2_enable(int enable);

/**
 * Enable/disable interrupt
 * @param enable: 1 = enable, 0 = disable
 */
void bsp_ps2_int_enable(int enable);

/**
 * Convert scancode to ASCII (US layout)
 * @param scancode: PS2 scancode
 * @return ASCII character or 0 if not printable
 */
char bsp_ps2_to_ascii(uint8_t scancode);

/*============================================================================
 * Inline Implementations (for performance-critical code)
 *============================================================================*/

static inline int bsp_ps2_data_available_inline(void) {
    return (PS2_STATUS_REG & PS2_STATUS_VALID) != 0;
}

static inline uint8_t bsp_ps2_read_inline(void) {
    return (uint8_t)PS2_DATA_REG;
}

#endif /* __BSP_PS2_H__ */
