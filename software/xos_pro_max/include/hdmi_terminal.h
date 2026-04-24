/*------------------------------------------------------------------------------
 * HDMI Terminal Module
 *
 * Provides a text terminal interface on HDMI display
 * - Character buffer management
 * - Cursor positioning
 * - Scrolling support
 * - Rendering to framebuffer
 *----------------------------------------------------------------------------*/

#ifndef HDMI_TERMINAL_H
#define HDMI_TERMINAL_H

#include <hdmi.h>
#include <stdint.h>

/* Terminal configuration */
#define TERMINAL_FONT_SIZE 16
#define TERMINAL_COLS (1920 / TERMINAL_FONT_SIZE) /* 120 columns */
#define TERMINAL_ROWS (1080 / TERMINAL_FONT_SIZE) /* 67 rows */

// Total rows in ring buffer (to allow scrolling)
#define TERMINAL_TOTAL_ROWS (FRAMEBUF_S_NUM_ROWS / TERMINAL_FONT_SIZE)

/* Terminal colors */
#define TERM_COLOR_BLACK 0x0000
#define TERM_COLOR_WHITE 0xFFFF
#define TERM_COLOR_RED 0xF800
#define TERM_COLOR_GREEN 0x07E0
#define TERM_COLOR_BLUE 0x001F
#define TERM_COLOR_YELLOW 0xFFE0
#define TERM_COLOR_CYAN 0x07FF
#define TERM_COLOR_MAGENTA 0xF81F

/* Character cell structure */
typedef struct {
    char ch;           /* Character */
    uint16_t fg_color; /* Foreground color */
    uint16_t bg_color; /* Background color */
} terminal_cell_t;

/* Terminal API */
void terminal_init(void);
void terminal_putc(char c);
void terminal_putchar(int tid, char c);
void terminal_clear(void);
void terminal_set_color(uint16_t fg, uint16_t bg);
void terminal_get_cursor(int *x, int *y);
void terminal_set_cursor(int x, int y);

/* Internal functions */
void terminal_scroll_up(void);
void terminal_render_char(int x, int y);
void terminal_newline(void);
void update_display_window(void);



extern terminal_cell_t screen_buffer[TERMINAL_TOTAL_ROWS][TERMINAL_COLS];
extern int display_start_row;
extern int cursor_x;
extern int cursor_y;

extern uint16_t current_fg_color;
extern uint16_t current_bg_color;

extern int shell_gc_pointer;

#endif /* HDMI_TERMINAL_H */
