#ifndef HDMI_H
#define HDMI_H

#include <stdint.h>

// HDMI 显示参数
#define HDMI_WIDTH   1920
#define HDMI_HEIGHT  1080

enum BUFFER {
    BUFFER_A = 0,
    BUFFER_B = 1,
    BUFFER_S = 2
};

#ifdef QEMU_RUN
// QEMU 模式：无真实 HDMI，使用内存模拟
#define HDMI_FB_SIZE    0x00400000
#define HDMI_FB_BASE_A  0x0F000000
#define HDMI_FB_BASE_B  0x0F400000
#define HDMI_FB_BASE_S  0x0F800000
#define FRAMEBUF_END    0x0FCFF000
#define FRAMEBUF_S_END    0x0FCFF000
#define FRAMEBUF_S_NUM_ROWS  1360
#define FRAMEBUF_SIZE   0x01000000
#define HDMI_ENABLE_REG 0x0FD0F034   // 无效地址，QEMU 下不使用
#define HDMI_STATUS_REG 0x0FD0F060
#define HDMI_FB_ADDR_REG 0x0FD0F064
#else
// 真实硬件模式
#define HDMI_FB_SIZE    0x00400000  // 4MB per buffer (1920*1080*2)
#define HDMI_FB_BASE_A  0x1F000000  // Framebuffer A
#define HDMI_FB_BASE_B  0x1F400000  // Framebuffer B
#define HDMI_FB_BASE_S  0x1F800000  // Framebuffer for Shell
#define FRAMEBUF_END    0x1FCFF000  // End of framebuffer region
#define FRAMEBUF_S_END    0x1FCFF000
#define FRAMEBUF_S_NUM_ROWS  1360
#define FRAMEBUF_SIZE   0x01000000  // 16MB (双缓冲 1080P)
#define HDMI_ENABLE_REG 0x1FD0F034   // HDMI 使能寄存器
#define HDMI_STATUS_REG  0x1FD0F060  // HDMI 状态寄存器
#define HDMI_FB_ADDR_REG 0x1FD0F064  // HDMI Framebuffer 地址寄存器
#endif

// HDMI_ENABLE_REG bits
#define HDMI_ENABLE_BIT   (1u << 0)
#define HDMI_SRC_SEL_BIT  (1u << 1)  // 0=DDR framebuffer, 1=NES framebuffer
#define HDMI_SOURCE_DDR   0
#define HDMI_SOURCE_NES   1

// 兼容旧代码
#define HDMI_FB_BASE    HDMI_FB_BASE_A

// RGB565 颜色定义
#define RGB565(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

// 常用颜色
#define COLOR_BLACK   RGB565(0, 0, 0)
#define COLOR_WHITE   RGB565(255, 255, 255)
#define COLOR_RED     RGB565(255, 0, 0)
#define COLOR_GREEN   RGB565(0, 255, 0)
#define COLOR_BLUE    RGB565(0, 0, 255)
#define COLOR_YELLOW  RGB565(255, 255, 0)
#define COLOR_CYAN    RGB565(0, 255, 255)
#define COLOR_MAGENTA RGB565(255, 0, 255)
#define COLOR_GRAY    RGB565(128, 128, 128)

// HDMI 基础函数
void hdmi_init(void);
void hdmi_enable(int enable);
void hdmi_set_source(int source);
int hdmi_get_source(void);
void hdmi_fb_show_base_set(enum BUFFER buffer);
void hdmi_fb_write_base_set(enum BUFFER buffer);
void hdmi_clear(uint16_t color);
void hdmi_clear_line(uint32_t v_start, uint32_t v_end, uint16_t color);
void hdmi_draw_pixel(int x, int y, uint16_t color);
void hdmi_draw_rect(int x, int y, int w, int h, uint16_t color);
void hdmi_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
void hdmi_set_show_addr(uint32_t offset);

// 当前 framebuffer 指针（外部变量）
extern volatile uint16_t* hdmi_fb_ptr;

// 获取当前 framebuffer 指针
#define hdmi_get_fb_pointer() (hdmi_fb_ptr)

// 字符显示函数
void hdmi_draw_char(int x, int y, char c, uint16_t fg_color, uint16_t bg_color);
void hdmi_draw_string(int x, int y, const char* str, uint16_t fg_color, uint16_t bg_color);
void hdmi_printf(int x, int y, uint16_t fg_color, uint16_t bg_color, const char* fmt, ...);

// 字体设置函数
void hdmi_set_font_size(int size);  // 设置字体大小 (8 或 16)
int hdmi_get_font_size(void);       // 获取当前字体大小

// 图片显示函数
void hdmi_draw_image(int x, int y, int w, int h, const uint16_t* image_data);

// 双缓冲函数
void hdmi_set_buffer(int buffer_index);       // 切换显示的 buffer (0=A, 1=B)
void* hdmi_get_back_buffer(void);             // 获取后台 buffer 指针
void hdmi_wait_vsync(void);                   // 等待垂直同步（软件延时）
void hdmi_swap_buffers(void);                 // 切换前后台 buffer

#endif // HDMI_H
