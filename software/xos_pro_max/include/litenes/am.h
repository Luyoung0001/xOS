/*
 * AbstractMachine Adapter for xOS
 * 将 AM 接口适配到 xOS 的 HDMI 和 PS2 驱动
 */

#ifndef AM_H
#define AM_H

#include <stdint.h>
#include <stdbool.h>

// 键码定义（对应 PS2 扫描码）
#define AM_KEY_NONE  0
#define AM_KEY_W     0x1D
#define AM_KEY_A     0x1C
#define AM_KEY_S     0x1B
#define AM_KEY_D     0x23
#define AM_KEY_J     0x3B
#define AM_KEY_K     0x42
#define AM_KEY_U     0x3C
#define AM_KEY_I     0x43

// NES 手柄映射（兼容旧代码中的 AM_KEY_KEY_* 命名）
#define AM_KEY_KEY_A      AM_KEY_J
#define AM_KEY_KEY_B      AM_KEY_K
#define AM_KEY_KEY_SELECT AM_KEY_U
#define AM_KEY_KEY_START  AM_KEY_I
#define AM_KEY_KEY_UP     AM_KEY_W
#define AM_KEY_KEY_DOWN   AM_KEY_S
#define AM_KEY_KEY_LEFT   AM_KEY_A
#define AM_KEY_KEY_RIGHT  AM_KEY_D

// 宏工具
#define TOSTRING(x) #x
#define CONCAT(x, y) x##y

// AM 类型定义
typedef struct {
    int width;
    int height;
} AM_GPU_CONFIG_T;

typedef struct {
    uint32_t us;  // 微秒（改为 32 位避免 64 位除法）
} AM_TIMER_UPTIME_T;

typedef struct {
    bool keydown;
    int keycode;
} AM_INPUT_KEYBRD_T;

// AM IO 设备枚举
#define AM_TIMER_UPTIME  1
#define AM_GPU_CONFIG    2
#define AM_GPU_FBDRAW    3
#define AM_INPUT_KEYBRD  4

// AM 接口函数
void ioe_init(void);
AM_TIMER_UPTIME_T am_timer_uptime(void);
AM_GPU_CONFIG_T am_gpu_config(void);
AM_INPUT_KEYBRD_T am_input_keybrd(void);
void am_gpu_fbdraw(int x, int y, uint32_t *pixels, int w, int h, bool sync);

// 宏定义（兼容 AM 代码）
#define io_read(dev) _io_read_##dev()
#define io_write(dev, ...) _io_write_##dev(__VA_ARGS__)

#define _io_read_AM_TIMER_UPTIME() am_timer_uptime()
#define _io_read_AM_GPU_CONFIG() am_gpu_config()
#define _io_read_AM_INPUT_KEYBRD() am_input_keybrd()
#define _io_write_AM_GPU_FBDRAW(x, y, p, w, h, s) am_gpu_fbdraw(x, y, p, w, h, s)

#endif // AM_H
