## xOS_pro_max

## 简介
使用了 bsp 提供的 hdmi、uart 等功能，位于：bsp/

使用了 mylibc 提供的库，比如 staio.h，位于：mylibc/，要关注 putchar.c：

```C
#include <stdio.h>
#include <stdint.h>
__attribute__((weak)) int putchar(int c) {
    /* 声明外部 BSP 函数 */
    extern void bsp_uart_putc(uint8_t uart_id, char ch);

    /* 默认使用 UART0 输出 */
    #define DEFAULT_UART_ID 0

    bsp_uart_putc(DEFAULT_UART_ID, (char)c);
    return (unsigned char)c;
}

```

因此，在 xos_pro 中调用 printf 默认输出到 uart。

## 从 uart 到 hdmi

### 目前的方案
#### 第一步：在 output.c 中添加 HDMI 输出支持：
  - 添加 HDMI 光标管理
  - 实现 hdmi_putchar() 函数处理特殊字符
  - 实现滚屏逻辑

#### 第二步：添加输出目标控制
  - 默认输出到 HDMI（或两者）
  - 添加 shell 命令切换输出目标（如 output uart|hdmi|both）

#### 第三步：优化 HDMI 显示
  - 使用 16x16 PingFang SC 字体
  - 添加颜色支持（不同类型的消息用不同颜色）
  - 可选：添加状态栏显示系统信息

### 需要解决的技术细节
#### HDMI 光标管理
```C
  // 需要维护的状态
  static int hdmi_cursor_x = 0;  // 字符列（0-119，假设 1920/16=120）
  static int hdmi_cursor_y = 0;  // 字符行（0-67，假设 1080/16=67.5）
  static int hdmi_max_cols = 120;
  static int hdmi_max_rows = 67;
```

#### 滚屏实现
当光标到达屏幕底部时，需要：
  - 将整个屏幕内容向上移动一行
  - 清空最后一行
  - 或者使用环形缓冲区（更高效）

#### 特殊字符处理

- \n - 换行（光标移到下一行开头）
- \r - 回车（光标移到当前行开头）
- \b - 退格（删除前一个字符）
- \t - 制表符（移动到下一个 tab 位置）

#### HDMI 初始化时机

需要在 main.c 的 system_init() 中添加：
```C
  hdmi_init();
  hdmi_set_font_size(16);
```




