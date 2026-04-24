#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include <stdint.h>

#define BAUDRATE 9600
// #define BAUDRATE 115200

#ifdef QEMU_RUN
#define UART_CLK 100000000      /* QEMU virt 机器时钟 */
#else
#define UART_CLK 62500000       /* 真实硬件时钟 */
#endif

#define UART_BASE 0x1FE001E0    /* QEMU 和真实硬件地址相同 */

/* UART 16550 寄存器偏移 */
#define UART_RBR 0x00 /* 接收缓冲 (读) */
#define UART_THR 0x00 /* 发送保持 (写) */
#define UART_DLL 0x00 /* 除数锁存低字节 (DLAB=1) */
#define UART_DLM 0x01 /* 除数锁存高字节 (DLAB=1) */
#define UART_IER 0x01 /* 中断使能 */
#define UART_FCR 0x02 /* FIFO控制 (写) */
#define UART_LCR 0x03 /* 线控制 */
#define UART_MCR 0x04 /* Modem控制 */
#define UART_LSR 0x05 /* 线状态 */

/* FCR 位定义 */
#define FCR_FIFO_EN 0x01 /* FIFO使能 */
#define FCR_RXSR 0x02    /* 接收FIFO复位 */
#define FCR_TXSR 0x04    /* 发送FIFO复位 */

/* LCR 位定义 */
#define LCR_DLAB 0x80 /* 除数锁存访问位 */
#define LCR_8N1 0x03  /* 8数据位, 无校验, 1停止位 */

/* LSR 位定义 */
#define LSR_DR 0x01   /* 数据就绪 */
#define LSR_THRE 0x20 /* 发送保持寄存器空 */

int bsp_uart_init(uint8_t uart_id, uint32_t baudrate);
void bsp_uart_putc(uint8_t uart_id, char ch);
char bsp_uart_getc(uint8_t uart_id);
int bsp_uart_getc_nonblock(uint8_t uart_id);
void bsp_uart_puts(uint8_t uart_id, const char *str);

#endif