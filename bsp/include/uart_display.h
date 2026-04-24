/*------------------------------------------------------------------------------
 * UART LCD Display Driver (1602/2004 Character LCD)
 *
 * Board Support Package (BSP)
 *
 * 用于控制基于 UART 的字符液晶屏（如 1602、2004 等）
 *----------------------------------------------------------------------------*/

#ifndef __BSP_UART_DISPLAY_H__
#define __BSP_UART_DISPLAY_H__

#include <stdint.h>

/*============================================================================
 * Configuration
 *============================================================================*/
#define UART_DISPLAY_ID     0       /* 使用 UART0 连接串口屏 */
#define UART_DISPLAY_COLS   20      /* 液晶屏列数 */
#define UART_DISPLAY_ROWS   2       /* 液晶屏行数 */

/*============================================================================
 * Command Codes (from datasheet)
 *============================================================================*/
#define LCD_CMD_TEST        0x11    /* 测试指令 */
#define LCD_CMD_SHOW_CHAR   0x40    /* 显示字符 */
#define LCD_CMD_DISPLAY_ON  0x41    /* 打开显示 */
#define LCD_CMD_DISPLAY_OFF 0x42    /* 关闭显示 */
#define LCD_CMD_SET_CURSOR  0x45    /* 设置光标位置 */
#define LCD_CMD_CURSOR_RST  0x46    /* 光标复位 */
#define LCD_CMD_CURSOR_ON   0x47    /* 光标打开 */
#define LCD_CMD_CURSOR_OFF  0x48    /* 光标关闭 */
#define LCD_CMD_BACKSPACE   0x4E    /* 退格 */
#define LCD_CMD_CLEAR       0x51    /* 清屏 */
#define LCD_CMD_CONTRAST    0x52    /* 对比度 */
#define LCD_CMD_BACKLIGHT   0x53    /* 背光亮度 */
#define LCD_CMD_SET_BAUDRATE 0x61   /* 设置波特率 */

/*============================================================================
 * Baudrate Parameters (for LCD_CMD_SET_BAUDRATE)
 *============================================================================*/
#define LCD_BAUD_1200       0x01    /* 1200 bps */
#define LCD_BAUD_2400       0x02    /* 2400 bps */
#define LCD_BAUD_4800       0x03    /* 4800 bps */
#define LCD_BAUD_9600       0x04    /* 9600 bps (default) */
#define LCD_BAUD_19200      0x05    /* 19200 bps */
#define LCD_BAUD_38400      0x06    /* 38400 bps */
#define LCD_BAUD_57600      0x07    /* 57600 bps */
#define LCD_BAUD_115200     0x08    /* 115200 bps */

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * 初始化串口屏
 * @param baudrate: 波特率（默认 9600）
 * @return 0 on success
 */
int uart_display_init(uint8_t uart_id, uint32_t baudrate);

/**
 * 在指定位置显示字符串
 * @param str: 要显示的字符串
 * @param col: 列位置 (0 ~ UART_DISPLAY_COLS-1)
 * @param row: 行位置 (0 ~ UART_DISPLAY_ROWS-1)
 * @return 0 on success, -1 on error
 */
int uart_display_text(const char *str, uint8_t col, uint8_t row);

/**
 * 清屏
 * @param row: 0=全部清屏, 1=第1行, 2=第2行, ...
 * @return 0 on success
 */
int uart_display_clear(uint8_t row);

/**
 * 打开显示
 */
void uart_display_on(void);

/**
 * 关闭显示
 */
void uart_display_off(void);

/**
 * 设置光标位置
 * @param col: 列位置
 * @param row: 行位置
 */
void uart_display_set_cursor(uint8_t col, uint8_t row);

/**
 * 光标控制
 * @param enable: 1=打开光标, 0=关闭光标
 */
void uart_display_cursor(int enable);

/**
 * 设置背光亮度
 * @param level: 亮度等级 (1~8, 默认4)
 */
void uart_display_backlight(uint8_t level);

/**
 * 设置对比度
 * @param level: 对比度等级 (1~8, 默认3)
 */
void uart_display_contrast(uint8_t level);

/**
 * 设置串口屏波特率
 * @param baud_param: 波特率参数 (LCD_BAUD_9600 等)
 *
 * 注意：设置后串口屏会切换波特率，需要重新初始化 UART
 * 建议在 uart_display_init() 开始时调用以确保波特率匹配
 */
void uart_display_set_baudrate(uint8_t baud_param);

/**
 * 格式化输出到串口屏（类似 printf）
 * @param col: 列位置
 * @param row: 行位置
 * @param fmt: 格式字符串
 * @return 格式化后的字符数
 */
int uart_display_printf(uint8_t col, uint8_t row, const char *fmt, ...);

#endif /* __BSP_UART_DISPLAY_H__ */
