/*------------------------------------------------------------------------------
 * Tetris Game Implementation
 *----------------------------------------------------------------------------*/

#include <tetris.h>
#include <hdmi.h>
#include <stdio.h>
#include <timer.h>

/*============================================================================
 * Tetromino Shape Definitions
 * 每个方块有4个旋转状态，每个状态用4个坐标表示
 *============================================================================*/

// 方块形状：[type][rotation][block_index][x/y]
static const int shapes[7][4][4][2] = {
    // I型
    {
        {{0,1}, {1,1}, {2,1}, {3,1}},  // 0度
        {{2,0}, {2,1}, {2,2}, {2,3}},  // 90度
        {{0,2}, {1,2}, {2,2}, {3,2}},  // 180度
        {{1,0}, {1,1}, {1,2}, {1,3}}   // 270度
    },
    // O型
    {
        {{1,1}, {2,1}, {1,2}, {2,2}},
        {{1,1}, {2,1}, {1,2}, {2,2}},
        {{1,1}, {2,1}, {1,2}, {2,2}},
        {{1,1}, {2,1}, {1,2}, {2,2}}
    },
    // T型
    {
        {{1,0}, {0,1}, {1,1}, {2,1}},  // 0度
        {{1,0}, {1,1}, {2,1}, {1,2}},  // 90度
        {{0,1}, {1,1}, {2,1}, {1,2}},  // 180度
        {{1,0}, {0,1}, {1,1}, {1,2}}   // 270度
    },
    // S型
    {
        {{1,0}, {2,0}, {0,1}, {1,1}},
        {{1,0}, {1,1}, {2,1}, {2,2}},
        {{1,1}, {2,1}, {0,2}, {1,2}},
        {{0,0}, {0,1}, {1,1}, {1,2}}
    },
    // Z型
    {
        {{0,0}, {1,0}, {1,1}, {2,1}},
        {{2,0}, {1,1}, {2,1}, {1,2}},
        {{0,1}, {1,1}, {1,2}, {2,2}},
        {{1,0}, {0,1}, {1,1}, {0,2}}
    },
    // J型
    {
        {{0,0}, {0,1}, {1,1}, {2,1}},
        {{1,0}, {2,0}, {1,1}, {1,2}},
        {{0,1}, {1,1}, {2,1}, {2,2}},
        {{1,0}, {1,1}, {0,2}, {1,2}}
    },
    // L型
    {
        {{2,0}, {0,1}, {1,1}, {2,1}},
        {{1,0}, {1,1}, {1,2}, {2,2}},
        {{0,1}, {1,1}, {2,1}, {0,2}},
        {{0,0}, {1,0}, {1,1}, {1,2}}
    }
};

// 颜色表
static const uint16_t colors[8] = {
    COLOR_I, COLOR_O, COLOR_T, COLOR_S,
    COLOR_Z, COLOR_J, COLOR_L, COLOR_BG
};

/*============================================================================
 * Global Game State
 *============================================================================*/
static tetris_game_t game;

/*============================================================================
 * Random Number Generator (简单的线性同余)
 *============================================================================*/
static uint32_t rand_seed = 12345;

static int tetris_rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & 0x7FFF;
}

static tetris_type_t tetris_random_type(void) {
    return (tetris_type_t)(tetris_rand() % 7);
}

/*============================================================================
 * Helper Functions
 *============================================================================*/

// 简单的延时函数（忙等待）
static void __attribute__((unused)) delay_cycles(uint32_t cycles) {
    for (volatile uint32_t i = 0; i < cycles; i++) {
        __asm__ volatile("nop");
    }
}

// 获取当前时间（毫秒）
static uint32_t get_time_ms(void) {
    uint64_t us = timer_get_uptime_us();
    // 避免 64 位除法，使用位移近似：us / 1000 ≈ us / 1024 = us >> 10
    return (uint32_t)(us >> 10);
}

// 绘制单个方格（优化版 - 去掉边界检查和重复计算）
static void draw_block(int grid_x, int grid_y, uint16_t color) {
    int pixel_x = TETRIS_OFFSET_X + grid_x * TETRIS_BLOCK_SIZE;
    int pixel_y = TETRIS_OFFSET_Y + grid_y * TETRIS_BLOCK_SIZE;

    volatile uint16_t *fb = hdmi_get_fb_pointer();

    // 预计算起始地址
    volatile uint16_t *fb_row = fb + (pixel_y + 1) * 1920 + pixel_x + 1;

    // 绘制实心方格（带1像素边框）
    for (int y = 1; y < TETRIS_BLOCK_SIZE - 1; y++) {
        volatile uint16_t *fb_ptr = fb_row;
        for (int x = 1; x < TETRIS_BLOCK_SIZE - 1; x++) {
            *fb_ptr++ = color;
        }
        fb_row += 1920;
    }
}

/*============================================================================
 * Collision Detection
 *============================================================================*/

// 检查方块是否可以放置在指定位置
static int check_collision(tetris_type_t type, int x, int y, int rotation) {
    for (int i = 0; i < 4; i++) {
        int bx = x + shapes[type][rotation][i][0];
        int by = y + shapes[type][rotation][i][1];

        // 检查边界
        if (bx < 0 || bx >= TETRIS_GRID_W || by >= TETRIS_GRID_H) {
            return 1;  // 碰撞
        }

        // 允许在顶部上方（by < 0）
        if (by < 0) {
            continue;
        }

        // 检查是否与已有方块重叠
        if (game.grid[by][bx] != TETRIS_EMPTY) {
            return 1;  // 碰撞
        }
    }

    return 0;  // 无碰撞
}

/*============================================================================
 * Game Logic Functions
 *============================================================================*/

// 将当前方块固定到游戏区域
static void lock_piece(void) {
    for (int i = 0; i < 4; i++) {
        int bx = game.current_x + shapes[game.current_type][game.current_rotation][i][0];
        int by = game.current_y + shapes[game.current_type][game.current_rotation][i][1];

        if (by >= 0 && by < TETRIS_GRID_H && bx >= 0 && bx < TETRIS_GRID_W) {
            game.grid[by][bx] = game.current_type;
        }
    }
}

// 检查并消除完整的行
static int clear_lines(void) {
    int lines_cleared = 0;

    for (int y = TETRIS_GRID_H - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < TETRIS_GRID_W; x++) {
            if (game.grid[y][x] == TETRIS_EMPTY) {
                full = 0;
                break;
            }
        }

        if (full) {
            lines_cleared++;
            // 将上面的行下移
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < TETRIS_GRID_W; x++) {
                    game.grid[yy][x] = game.grid[yy - 1][x];
                }
            }
            // 清空顶行
            for (int x = 0; x < TETRIS_GRID_W; x++) {
                game.grid[0][x] = TETRIS_EMPTY;
            }
            y++;  // 重新检查当前行
        }
    }

    return lines_cleared;
}

// 生成新方块
static void spawn_piece(void) {
    game.current_type = game.next_type;
    game.next_type = tetris_random_type();
    game.current_x = TETRIS_GRID_W / 2 - 2;
    game.current_y = -1;
    game.current_rotation = 0;

    // 检查是否游戏结束
    if (check_collision(game.current_type, game.current_x, game.current_y, game.current_rotation)) {
        game.game_over = 1;
    }
}

/*============================================================================
 * Rendering Functions
 *============================================================================*/

// 只清空游戏区域（10×20 方格）- 优化版
static void clear_game_area(void) {
    volatile uint16_t *fb = hdmi_get_fb_pointer();

    int start_x = TETRIS_OFFSET_X;
    int start_y = TETRIS_OFFSET_Y;
    int width = TETRIS_GRID_W * TETRIS_BLOCK_SIZE;
    int height = TETRIS_GRID_H * TETRIS_BLOCK_SIZE;

    // 预计算起始地址
    volatile uint16_t *fb_row = fb + start_y * 1920 + start_x;

    for (int y = 0; y < height; y++) {
        volatile uint16_t *fb_ptr = fb_row;
        for (int x = 0; x < width; x++) {
            *fb_ptr++ = COLOR_BG;
        }
        fb_row += 1920;
    }
}

// 绘制游戏区域的所有固定方块
static void draw_grid(void) {
    for (int y = 0; y < TETRIS_GRID_H; y++) {
        for (int x = 0; x < TETRIS_GRID_W; x++) {
            if (game.grid[y][x] != TETRIS_EMPTY) {
                draw_block(x, y, colors[game.grid[y][x]]);
            }
        }
    }
}

// 绘制当前下落的方块
static void draw_current_piece(void) {
    uint16_t color = colors[game.current_type];
    for (int i = 0; i < 4; i++) {
        int bx = game.current_x + shapes[game.current_type][game.current_rotation][i][0];
        int by = game.current_y + shapes[game.current_type][game.current_rotation][i][1];

        if (by >= 0) {
            draw_block(bx, by, color);
        }
    }
}

/*============================================================================
 * Input Handling
 *============================================================================*/

static int is_break_code = 0;  // 跟踪 break code 状态

// 处理键盘输入
static void handle_input(void) {
    extern int kb_get_scancode(void);
    int scancode = kb_get_scancode();

    if (scancode < 0) {
        return;
    }

    // 检测 break code 前缀
    if (scancode == 0xF0) {
        is_break_code = 1;
        return;
    }

    // 如果是 break code，忽略这个按键（按键释放）
    if (is_break_code) {
        is_break_code = 0;
        return;
    }

    switch (scancode) {
    case 0x1C:  // A - 左移
        if (!check_collision(game.current_type, game.current_x - 1, game.current_y, game.current_rotation)) {
            game.current_x--;
        }
        break;

    case 0x23:  // D - 右移
        if (!check_collision(game.current_type, game.current_x + 1, game.current_y, game.current_rotation)) {
            game.current_x++;
        }
        break;

    case 0x1B:  // S - 加速下落
        if (!check_collision(game.current_type, game.current_x, game.current_y + 1, game.current_rotation)) {
            game.current_y++;
            game.score++;
        }
        break;

    case 0x1D:  // W - 旋转
        {
            int new_rotation = (game.current_rotation + 1) % 4;
            if (!check_collision(game.current_type, game.current_x, game.current_y, new_rotation)) {
                game.current_rotation = new_rotation;
            }
        }
        break;

    case 0x29:  // 空格 - 硬降
        while (!check_collision(game.current_type, game.current_x, game.current_y + 1, game.current_rotation)) {
            game.current_y++;
            game.score += 2;
        }
        break;
    }
}

/*============================================================================
 * Game Initialization
 *============================================================================*/

void tetris_init(void) {
    // 清空游戏区域
    for (int y = 0; y < TETRIS_GRID_H; y++) {
        for (int x = 0; x < TETRIS_GRID_W; x++) {
            game.grid[y][x] = TETRIS_EMPTY;
        }
    }

    // 初始化游戏状态
    game.score = 0;
    game.lines = 0;
    game.level = 1;
    game.game_over = 0;
    game.fall_interval = 1000;  // 1秒下落一次

    // 使用当前时间作为随机种子
    rand_seed = get_time_ms();

    // 生成第一个和下一个方块
    game.next_type = tetris_random_type();
    spawn_piece();

    game.last_fall_time = get_time_ms();
}

/*============================================================================
 * Game Main Loop
 *============================================================================*/

void tetris_run(void) {
    printf("==========================================\n");
    printf("  Tetris Game\n");
    printf("==========================================\n");
    printf("Controls:\n");
    printf("  A/D      - Move Left/Right\n");
    printf("  W        - Rotate\n");
    printf("  S        - Soft Drop\n");
    printf("  Space    - Hard Drop\n");
    printf("  ESC      - Quit (not implemented)\n");
    printf("==========================================\n\n");

    // 初始化 HDMI 双缓冲
    hdmi_fb_write_base_set(BUFFER_A);
    hdmi_clear(COLOR_BG);
    hdmi_fb_write_base_set(BUFFER_B);
    hdmi_clear(COLOR_BG);

    // 显示 A，绘制到 B
    hdmi_fb_show_base_set(BUFFER_A);
    hdmi_fb_write_base_set(BUFFER_B);

    printf("Starting game...\n");

    // 记录上一帧的方块位置
    int last_x = game.current_x;
    int last_y = game.current_y;
    int last_rotation = game.current_rotation;
    tetris_type_t last_type = game.current_type;
    int need_full_redraw = 2;
    int frame_counter = 0;

    // 游戏主循环
    while (!game.game_over) {
        // 1. 处理键盘输入
        handle_input();

        // 2. 增量渲染（在当前绘制 buffer 上）
        if (need_full_redraw > 0) {
            clear_game_area();
            draw_grid();
            draw_current_piece();
            need_full_redraw--;
        } else {
            // 擦除旧位置
            for (int i = 0; i < 4; i++) {
                int bx = last_x + shapes[last_type][last_rotation][i][0];
                int by = last_y + shapes[last_type][last_rotation][i][1];
                if (by >= 0 && by < TETRIS_GRID_H && bx >= 0 && bx < TETRIS_GRID_W) {
                    if (game.grid[by][bx] != TETRIS_EMPTY) {
                        draw_block(bx, by, colors[game.grid[by][bx]]);
                    } else {
                        draw_block(bx, by, COLOR_BG);
                    }
                }
            }
            draw_current_piece();
        }

        // 3. swap 并在另一个 buffer 上做相同操作
        hdmi_swap_buffers();

        // 在另一个 buffer 上也做相同的绘制
        if (need_full_redraw > 0) {
            clear_game_area();
            draw_grid();
            draw_current_piece();
            need_full_redraw--;
        } else {
            for (int i = 0; i < 4; i++) {
                int bx = last_x + shapes[last_type][last_rotation][i][0];
                int by = last_y + shapes[last_type][last_rotation][i][1];
                if (by >= 0 && by < TETRIS_GRID_H && bx >= 0 && bx < TETRIS_GRID_W) {
                    if (game.grid[by][bx] != TETRIS_EMPTY) {
                        draw_block(bx, by, colors[game.grid[by][bx]]);
                    } else {
                        draw_block(bx, by, COLOR_BG);
                    }
                }
            }
            draw_current_piece();
        }

        // 3. 更新位置记录
        last_x = game.current_x;
        last_y = game.current_y;
        last_rotation = game.current_rotation;
        last_type = game.current_type;

        // 4. 帧计数控制自动下落
        frame_counter++;
        if (frame_counter >= 15) {  // 大约每15帧下落一次（约1秒）
            frame_counter = 0;
            if (!check_collision(game.current_type, game.current_x, game.current_y + 1, game.current_rotation)) {
                game.current_y++;
            } else {
                lock_piece();
                clear_lines();
                spawn_piece();
                need_full_redraw = 2;
            }
        }
    }

    // 游戏结束
    printf("\n\n==========================================\n");
    printf("  GAME OVER!\n");
    printf("==========================================\n");
    printf("Final Score: %u\n", game.score);
    printf("Lines Cleared: %u\n", game.lines);
    printf("Level Reached: %u\n", game.level);
    printf("==========================================\n\n");

    // 恢复 Shell buffer
    hdmi_fb_show_base_set(BUFFER_S);
}
