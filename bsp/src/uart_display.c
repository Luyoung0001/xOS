/*------------------------------------------------------------------------------
 * UART LCD Display Driver Implementation
 *
 * Board Support Package (BSP)
 *----------------------------------------------------------------------------*/

#include <stdarg.h> /* GCC builtin: va_list, va_start, va_end */
#include <stdio.h>
#include <uart.h>
#include <uart_display.h>

/*============================================================================
 * Private Helper Functions
 *============================================================================*/

/**
 * 发送命令帧头（协议固定格式）
 * 格式: 00 00 00 [cmd] [param1] [param2] ... 0A 0D
 */
static void lcd_send_header(void) {
    bsp_uart_putc(UART_DISPLAY_ID, 0x00);
    bsp_uart_putc(UART_DISPLAY_ID, 0x00);
    bsp_uart_putc(UART_DISPLAY_ID, 0x00);
}

/**
 * 发送命令帧尾（协议固定格式）
 */
static void lcd_send_footer(void) {
    bsp_uart_putc(UART_DISPLAY_ID, 0x0A);
    bsp_uart_putc(UART_DISPLAY_ID, 0x0D);
}

/**
 * 发送简单命令（无参数）
 * @param cmd: 命令码
 */
static void lcd_send_simple_cmd(uint8_t cmd) {
    lcd_send_header();
    bsp_uart_putc(UART_DISPLAY_ID, cmd);
    lcd_send_footer();
}

/**
 * 发送单参数命令
 * @param cmd: 命令码
 * @param param: 参数
 */
static void lcd_send_cmd_with_param(uint8_t cmd, uint8_t param) {
    lcd_send_header();
    bsp_uart_putc(UART_DISPLAY_ID, cmd);
    bsp_uart_putc(UART_DISPLAY_ID, param);
    lcd_send_footer();
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

int uart_display_init(uint8_t uart_id, uint32_t baudrate) {
    /* 初始化 UART（使用 UART1 连接串口屏）*/
    bsp_uart_init(uart_id, baudrate);

    /* 等待串口屏启动（建议延时 100ms）*/
    for (volatile int i = 0; i < 1000000; i++)
        ;

    /* 设置串口屏波特率为 9600（确保匹配）*/
    uart_display_set_baudrate(LCD_BAUD_9600);

    /* 清屏并打开显示 */
    uart_display_clear(0);
    uart_display_on();

    /* 调整对比度和亮度 */
    uart_display_contrast(8);
    uart_display_backlight(8);

    return 0;
}

int uart_display_text(const char *str, uint8_t col, uint8_t row) {
    if (!str || col >= UART_DISPLAY_COLS || row >= UART_DISPLAY_ROWS) {
        return -1;
    }

    /* 发送显示字符命令
     * 格式: 00 00 00 40 [col] [row] [str...] 0A 0D
     */
    lcd_send_header();
    bsp_uart_putc(UART_DISPLAY_ID, LCD_CMD_SHOW_CHAR);
    bsp_uart_putc(UART_DISPLAY_ID, col);
    bsp_uart_putc(UART_DISPLAY_ID, row);

    /* 发送字符串内容 */
    while (*str) {
        bsp_uart_putc(UART_DISPLAY_ID, (uint8_t)*str);
        str++;
    }

    lcd_send_footer();
    return 0;
}

int uart_display_clear(uint8_t row) {
    if (row > UART_DISPLAY_ROWS) {
        return -1;
    }

    lcd_send_cmd_with_param(LCD_CMD_CLEAR, row);
    return 0;
}

void uart_display_on(void) { lcd_send_simple_cmd(LCD_CMD_DISPLAY_ON); }

void uart_display_off(void) { lcd_send_simple_cmd(LCD_CMD_DISPLAY_OFF); }

void uart_display_set_cursor(uint8_t col, uint8_t row) {
    if (col >= UART_DISPLAY_COLS || row >= UART_DISPLAY_ROWS) {
        return;
    }

    lcd_send_header();
    bsp_uart_putc(UART_DISPLAY_ID, LCD_CMD_SET_CURSOR);
    bsp_uart_putc(UART_DISPLAY_ID, col);
    bsp_uart_putc(UART_DISPLAY_ID, row);
    lcd_send_footer();
}

void uart_display_cursor(int enable) {
    if (enable) {
        lcd_send_simple_cmd(LCD_CMD_CURSOR_ON);
    } else {
        lcd_send_simple_cmd(LCD_CMD_CURSOR_OFF);
    }
}

void uart_display_backlight(uint8_t level) {
    /* 限制范围 1-8 */
    if (level < 1)
        level = 1;
    if (level > 8)
        level = 8;

    lcd_send_cmd_with_param(LCD_CMD_BACKLIGHT, level);
}

void uart_display_contrast(uint8_t level) {
    /* 限制范围 1-8 */
    if (level < 1)
        level = 1;
    if (level > 8)
        level = 8;

    lcd_send_cmd_with_param(LCD_CMD_CONTRAST, level);
}

void uart_display_set_baudrate(uint8_t baud_param) {
    /* 限制范围 1-8 */
    if (baud_param < 1)
        baud_param = 1;
    if (baud_param > 8)
        baud_param = 8;

    /* 发送波特率设置命令 */
    lcd_send_cmd_with_param(LCD_CMD_SET_BAUDRATE, baud_param);

    /* 等待串口屏切换波特率（需要一定延时）*/
    for (volatile int i = 0; i < 200000; i++)
        ;
}

int uart_display_printf(uint8_t col, uint8_t row, const char *fmt, ...) {
    char uart_display_buffer[UART_DISPLAY_COLS * UART_DISPLAY_ROWS + 1] = {0};
    va_list args;
    int ret;

    /* 格式化字符串到缓冲区（最多 40 字符）*/
    va_start(args, fmt);
    ret =
        vsnprintf(uart_display_buffer, sizeof(uart_display_buffer), fmt, args);
    va_end(args);

    /* 如果字符串长度 > 20，需要分两行显示 */
    if (ret > UART_DISPLAY_COLS) {
        /* 第一行：显示前 20 个字符 */
        char line1[UART_DISPLAY_COLS + 1] = {0};
        for (int i = 0; i < UART_DISPLAY_COLS; i++) {
            line1[i] = uart_display_buffer[i];
        }
        uart_display_text(line1, col, row);

        /* 第二行：显示后 20 个字符（如果还有空间）*/
        if (row + 1 < UART_DISPLAY_ROWS) {
            char line2[UART_DISPLAY_COLS + 1] = {0};
            for (int i = 0; i < UART_DISPLAY_COLS &&
                            uart_display_buffer[UART_DISPLAY_COLS + i];
                 i++) {
                line2[i] = uart_display_buffer[UART_DISPLAY_COLS + i];
            }
            uart_display_text(line2, col, row + 1);
        }
    } else {
        /* 字符串长度 <= 20，直接显示一行 */
        uart_display_text(uart_display_buffer, col, row);
    }

    return ret;
}
