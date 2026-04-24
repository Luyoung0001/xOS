/*------------------------------------------------------------------------------
 * UART Display Example
 *
 * 演示如何使用 uart_display API 控制串口液晶屏
 *----------------------------------------------------------------------------*/

#include <uart_display.h>

int main(void) {
    /* 1. 初始化串口屏（9600 波特率）*/
    uart_display_init(0, 9600);

    /* 2. 显示 "hello, world" 在第1行第1列 */
    uart_display_text("hello, world", 0, 0);

    /* 3. 显示第二行 */
    uart_display_text("UART LCD Demo", 0, 1);

    /* 4. 格式化输出示例 */
    int counter = 0;
    while (1) {
        /* 使用 uart_display_printf 直接格式化输出 */
        uart_display_printf(0, 2, "Count: %d", counter);
        counter++;

        /* 延时 */
        for (volatile int i = 0; i < 1000000; i++);

        /* 每10次清屏一次 */
        if (counter % 10 == 0) {
            uart_display_clear(0);  /* 全屏清除 */
            uart_display_text("Screen cleared!", 0, 0);
        }
    }

    return 0;
}

/*============================================================================
 * 更多示例
 *============================================================================*/

/* 示例1：简单文本显示 */
void example_simple_text(void) {
    uart_display_init(0, 9600);
    uart_display_text("Hello!", 0, 0);
}

/* 示例2：多行显示 */
void example_multiline(void) {
    uart_display_init(0, 9600);
    uart_display_text("Line 1: CPU", 0, 0);
    uart_display_text("Line 2: OK", 0, 1);
}

/* 示例3：光标控制 */
void example_cursor(void) {
    uart_display_init(0, 9600);
    uart_display_text("Enter text:", 0, 0);
    uart_display_set_cursor(0, 1);  /* 设置光标到第2行 */
    uart_display_cursor(1);         /* 打开光标 */
}

/* 示例4：格式化输出 */
void example_printf(void) {
    uart_display_init(0, 9600);

    int temperature = 45;
    int voltage = 33;  /* 3.3V * 10 */

    /* 使用 uart_display_printf 直接格式化输出 */
    uart_display_printf(0, 0, "Temp: %dC", temperature);
    uart_display_printf(0, 1, "Volt: %d.%dV", voltage / 10, voltage % 10);
}

/* 示例5：调整背光和对比度 */
void example_brightness(void) {
    uart_display_init(0, 9600);
    uart_display_backlight(8);  /* 最大亮度 */
    uart_display_contrast(5);   /* 中等对比度 */
    uart_display_text("Bright screen!", 0, 0);
}

/* 示例6：清屏操作 */
void example_clear(void) {
    uart_display_init(0, 9600);

    uart_display_text("Row 1", 0, 0);
    uart_display_text("Row 2", 0, 1);

    /* 延时 */
    for (volatile int i = 0; i < 5000000; i++);

    uart_display_clear(1);  /* 只清除第1行 */

    /* 延时 */
    for (volatile int i = 0; i < 5000000; i++);

    uart_display_clear(0);  /* 清除全屏 */
}
