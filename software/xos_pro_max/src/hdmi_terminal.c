/*------------------------------------------------------------------------------
 * HDMI Terminal Implementation - 极简设计
 *
 * 核心思路：
 * 1. cursor_y 从 0 一直增长，无脑渲染到对应位置
 * 2. 渲染后检查是否超出可见窗口，超出就 scroll up
 * 3. scroll up 只做一件事：移动显示窗口的起始地址 和 清除新出现的行
 * 4. 当 cursor_y 超出缓冲区总行数时，重置缓冲区并清屏
 * 5. 后面的改进思路是，让并发的 task_gc
 *定期清理已经刷上去的旧行，这样重置缓冲区的时候就不用清屏了，不然会慢
 *----------------------------------------------------------------------------*/
#include <hdmi.h>
#include <hdmi_terminal.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <uart.h>

/* Character buffer - 136 行 */
terminal_cell_t screen_buffer[TERMINAL_TOTAL_ROWS][TERMINAL_COLS];

/* 显示窗口起始行 */
int display_start_row = 0;

/* 光标位置 */
int cursor_x = 0;
int cursor_y = 0;

uint16_t current_fg_color = TERM_COLOR_WHITE;
uint16_t current_bg_color = TERM_COLOR_BLACK;

/*------------------------------------------------------------------------------
 * Initialize terminal
 *----------------------------------------------------------------------------*/
void terminal_init(void) {
    hdmi_init();
    hdmi_set_font_size(TERMINAL_FONT_SIZE);
    memset(screen_buffer, 0, sizeof(screen_buffer));
    cursor_x = 0;
    cursor_y = 0;
    display_start_row = 0;
    current_fg_color = TERM_COLOR_WHITE;
    current_bg_color = TERM_COLOR_BLACK;
    hdmi_clear(current_bg_color);
}

/*------------------------------------------------------------------------------
 * Reset buffer
 *----------------------------------------------------------------------------*/
static void terminal_reset_buffer(void) {

    // reset cursor and display window
    cursor_x = 0;
    cursor_y = 0;
    display_start_row = 0;

    // reset hdmi framebuffer show address
    hdmi_set_show_addr(HDMI_FB_BASE_S);

    /* Clear framebuffer */
    // hdmi_clear(current_bg_color);

    /* Clear screen buffer */
    memset(screen_buffer, 0, sizeof(screen_buffer));
}
void terminal_scroll_up(void) {
    display_start_row++;

    uint32_t offset = display_start_row * TERMINAL_FONT_SIZE * HDMI_WIDTH * 2;
    hdmi_set_show_addr(offset + HDMI_FB_BASE_S);

    /* 清除新出现的底部行 */
    int new_row = cursor_y;
    int pixel_row_start = new_row * TERMINAL_FONT_SIZE;
    int pixel_row_end = pixel_row_start + TERMINAL_FONT_SIZE;
    hdmi_clear_line(pixel_row_start, pixel_row_end, current_bg_color);
}

/*------------------------------------------------------------------------------
 * Render a character directly to framebuffer
 *----------------------------------------------------------------------------*/
void terminal_render_char(int x, int y) {
    if (y >= TERMINAL_TOTAL_ROWS) {
        return;
    }
    terminal_cell_t *cell = &screen_buffer[y][x];
    int pixel_x = x * TERMINAL_FONT_SIZE;
    int pixel_y = y * TERMINAL_FONT_SIZE;
    hdmi_draw_char(pixel_x, pixel_y, cell->ch, cell->fg_color, cell->bg_color);
}

/*------------------------------------------------------------------------------
 * Handle newline
 *----------------------------------------------------------------------------*/
void terminal_newline(void) {
    cursor_x = 0;
    cursor_y++;

    /* 检查是否到达缓冲区底部 */
    if (cursor_y >= TERMINAL_TOTAL_ROWS) {
        /* 缓冲区满了，重置缓冲区并清屏 */
        terminal_reset_buffer();
        return;
    }

    /* 检查是否超出可见窗口 */
    if (cursor_y >= display_start_row + TERMINAL_ROWS) {
        terminal_scroll_up();
    }
}

/*------------------------------------------------------------------------------
 * Output a character
 *----------------------------------------------------------------------------*/

void terminal_putchar(int tid, char c) {

    if (tid < 0 || tid >= MAX_TASKS) {
        return;
    }

    task_t *task = &tasks[tid];
    task_output_t *out = &task->output;

    /* Write to task buffer (ring buffer) */
    out->buffer[out->write_pos] = c;
    out->write_pos = (out->write_pos + 1) % TASK_OUTPUT_BUF_SIZE;
    out->total_bytes++;

    /* If foreground, also write to HDMI terminal */
    if (out->is_foreground) {
        terminal_putc(c);
    }
}

void terminal_putc(char c) {
    if (c == '\n') {
        terminal_newline();
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        return;
    }
    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            screen_buffer[cursor_y][cursor_x].ch = ' ';
            screen_buffer[cursor_y][cursor_x].fg_color = current_fg_color;
            screen_buffer[cursor_y][cursor_x].bg_color = current_bg_color;
            terminal_render_char(cursor_x, cursor_y);
        }
        return;
    }
    if (c == '\t') {
        int spaces = 4 - (cursor_x % 4);
        for (int i = 0; i < spaces; i++) {
            terminal_putc(' ');
        }
        return;
    }

    /* Normal character */
    if (c >= 32 && c <= 126) {
        screen_buffer[cursor_y][cursor_x].ch = c;
        screen_buffer[cursor_y][cursor_x].fg_color = current_fg_color;
        screen_buffer[cursor_y][cursor_x].bg_color = current_bg_color;
        terminal_render_char(cursor_x, cursor_y);
        cursor_x++;
        if (cursor_x >= TERMINAL_COLS) {
            terminal_newline();
        }
    }
}



/*------------------------------------------------------------------------------
 * Clear screen
 *----------------------------------------------------------------------------*/
void terminal_clear(void) {
    memset(screen_buffer, 0, sizeof(screen_buffer));
    hdmi_clear(current_bg_color);
    cursor_x = 0;
    cursor_y = 0;
    display_start_row = 0;
    hdmi_set_show_addr(HDMI_FB_BASE_S);
}

/*------------------------------------------------------------------------------
 * Set colors
 *----------------------------------------------------------------------------*/
void terminal_set_color(uint16_t fg, uint16_t bg) {
    current_fg_color = fg;
    current_bg_color = bg;
}

/*------------------------------------------------------------------------------
 * Get/Set cursor
 *----------------------------------------------------------------------------*/
void terminal_get_cursor(int *x, int *y) {
    if (x)
        *x = cursor_x;
    if (y)
        *y = cursor_y;
}

void terminal_set_cursor(int x, int y) {
    if (x >= 0 && x < TERMINAL_COLS)
        cursor_x = x;
    if (y >= 0 && y < TERMINAL_TOTAL_ROWS)
        cursor_y = y;
}
