#ifndef TETRIS_H
#define TETRIS_H

#include <stdint.h>

/*============================================================================
 * Tetris Game Configuration
 *============================================================================*/
#define TETRIS_GRID_W       10      // 游戏区域宽度（方格数）
#define TETRIS_GRID_H       20      // 游戏区域高度（方格数）
#define TETRIS_BLOCK_SIZE   32      // 每个方格的像素大小
#define TETRIS_PREVIEW_SIZE 12      // 预览区方格大小

// 游戏区域在屏幕上的位置
#define TETRIS_OFFSET_X     ((1920 - TETRIS_GRID_W * TETRIS_BLOCK_SIZE) / 2)
#define TETRIS_OFFSET_Y     ((1080 - TETRIS_GRID_H * TETRIS_BLOCK_SIZE) / 2)

/*============================================================================
 * Tetromino Types (7 shapes)
 *============================================================================*/
typedef enum {
    TETRIS_I = 0,  // 青色
    TETRIS_O = 1,  // 黄色
    TETRIS_T = 2,  // 紫色
    TETRIS_S = 3,  // 绿色
    TETRIS_Z = 4,  // 红色
    TETRIS_J = 5,  // 蓝色
    TETRIS_L = 6,  // 橙色
    TETRIS_EMPTY = 7
} tetris_type_t;

/*============================================================================
 * Colors (RGB565 format)
 *============================================================================*/
#define COLOR_I  0x07FF  // 青色
#define COLOR_O  0xFFE0  // 黄色
#define COLOR_T  0x801F  // 紫色
#define COLOR_S  0x07E0  // 绿色
#define COLOR_Z  0xF800  // 红色
#define COLOR_J  0x001F  // 蓝色
#define COLOR_L  0xFD20  // 橙色
#define COLOR_BG 0x0000  // 黑色
#define COLOR_BORDER 0xFFFF  // 白色

/*============================================================================
 * Game State
 *============================================================================*/
typedef struct {
    uint8_t grid[TETRIS_GRID_H][TETRIS_GRID_W];  // 游戏区域（存储方块类型）

    // 当前方块
    tetris_type_t current_type;
    int current_x;
    int current_y;
    int current_rotation;

    // 下一个方块
    tetris_type_t next_type;

    // 游戏状态
    uint32_t score;
    uint32_t lines;
    uint32_t level;
    int game_over;

    // 定时器
    uint32_t last_fall_time;
    uint32_t fall_interval;  // 毫秒
} tetris_game_t;

/*============================================================================
 * API Functions
 *============================================================================*/
void tetris_init(void);
void tetris_run(void);

#endif // TETRIS_H
