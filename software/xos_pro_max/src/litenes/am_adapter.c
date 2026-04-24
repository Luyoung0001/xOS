/*
 * am_adapter.c - AbstractMachine 适配层实现
 * 将 AM 接口映射到 xOS 的 HDMI 和 PS2 驱动
 */

#include <hdmi.h>
#include <litenes/am.h>
#include <ps2.h>
#include <stdio.h>
#include <timer.h>
#include <uart.h>

#ifdef QEMU_RUN
#include <qemu_fb.h>
static bool qemu_fb_ready = false;
#endif

// 初始化 IO 设备
void ioe_init(void) {
#ifdef QEMU_RUN
    // QEMU 模式：初始化 bochs-display
    if (qemu_fb_init() == 0) {
        qemu_fb_clear(0x00000000);  // 黑色
        qemu_fb_ready = true;
        printf("[AM] QEMU framebuffer initialized\n");
    } else {
        qemu_fb_ready = false;
        printf("[AM] QEMU framebuffer init failed\n");
    }
#else
    // 硬件模式：初始化 HDMI（双缓冲）
    hdmi_fb_write_base_set(BUFFER_B);
    hdmi_fb_show_base_set(BUFFER_B);
    hdmi_clear(0x0000); // 黑色

    hdmi_fb_write_base_set(BUFFER_A);
    hdmi_fb_show_base_set(BUFFER_A);
    hdmi_clear(0x0000); // 黑色
#endif
    printf("[AM] IO devices initialized\n");
}

// 定时器实现（使用真实硬件定时器）
AM_TIMER_UPTIME_T am_timer_uptime(void) {
    AM_TIMER_UPTIME_T ret;
    ret.us = timer_get_uptime_us();
    return ret;
}

// GPU 配置（返回 NES 原始分辨率）
AM_GPU_CONFIG_T am_gpu_config(void) {
    AM_GPU_CONFIG_T cfg;
#ifdef QEMU_RUN
    if (!qemu_fb_ready) {
        cfg.width = 640;
        cfg.height = 480;
    } else {
        qemu_fb_info_t *info = qemu_fb_get_info();
        cfg.width = info->width;
        cfg.height = info->height;
    }
#else
    cfg.width = 256;   // NES 原始宽度
    cfg.height = 240;  // NES 原始高度
#endif
    return cfg;
}

// GPU 绘制函数
void am_gpu_fbdraw(int x, int y, uint32_t *pixels, int w, int h, bool sync) {
#ifdef QEMU_RUN
    if (!qemu_fb_ready) {
        (void)x;
        (void)y;
        (void)pixels;
        (void)w;
        (void)h;
        (void)sync;
        return;
    }
    // QEMU 模式：直接写入 32 位 framebuffer
    qemu_fb_blit(x, y, pixels, w, h);
    (void)sync;  // QEMU 不需要同步
#else
    // 硬件模式：HDMI RGB565 输出
    volatile uint16_t *fb = hdmi_get_fb_pointer();

    // 边界检查
    if (x < 0 || y < 0 || x + w > 1920 || y + h > 1080) {
        return;
    }

    // 预计算基地址（y * 1920 使用移位）
    int y_offset = (y << 10) + (y << 9) + (y << 8) + (y << 7);
    volatile uint16_t *fb_row = fb + y_offset + x;
    uint32_t *src = pixels;

    // 预加载掩码到寄存器
    uint32_t mask_r = 0xF800;
    uint32_t mask_g = 0x07E0;
    uint32_t mask_b = 0x001F;

    // 使用内联汇编优化内循环
    for (int j = 0; j < h; j++) {
        uint32_t *src_ptr = src;
        volatile uint16_t *dst_ptr = fb_row;
        int count = w;

        // 内联汇编：直接从寄存器写入，避免栈操作
        __asm__ volatile("1:\n\t"
                         "ld.w    $t0, %3, 0\n\t"    // 加载 ARGB
                         "addi.w  %3, %3, 4\n\t"     // src++
                         "srli.w  $t1, $t0, 8\n\t"   // argb >> 8
                         "srli.w  $t2, $t0, 5\n\t"   // argb >> 5
                         "srli.w  $t3, $t0, 3\n\t"   // argb >> 3
                         "and     $t1, $t1, %6\n\t"  // R: & mask_r
                         "and     $t2, $t2, %7\n\t"  // G: & mask_g
                         "and     $t3, $t3, %8\n\t"  // B: & mask_b
                         "or      $t1, $t1, $t2\n\t" // R | G
                         "or      $t1, $t1, $t3\n\t" // R | G | B
                         "st.h    $t1, %4, 0\n\t"    // 直接写入16位
                         "addi.w  %4, %4, 2\n\t"     // dst++
                         "addi.w  %5, %5, -1\n\t"    // count--
                         "bne     $zero, %5, 1b\n\t" // 循环
                         : "=r"(src_ptr), "=r"(dst_ptr), "=r"(count)
                         : "0"(src_ptr), "1"(dst_ptr), "2"(count), "r"(mask_r),
                           "r"(mask_g), "r"(mask_b)
                         : "t0", "t1", "t2", "t3", "memory");

        src += w;
        fb_row += 1920;
    }

    if (sync) {
        hdmi_swap_buffers();
    }
#endif
}

// 键盘输入处理
#ifndef QEMU_RUN
// PS2 扫描码到 AM 键码的映射
static bool is_break_code = false;

static int ps2_to_am_keycode(uint8_t scancode) {
    switch (scancode) {
    case 0x1D:
        return AM_KEY_W; // W (上)
    case 0x1C:
        return AM_KEY_A; // A (左)
    case 0x1B:
        return AM_KEY_S; // S (下)
    case 0x23:
        return AM_KEY_D; // D (右)
    case 0x3B:
        return AM_KEY_J; // J (A 按钮)
    case 0x42:
        return AM_KEY_K; // K (B 按钮)
    case 0x3C:
        return AM_KEY_U; // U (Select)
    case 0x43:
        return AM_KEY_I; // I (Start)
    case 0x76:
        return AM_KEY_ESC; // ESC
    default:
        return -1;
    }
}
#else
static int qemu_ascii_to_am_keycode(int ch) {
    switch (ch) {
    case 'w':
    case 'W':
        return AM_KEY_W;
    case 'a':
    case 'A':
        return AM_KEY_A;
    case 's':
    case 'S':
        return AM_KEY_S;
    case 'd':
    case 'D':
        return AM_KEY_D;
    case 'j':
    case 'J':
        return AM_KEY_J;
    case 'k':
    case 'K':
        return AM_KEY_K;
    case 'u':
    case 'U':
        return AM_KEY_U;
    case 'i':
    case 'I':
        return AM_KEY_I;
    case 0x1b: // ESC
    case 'q':
    case 'Q':
        return AM_KEY_ESC;
    default:
        return AM_KEY_NONE;
    }
}
#endif

AM_INPUT_KEYBRD_T am_input_keybrd(void) {
    AM_INPUT_KEYBRD_T ev;
    ev.keydown = false;
    ev.keycode = AM_KEY_NONE;

#ifdef QEMU_RUN
    static int pending_release = AM_KEY_NONE;
    static int esc_seq_state = 0;

    if (pending_release != AM_KEY_NONE) {
        ev.keycode = pending_release;
        ev.keydown = false;
        pending_release = AM_KEY_NONE;
        return ev;
    }

    int ch = bsp_uart_getc_nonblock(0);
    if (ch < 0) {
        return ev;
    }

    if (esc_seq_state == 1) {
        esc_seq_state = 0;
        if (ch == '[') {
            esc_seq_state = 2;
            return ev;
        }
        ev.keycode = AM_KEY_ESC;
        ev.keydown = true;
        pending_release = AM_KEY_ESC;
        return ev;
    }
    if (esc_seq_state == 2) {
        esc_seq_state = 0;
        switch (ch) {
        case 'A':
            ev.keycode = AM_KEY_W;
            break;
        case 'B':
            ev.keycode = AM_KEY_S;
            break;
        case 'C':
            ev.keycode = AM_KEY_D;
            break;
        case 'D':
            ev.keycode = AM_KEY_A;
            break;
        default:
            ev.keycode = AM_KEY_NONE;
            break;
        }
        if (ev.keycode != AM_KEY_NONE) {
            ev.keydown = true;
            pending_release = ev.keycode;
        }
        return ev;
    }

    if (ch == 0x1b) {
        esc_seq_state = 1;
        return ev;
    }

    int keycode = qemu_ascii_to_am_keycode(ch);
    if (keycode == AM_KEY_NONE) {
        return ev;
    }
    ev.keycode = keycode;
    ev.keydown = true;
    pending_release = keycode;
    return ev;
#else
    // 使用 shell 的键盘缓冲区（Mario 独占运行时可以使用）
    extern int kb_get_scancode(void);
    int scancode = kb_get_scancode();

    if (scancode < 0) {
        // 没有新按键事件
        return ev;
    }

    // 检查是否是 break code 前缀
    if (scancode == 0xF0) {
        is_break_code = true;
        return ev;
    }

    // 转换为 AM 键码
    int keycode = ps2_to_am_keycode(scancode);
    if (keycode >= 0) {
        ev.keycode = keycode;
        ev.keydown = !is_break_code;
    }

    is_break_code = false;
    return ev;
#endif
}
