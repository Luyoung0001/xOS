#include <stdio.h>
#include <stdint.h>

/*
 * putchar - 输出单个字符
 *
 * 这是一个弱符号实现。用户可以在自己的代码中提供同名的强符号函数来覆盖它。
 *
 * 默认实现：调用 BSP 的 UART 输出函数
 * 用户可以覆盖此函数来实现自己的输出设备（LCD、网络等）
 */
__attribute__((weak)) int putchar(int c) {
    /* 声明外部 BSP 函数 */
    extern void bsp_uart_putc(uint8_t uart_id, char ch);

    /* 默认使用 UART0 输出 */
    #define DEFAULT_UART_ID 0

    bsp_uart_putc(DEFAULT_UART_ID, (char)c);
    return (unsigned char)c;
}
