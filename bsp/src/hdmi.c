#include <font_16x16.h>
#include <font_8x8.h>
#include <hdmi.h>
#include <stdio.h>

// 字体大小全局变量 (8 或 16)
static int current_font_size = 16; // 默认使用 16x16 字体

// 三缓冲，默认使用 Buffer SHELL 作为当前缓冲
static int current_display_buffer = BUFFER_S;
static int current_draw_buffer = BUFFER_S;

// HDMI 控制寄存器影子 (bit0=enable, bit1=src_sel)
static uint32_t hdmi_ctrl_shadow = 0;

// Framebuffer 指针 (指向当前绘制的后台buffer)
// 初始指向 Buffer A，与 HDMI 同步
volatile uint16_t *hdmi_fb_ptr = (volatile uint16_t *)HDMI_FB_BASE_A;

static void hdmi_write_ctrl(void) {
#ifndef QEMU_RUN
    volatile uint32_t *hdmi_enable_reg = (volatile uint32_t *)HDMI_ENABLE_REG;
    *hdmi_enable_reg = hdmi_ctrl_shadow;
#endif
}

void hdmi_init(void) {
    // 将绘制和显示都设置为 Buffer SHELL
    hdmi_fb_show_base_set(BUFFER_S);
    hdmi_fb_write_base_set(BUFFER_S);
    hdmi_set_source(HDMI_SOURCE_DDR);
    hdmi_enable(1);
}

void hdmi_fb_show_base_set(enum BUFFER buffer) {
#ifndef QEMU_RUN
    volatile uint32_t *hdmi_fb_base_reg = (volatile uint32_t *)HDMI_FB_ADDR_REG;
#endif
    if (buffer == BUFFER_A) {
#ifndef QEMU_RUN
        *hdmi_fb_base_reg = HDMI_FB_BASE_A;
#endif
        current_display_buffer = BUFFER_A;
    } else if( buffer == BUFFER_B) {
#ifndef QEMU_RUN
        *hdmi_fb_base_reg = HDMI_FB_BASE_B;
#endif
        current_display_buffer = BUFFER_B;
    } else {
#ifndef QEMU_RUN
        *hdmi_fb_base_reg = HDMI_FB_BASE_S;
#endif
        current_display_buffer = BUFFER_S;
    }
}

void hdmi_fb_write_base_set(enum BUFFER buffer) {
    if (buffer == BUFFER_A) {
        hdmi_fb_ptr = (volatile uint16_t *)HDMI_FB_BASE_A;
    } else if (buffer == BUFFER_B) {
        hdmi_fb_ptr = (volatile uint16_t *)HDMI_FB_BASE_B;
    } else {
        hdmi_fb_ptr = (volatile uint16_t *)HDMI_FB_BASE_S;
    }
}

void hdmi_set_show_addr(uint32_t offset){
#ifndef QEMU_RUN
    volatile uint32_t *hdmi_fb_addr_reg = (volatile uint32_t *)HDMI_FB_ADDR_REG;
    *hdmi_fb_addr_reg = offset;
#endif
    (void)offset;  // 避免 QEMU 模式下未使用警告
}

void hdmi_enable(int enable) {
    if (enable) {
        hdmi_ctrl_shadow |= HDMI_ENABLE_BIT;
    } else {
        hdmi_ctrl_shadow &= ~HDMI_ENABLE_BIT;
    }
    hdmi_write_ctrl();
    (void)enable;
}

void hdmi_set_source(int source) {
    if (source) {
        hdmi_ctrl_shadow |= HDMI_SRC_SEL_BIT;
    } else {
        hdmi_ctrl_shadow &= ~HDMI_SRC_SEL_BIT;
    }
    hdmi_write_ctrl();
}

int hdmi_get_source(void) {
    return (hdmi_ctrl_shadow & HDMI_SRC_SEL_BIT) ? 1 : 0;
}

void hdmi_clear(uint16_t color) {
    for (int i = 0; i < HDMI_WIDTH * HDMI_HEIGHT; i++) {
        hdmi_fb_ptr[i] = color;
    }
}

void hdmi_clear_line(uint32_t v_start, uint32_t v_end, uint16_t color){
    for (uint32_t y = v_start; y < v_end; y++) {
        for (uint32_t x = 0; x < HDMI_WIDTH; x++) {
            hdmi_fb_ptr[y * HDMI_WIDTH + x] = color;
        }
    }
}

// all the character drawing functions draw on buffer S directly
// we don't want to our game drawing functions to be affected by character drawing
void hdmi_draw_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < HDMI_WIDTH && y >= 0 && y < FRAMEBUF_S_NUM_ROWS) {
        // 不用切换，直接在 buffer S 上绘制
        volatile uint16_t *hdmi_fb_ptr_S = (volatile uint16_t *)HDMI_FB_BASE_S;
        hdmi_fb_ptr_S[y * HDMI_WIDTH + x] = color;
    }
}

void hdmi_draw_rect(int x, int y, int w, int h, uint16_t color) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            hdmi_draw_pixel(x + i, y + j, color);
        }
    }
}

void hdmi_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;

    // 使用整数运算的Bresenham算法
    int dx_abs = dx < 0 ? -dx : dx;
    int dy_abs = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;

    int x = x0;
    int y = y0;

    if (dx_abs > dy_abs) {
        int err = dx_abs / 2;
        while (x != x1) {
            hdmi_draw_pixel(x, y, color);
            err -= dy_abs;
            if (err < 0) {
                y += sy;
                err += dx_abs;
            }
            x += sx;
        }
    } else {
        int err = dy_abs / 2;
        while (y != y1) {
            hdmi_draw_pixel(x, y, color);
            err -= dx_abs;
            if (err < 0) {
                x += sx;
                err += dy_abs;
            }
            y += sy;
        }
    }
    hdmi_draw_pixel(x, y, color);
}

void hdmi_draw_char(int x, int y, char c, uint16_t fg_color,
                    uint16_t bg_color) {
    if (c < 32 || c > 126)
        c = ' ';

    if (current_font_size == 8) {
        // 使用 8x8 字体
        const uint8_t *glyph = font_8x8[c - 32];
        for (int row = 0; row < 8; row++) {
            uint8_t line = glyph[row];
            for (int col = 0; col < 8; col++) {
                uint16_t color =
                    (line & (1 << (7 - col))) ? fg_color : bg_color;
                hdmi_draw_pixel(x + col, y + row, color);
            }
        }
    } else if (current_font_size == 16) {
        // 使用 16x16 字体
        const uint16_t *glyph = font_16x16[c - 32];
        for (int row = 0; row < 16; row++) {
            uint16_t line = glyph[row];
            for (int col = 0; col < 16; col++) {
                uint16_t color =
                    (line & (1 << (15 - col))) ? fg_color : bg_color;
                hdmi_draw_pixel(x + col, y + row, color);
            }
        }
    } else {
        // 默认回退到 16x16 字体
        const uint16_t *glyph = font_16x16[c - 32];
        for (int row = 0; row < 16; row++) {
            uint16_t line = glyph[row];
            for (int col = 0; col < 16; col++) {
                uint16_t color =
                    (line & (1 << (15 - col))) ? fg_color : bg_color;
                hdmi_draw_pixel(x + col, y + row, color);
            }
        }
    }
}

void hdmi_draw_string(int x, int y, const char *str, uint16_t fg_color,
                      uint16_t bg_color) {
    int cur_x = x;
    int cur_y = y;

    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += current_font_size;
        } else {
            hdmi_draw_char(cur_x, cur_y, *str, fg_color, bg_color);
            cur_x += current_font_size;
            if (cur_x >= HDMI_WIDTH) {
                cur_x = x;
                cur_y += current_font_size;
            }
        }
        str++;
    }
}

void hdmi_printf(int x, int y, uint16_t fg_color, uint16_t bg_color,
                 const char *fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);

    // 使用mylib中的mysprintf
    sprintf(buffer, fmt, args);

    va_end(args);

    hdmi_draw_string(x, y, buffer, fg_color, bg_color);
}

void hdmi_draw_image(int x, int y, int w, int h, const uint16_t *image_data) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int screen_x = x + i;
            int screen_y = y + j;
            if (screen_x >= 0 && screen_x < HDMI_WIDTH && screen_y >= 0 &&
                screen_y < HDMI_HEIGHT) {
                hdmi_fb_ptr[screen_y * HDMI_WIDTH + screen_x] = image_data[j * w + i];
            }
        }
    }
}

//=============================================================================
// 双缓冲函数实现
//=============================================================================

// 等待垂直同步 (读取硬件V-Sync标志，等待变化)
void hdmi_wait_vsync(void) {
    // 临时禁用 vsync 等待以测试性能
    // TODO: 检查 HDMI 硬件的 vsync_flag 是否正常工作

    // volatile uint32_t *hdmi_status = (volatile uint32_t *)HDMI_STATUS_REG;
    // uint32_t last_vsync = *hdmi_status & 0x1;
    //
    // while ((*hdmi_status & 0x1) == last_vsync) {
    //     // 等待 vsync_flag 变化
    // }

    // 暂时不等待，直接返回
    return;
}

// 设置显示的 buffer (需要硬件支持动态切换，当前为软件模拟)
void hdmi_set_buffer(int buffer_index) {
    // 注意：当前硬件实现中，fb_base_addr 是固定的
    // 这个函数用于将来硬件支持动态切换时使用
    // 在软件实现中，我们通过复制内存来"切换"

    current_display_buffer = buffer_index & 1;
}

// 获取后台 buffer 指针
void *hdmi_get_back_buffer(void) {
    if (current_draw_buffer == 0) {
        return (void *)HDMI_FB_BASE_A;
    } else {
        return (void *)HDMI_FB_BASE_B;
    }
}

// 交换前后台 buffer（硬件零拷贝切换）
void hdmi_swap_buffers(void) {
    // 等待 V-Sync，避免画面撕裂
    hdmi_wait_vsync();

    // 硬件方案：直接切换 HDMI 控制器读取的 framebuffer 地址
    // 无需复制内存，瞬间完成！

    volatile uint32_t *hdmi_fb_addr_reg = (volatile uint32_t *)HDMI_FB_ADDR_REG;

    // int current_buffer = current_display_buffer;

    // 切换显示的 buffer
    if (current_display_buffer == 0) {
        // 当前显示 Buffer A，切换到 Buffer B
        *hdmi_fb_addr_reg = HDMI_FB_BASE_B;
        current_display_buffer = 1;
        current_draw_buffer = 0; // 下次绘制到 A
    } else {
        // 当前显示 Buffer B，切换到 Buffer A
        *hdmi_fb_addr_reg = HDMI_FB_BASE_A;
        current_display_buffer = 0;
        current_draw_buffer = 1; // 下次绘制到 B
    }

    // 更新绘制指针到后台 buffer
    hdmi_fb_ptr = (volatile uint16_t *)hdmi_get_back_buffer();

    // 注释掉 printf，避免 UART 输出拖慢帧率
    // printf("Display Buffer switched: 0x%x -> 0x%x\n", current_buffer,
    //        current_display_buffer);
}

// 设置字体大小 (8 或 16)
void hdmi_set_font_size(int size) {
    if (size == 8 || size == 16) {
        current_font_size = size;
    }
}

// 获取当前字体大小
int hdmi_get_font_size(void) { return current_font_size; }
