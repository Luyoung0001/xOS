/*------------------------------------------------------------------------------
 * xOS Shell - Simple Command Line Interface
 *----------------------------------------------------------------------------*/

#ifndef __SHELL_H__
#define __SHELL_H__


/*============================================================================
 * Shell Configuration
 *============================================================================*/
#define SHELL_CMD_MAX_LEN    128    /* Maximum command line length */
#define SHELL_MAX_ARGS       16     /* Maximum arguments per command */
#define SHELL_PROMPT         "xos> "

/*============================================================================
 * Command Handler Type
 *============================================================================*/
typedef int (*shell_cmd_handler_t)(int argc, char *argv[]);

/*============================================================================
 * Command Entry Structure
 *============================================================================*/
typedef struct {
    const char *name;           /* Command name */
    const char *help;           /* Help message */
    shell_cmd_handler_t handler; /* Command handler function */
} shell_cmd_t;

/*============================================================================
 * Shell API
 *============================================================================*/

/**
 * Initialize shell
 */
void shell_init(void);

/**
 * Run shell main loop (never returns)
 */
void shell_run(void);

/**
 * Process a single character input
 * @param c: Input character
 */
void shell_input_char(char c);

/**
 * Print shell prompt
 */
void shell_print_prompt(void);

/*============================================================================
 * Built-in Commands
 *============================================================================*/
int cmd_help(int argc, char *argv[]);
int cmd_echo(int argc, char *argv[]);
int cmd_clear(int argc, char *argv[]);
int cmd_info(int argc, char *argv[]);
int cmd_countdown(int argc, char *argv[]);
int cmd_ps(int argc, char *argv[]);
int cmd_fg(int argc, char *argv[]);
int cmd_bg(int argc, char *argv[]);
int cmd_logs(int argc, char *argv[]);
int cmd_heap(int argc, char *argv[]);
int cmd_hdmi_buffer_gc(int argc, char *argv[]);
int cmd_kill(int argc, char *argv[]);
int cmd_mario(int argc, char *argv[]);
int cmd_tetris(int argc, char *argv[]);
int cmd_change(int argc, char *argv[]);
int cmd_hdmi_src(int argc, char *argv[]);
int cmd_jit_mode(int argc, char *argv[]);
int cmd_difftest(int argc, char *argv[]);

#endif /* __SHELL_H__ */
