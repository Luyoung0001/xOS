#include <uart.h>

#define UART_REG(offset) (*(volatile uint8_t *)(UART_BASE + (offset)))
int bsp_uart_init(uint8_t uart_id, uint32_t baudrate) {
    uint16_t divisor;

    /* 计算波特率除数: divisor = CLK / (16 * baudrate) */
    divisor = UART_CLK / (16 * baudrate);

    /* 1. 使能并复位FIFO */
    UART_REG(UART_FCR) = FCR_FIFO_EN | FCR_RXSR | FCR_TXSR; /* 0x07 */

    /* 2. 设置DLAB=1，准备写入除数 */
    UART_REG(UART_LCR) = LCR_DLAB; /* 0x80 */

    /* 3. 写入除数 */
    UART_REG(UART_DLM) = (divisor >> 8) & 0xFF; /* 高字节 */
    UART_REG(UART_DLL) = divisor & 0xFF;        /* 低字节 */

    /* 4. 设置数据格式 8N1，同时清除DLAB */
    UART_REG(UART_LCR) = LCR_8N1; /* 0x03 */

    /* 5. 关闭Modem控制 */
    UART_REG(UART_MCR) = 0x00;

    return 0;
}

void bsp_uart_putc(uint8_t uart_id, char ch) {
    #ifndef SIMULATION
        /* 等待发送缓冲区空 (真实硬件需要等待) */
        while (!(UART_REG(UART_LSR) & LSR_THRE))
            ;
    #endif

    /* 自动将 \n 转换为 \r\n，确保终端正确换行 */
    if (ch == '\n') {
        UART_REG(UART_THR) = '\r';  // 先发送回车
        #ifndef SIMULATION
            while (!(UART_REG(UART_LSR) & LSR_THRE))
                ;
        #endif
    }

    UART_REG(UART_THR) = ch;
}

/* 非阻塞读取，返回 -1 表示无数据 */
int bsp_uart_getc_nonblock(uint8_t uart_id) {
    (void)uart_id;
    if (UART_REG(UART_LSR) & LSR_DR) {
        return UART_REG(UART_RBR);
    }
    return -1;
}

char bsp_uart_getc(uint8_t uart_id) {
    /* 等待数据就绪 */
    while (!(UART_REG(UART_LSR) & LSR_DR))
        ;
    return UART_REG(UART_RBR);
}
void bsp_uart_puts(uint8_t uart_id, const char *str) {
    while (*str) {
        bsp_uart_putc(uart_id, *str++);
    }
}