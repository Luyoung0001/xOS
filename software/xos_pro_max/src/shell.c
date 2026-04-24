/*------------------------------------------------------------------------------
 * xOS Shell Implementation
 *----------------------------------------------------------------------------*/

#include <hdmi.h>
#include <hdmi_terminal.h>
#include <heap.h>
#include <jit_demo.h>
#include <litenes/am.h>
#include <litenes/fce.h>
#include <litenes/jit.h>
#include <ps2.h>
#include <sched.h>
#include <shell.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <tetris.h>
#include <uart.h>
#include <uart_display.h>
#include <nes.h>
/*============================================================================
 * External: Keyboard buffer from interrupt handler (in main.c)
 *============================================================================*/
extern int kb_get_scancode(void);
// JIT stub symbols are provided by litenes/jit_stub.c
extern bool jit_enabled;
extern bool difftest_enabled;
extern int difftest_pass_count;
extern int difftest_fail_count;

/*============================================================================
 * Static Variables
 *============================================================================*/
static char cmd_buffer[SHELL_CMD_MAX_LEN];
static int cmd_pos = 0;

/* PS2 scancode state machine */
static int is_break_code = 0;
static int is_extended = 0;
static uint8_t nes_joypad_state = 0;

#ifndef QEMU_RUN
// NES joypad bit definitions
#define NES_BTN_A      (1u << 0)
#define NES_BTN_B      (1u << 1)
#define NES_BTN_SELECT (1u << 2)
#define NES_BTN_START  (1u << 3)
#define NES_BTN_UP     (1u << 4)
#define NES_BTN_DOWN   (1u << 5)
#define NES_BTN_LEFT   (1u << 6)
#define NES_BTN_RIGHT  (1u << 7)

static uint8_t nes_mask_from_scancode(uint8_t scancode) {
    switch (scancode) {
    case 0x1D: return NES_BTN_UP;     // W
    case 0x1C: return NES_BTN_LEFT;   // A
    case 0x1B: return NES_BTN_DOWN;   // S
    case 0x23: return NES_BTN_RIGHT;  // D
    case 0x3B: return NES_BTN_A;      // J
    case 0x42: return NES_BTN_B;      // K
    case 0x3C: return NES_BTN_SELECT; // U
    case 0x43: return NES_BTN_START;  // I
    default:   return 0;
    }
}
#endif

/* External declarations */
int shell_gc_pointer = 0;

/*============================================================================
 * Task Wrapper for Background Commands
 *============================================================================*/
typedef struct {
    shell_cmd_handler_t handler;
    int argc;
    char *argv_storage[SHELL_MAX_ARGS];
    char arg_buffer[SHELL_CMD_MAX_LEN];
} task_cmd_context_t;

static task_cmd_context_t task_contexts[MAX_TASKS];

//=============================================================================
// NES hardware helpers (Boot RAM loader)
//=============================================================================
#ifndef QEMU_RUN
static void delay_cycles(volatile uint32_t loops) {
    while (loops--) {
        __asm__ volatile("nop");
    }
}

static void mem_copy(volatile uint8_t *dst, const uint8_t *src, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

static void mem_fill(volatile uint8_t *dst, uint8_t val, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = val;
    }
}

static uint8_t nes_size_code(uint8_t blocks) {
    if (blocks <= 1) return 0;
    if (blocks <= 2) return 1;
    if (blocks <= 4) return 2;
    if (blocks <= 8) return 3;
    if (blocks <= 16) return 4;
    if (blocks <= 32) return 5;
    if (blocks <= 64) return 6;
    return 7;
}

static int nes_hw_load_rom(const uint8_t *rom, uint32_t rom_len, uint32_t *out_flags) {
    const uint32_t BOOT_RAM_BASE = 0x20000000u;
    const uint32_t NES_BASE_OFFSET = 0x00000400u;
    const uint32_t NES_PRG_OFF  = 0x00000000u; // 32KB
    const uint32_t NES_CHR_OFF  = 0x00008000u; // 8KB
    const uint32_t NES_VRAM_OFF = 0x0000A000u; // 2KB
    const uint32_t NES_WRAM_OFF = 0x0000A800u; // 2KB

    const uint32_t NES_PRG_MAX = 0x8000u;
    const uint32_t NES_CHR_MAX = 0x2000u;
    const uint32_t NES_VRAM_SIZE = 0x800u;
    const uint32_t NES_WRAM_SIZE = 0x800u;

    if (rom_len < 16) {
        return -1;
    }
    // name check
    if (rom[0] != 'N' || rom[1] != 'E' || rom[2] != 'S' || rom[3] != 0x1A) {
        return -1;
    }
    // PRG (Program ROM)
    uint8_t prg_blocks = rom[4];
    // CHR (Character ROM/RAM)
    uint8_t chr_blocks = rom[5];
    uint8_t flag6 = rom[6];
    uint8_t flag7 = rom[7];
    uint8_t mapper = (flag7 & 0xF0) | (flag6 >> 4);
    uint8_t mirroring = flag6 & 0x1;
    uint8_t has_chr_ram = (chr_blocks == 0);

    // 16KB per block
    uint32_t prg_bytes = (uint32_t)prg_blocks * 0x4000u;
    // 8KB per block
    uint32_t chr_bytes = (uint32_t)chr_blocks * 0x2000u;

    if (mapper != 0) {
        return -1;
    }
    if (prg_bytes > NES_PRG_MAX || chr_bytes > NES_CHR_MAX) {
        return -1;
    }
    if (rom_len < 16u + prg_bytes + chr_bytes) {
        return -1;
    }

    uint8_t prg_code = nes_size_code(prg_blocks);
    uint8_t chr_code = nes_size_code(chr_blocks);
    uint32_t flags = ((uint32_t)has_chr_ram << 15) |
                     ((uint32_t)mirroring  << 14) |
                     ((uint32_t)chr_code   << 11) |
                     ((uint32_t)prg_code   << 8)  |
                     (uint32_t)mapper;

    const uint8_t *prg = rom + 16;
    volatile uint8_t *prg_dst = (volatile uint8_t *)(BOOT_RAM_BASE + NES_BASE_OFFSET + NES_PRG_OFF);
    mem_copy(prg_dst, prg, prg_bytes);
    if (prg_blocks == 1) {
        mem_copy(prg_dst + 0x4000u, prg, 0x4000u);
    }

    volatile uint8_t *chr_dst = (volatile uint8_t *)(BOOT_RAM_BASE + NES_BASE_OFFSET + NES_CHR_OFF);
    if (chr_blocks > 0) {
        const uint8_t *chr = prg + prg_bytes;
        mem_copy(chr_dst, chr, chr_bytes);
    } else {
        mem_fill(chr_dst, 0x00, NES_CHR_MAX);
    }

    mem_fill((volatile uint8_t *)(BOOT_RAM_BASE + NES_BASE_OFFSET + NES_VRAM_OFF), 0x00, NES_VRAM_SIZE);
    mem_fill((volatile uint8_t *)(BOOT_RAM_BASE + NES_BASE_OFFSET + NES_WRAM_OFF), 0x00, NES_WRAM_SIZE);

    *out_flags = flags;
    return 0;
}
#endif

/*============================================================================
 * Command Table
 *============================================================================*/
static const shell_cmd_t commands[] = {
    {"help", "Show available commands", cmd_help},
    {"echo", "Echo arguments (e.g. echo hello world)", cmd_echo},
    {"clear", "Clear screen", cmd_clear},
    {"info", "Show system information", cmd_info},
    {"ps", "Show task status", cmd_ps},
    {"fg", "Move task to foreground (e.g. fg 1)", cmd_fg},
    {"bg", "Move task to background (e.g. bg 1)", cmd_bg},
    {"logs", "Show task output history (e.g. logs 1)", cmd_logs},
    {"heap", "Show heap memory statistics", cmd_heap},
    {"countdown", "Countdown from N (e.g. countdown 10)", cmd_countdown},
    {"hdmigc", "Run HDMI buffer garbage collection", cmd_hdmi_buffer_gc},
    {"kill", "Kill task (e.g. kill 1)", cmd_kill},
    {"mario", "Run Super Mario Bros (QEMU: LiteNES, FPGA: NES HW)", cmd_mario},
    {"tetris", "Play Tetris game", cmd_tetris},
    {"change", "Change framebuffer (e.g. change A/B/S)", cmd_change},
    {"hdmisrc", "Switch HDMI source (ddr/nes)", cmd_hdmi_src},
    {"jitdemo", "Run JIT compiler demo", cmd_jit_demo},
    {"jitmode", "Toggle JIT mode (on/off)", cmd_jit_mode},
    {"difftest", "Toggle real-time DiffTest (on/off)", cmd_difftest},
    {NULL, NULL, NULL} /* End marker */
};

/* Parse command line into argc/argv */
static int parse_command(char *cmd, char *argv[], int max_args) {
    int argc = 0;
    char *p = cmd;

    while (*p && argc < max_args) {
        /* Skip leading spaces */
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;

        /* Start of argument */
        argv[argc++] = p;

        /* Find end of argument */
        while (*p && *p != ' ')
            p++;

        /* Null terminate */
        if (*p) {
            *p++ = '\0';
        }
    }

    return argc;
}

/* Task wrapper function - executes command and exits */
static void task_wrapper(void) {
    printf("wrapper entered\n");
    int tid = get_current_task();
    if (tid < 0 || tid >= MAX_TASKS) {
        task_exit();
    }

    task_cmd_context_t *ctx = &task_contexts[tid];

    /* Reconstruct argv pointers from stored buffer */
    char *p = ctx->arg_buffer;
    for (int i = 0; i < ctx->argc; i++) {
        ctx->argv_storage[i] = p;
        while (*p++)
            ; /* Skip to next null terminator */
    }

    /* Execute command handler */
    ctx->handler(ctx->argc, ctx->argv_storage);

    /* Task complete, exit */
    task_exit();
}

/* Execute command */
static void execute_command(void) {
    char *argv[SHELL_MAX_ARGS];
    int argc;

    /* Skip empty commands */
    if (cmd_pos == 0)
        return;

    /* Null terminate */
    cmd_buffer[cmd_pos] = '\0';

    /* Parse into argc/argv */
    argc = parse_command(cmd_buffer, argv, SHELL_MAX_ARGS);
    if (argc == 0)
        return;

    /* Find command */
    const shell_cmd_t *cmd = commands;
    while (cmd->name) {
        if (strcmp(argv[0], cmd->name) == 0) {
            /* Check if command should run as background task */
            int run_background = 0;

            /* Countdown command runs in background */
            if (strcmp(cmd->name, "countdown") == 0 ||
                strcmp(cmd->name, "hdmigc") == 0 ||
                strcmp(cmd->name, "tetris") == 0) {
                run_background = 1;
            }

            if (strcmp(cmd->name, "mario") == 0) {
#ifdef QEMU_RUN
                /* QEMU 下 mario 需要独占 UART 输入，前台运行 */
                run_background = 0;
#else
                run_background = 1;
#endif
            }

            if (run_background) {
                /* Critical section: disable interrupts to prevent race
                 * condition */
                /* We must setup task context before the task can be scheduled
                 */
                irq_global_disable();

                /* Create background task */
                int tid = task_create(task_wrapper, cmd->name);
                if (tid < 0) {
                    irq_global_enable();
                    return;
                }
                /* Store command context for the task */
                task_cmd_context_t *ctx = &task_contexts[tid];
                ctx->handler = cmd->handler;
                ctx->argc = argc;

                /* Copy arguments into task's buffer */
                char *dst = ctx->arg_buffer;
                for (int i = 0; i < argc; i++) {
                    int len = strlen(argv[i]);
                    strcpy(dst, argv[i]);
                    dst += len + 1; /* Include null terminator */
                }
                /* Re-enable interrupts - now it's safe to schedule the task */
                irq_global_enable();
            } else {
                /* Run in foreground (current shell context) */
                cmd->handler(argc, argv);
            }
            return;
        }
        cmd++;
    }

    /* Command not found */
    printf("Unknown command: %s\n", argv[0]);
    printf("Type 'help' for available commands.\n");
}

/*============================================================================
 * PS2 Input Processing (hardware path only)
 *============================================================================*/
#ifndef QEMU_RUN
static void process_scancode(uint8_t scancode) {

    // when pressed a key then released it
    // here we can get 3 scancodes: make code, break code(2 scancodes included).
    // break code includes 2 scancodes: F0h and the very make code.
    // off cause we want the first scancode to be the make code,
    // so when the scancode comes, if it is a make code, we can
    // get the nes-mask, or can pass it to the shell.

    // when it is break code, which includes F0h, we
    // just make a tag is_break_code = 1 and return.
    // and the next scancode is make code, but it is not what we want, so just return.
    // when is_break_code detected, we can clear the bit and be ready for the next key press.


    // as for the extend code, it is used for some special keys like arrow keys,
    // like ctrl + function keys, a example is: when we press ctrl + w, and the release
    // w, then release ctrl, we will get 6 scancodes: 12h 1Dh F0h 1Dh F0h 12h
    // what we want is 12h 1Dh, so when we once start to get break code, the function works.
    // this function is a little complex, now just simplify it.



    if (scancode == PS2_EXTENDED_CODE) {
        is_extended = 1;
        return;
    }
    if (scancode == PS2_BREAK_CODE) {
        is_break_code = 1;
        return;
    }

    // Update NES joypad state (keydown/keyup)
    uint8_t mask = nes_mask_from_scancode(scancode);
    if (mask) {
        if (is_break_code) {
            // key release -> clear bit
            nes_joypad_state &= (uint8_t)~mask;
        } else {
            // key press -> set bit
            nes_joypad_state |= mask;
        }
        bsp_nes_set_joypad(nes_joypad_state);
    }

    if (is_break_code) {
        is_break_code = 0;
        is_extended = 0;
        return;
    }
    if (is_extended) {
        is_extended = 0;
        return;
    }

    char c = bsp_ps2_to_ascii(scancode);
    if (c)
        shell_input_char(c);

    is_break_code = 0;
    is_extended = 0;
}
#endif

/*============================================================================
 * Shell API Implementation
 *============================================================================*/

void shell_init(void) {
    cmd_pos = 0;
    is_break_code = 0;
    is_extended = 0;
    nes_joypad_state = 0;
    bsp_nes_set_joypad(0);

    /* Clear command buffer */
    for (int i = 0; i < SHELL_CMD_MAX_LEN; i++) {
        cmd_buffer[i] = '\0';
    }
}

void shell_print_prompt(void) { printf(SHELL_PROMPT); }

void shell_input_char(char c) {
    switch (c) {
    case '\n':        /* Enter */
        printf("\n"); // UART 驱动会自动转换为 \r\n
        execute_command();
        cmd_pos = 0;
        shell_print_prompt();
        break;

    case '\b': /* Backspace */
        if (cmd_pos > 0) {
            cmd_pos--;
            /* Erase character on screen: backspace, space, backspace */
            bsp_uart_putc(0, '\b');
            bsp_uart_putc(0, ' ');
            bsp_uart_putc(0, '\b');
        }
        break;

    case '\t': /* Tab - ignore for now */
        break;

    case 0x1B: /* ESC - clear line */
        while (cmd_pos > 0) {
            cmd_pos--;
            bsp_uart_putc(0, '\b');
            bsp_uart_putc(0, ' ');
            bsp_uart_putc(0, '\b');
        }
        break;

    default:
        /* Printable character */
        if (c >= ' ' && c <= '~' && cmd_pos < SHELL_CMD_MAX_LEN - 1) {
            cmd_buffer[cmd_pos++] = c;
            putchar(c); /* Echo */
        }
        break;
    }
}
// helper functions for LED blinking
#define LED_REG (*(volatile unsigned int *)0x1FD0F000)

#define LED0 0x1
#define LED1 0x2
#define LED_OFF 0x0

/* 0.1秒延时（50MHz时钟约5000000个周期，循环约10周期） */
static void delay_100ms(void) {
    volatile int i;
    for (i = 0; i < 50000; i++) {
        __asm__ volatile("nop");
    }
}

/* 闪烁LED n次 */
static void __attribute__((unused)) blink_led(unsigned int led, int count) {
    int i;
    for (i = 0; i < count; i++) {
        LED_REG = led; /* 亮 */
        delay_100ms();
        LED_REG = LED_OFF; /* 灭 */
        delay_100ms();
    }
}

void shell_run(void) {
    printf("\n");
    printf("========================================\n");
    printf("  xOS - Simple Operating System\n");
    printf("  for LoongArch32R SoC\n");
    printf("========================================\n");
    printf("\n");
    printf("Type 'help' for available commands.\n");
    printf("\n");

    shell_print_prompt();

    /* Main loop - keyboard input comes from interrupt buffer */
    while (1) {
#ifdef QEMU_RUN
        /* QEMU 模式：从 UART 读取输入 */
        int c = bsp_uart_getc_nonblock(0);
        if (c >= 0) {
            char ch = (char)c;
            /* 处理特殊字符 */
            if (ch == '\r' || ch == '\n') {
                shell_input_char('\n');
            } else if (ch == 0x7F || ch == '\b') {
                shell_input_char('\b');
            } else {
                shell_input_char(ch);
            }
        }
#else
        /* 真实硬件：从 PS2 键盘缓冲区读取 */
        int scancode = kb_get_scancode();
        if (scancode >= 0) {
            process_scancode((uint8_t)scancode);
        }
#endif
    }
}

/*============================================================================
 * Built-in Commands
 *============================================================================*/

int cmd_jit_mode(int argc, char *argv[]) {
    extern void jit_dump_code(void);
    if (argc < 2) {
        printf("JIT mode: %s\n", jit_enabled ? "ON" : "OFF");
        printf("Usage: jitmode on|off|stats|reset|dump\n");
        return 0;
    }
    if (strcmp(argv[1], "on") == 0) {
        // 每次打开 JIT 先清理缓存，避免旧块遗留导致状态异常
        jit_init();
        jit_enabled = true;
        printf("JIT mode enabled\n");
    } else if (strcmp(argv[1], "off") == 0) {
        jit_enabled = false;
        printf("JIT mode disabled\n");
    } else if (strcmp(argv[1], "stats") == 0) {
        jit_print_stats();
    } else if (strcmp(argv[1], "reset") == 0) {
        jit_reset_stats();
        printf("JIT stats reset\n");
    } else if (strcmp(argv[1], "dump") == 0) {
        jit_dump_code();
    } else {
        printf("Usage: jitmode on|off|stats|reset|dump\n");
    }
    return 0;
}

int cmd_difftest(int argc, char *argv[]) {
    if (argc < 2) {
        printf("DiffTest mode: %s\n", difftest_enabled ? "ON" : "OFF");
        printf("Pass: %d, Fail: %d\n", difftest_pass_count,
               difftest_fail_count);
        printf("Usage: difftest on|off|reset\n");
        return 0;
    }
    if (strcmp(argv[1], "on") == 0) {
        difftest_enabled = true;
        printf("DiffTest mode enabled\n");
    } else if (strcmp(argv[1], "off") == 0) {
        difftest_enabled = false;
        printf("DiffTest mode disabled\n");
        printf("Final: Pass=%d, Fail=%d\n", difftest_pass_count,
               difftest_fail_count);
    } else if (strcmp(argv[1], "reset") == 0) {
        difftest_pass_count = 0;
        difftest_fail_count = 0;
        printf("DiffTest counters reset\n");
    } else {
        printf("Usage: difftest on|off|reset\n");
    }
    return 0;
}

int cmd_help(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("Available commands:\n");
    const shell_cmd_t *cmd = commands;
    while (cmd->name) {
        printf("%s - %s\n", cmd->name, cmd->help);
        cmd++;
    }
    return 0;
}

int cmd_countdown(int argc, char *argv[]) {
    int count = 5; /* Default countdown value */

    /* Parse argument if provided */
    if (argc > 1) {
        /* Simple atoi implementation */
        count = 0;
        char *p = argv[1];
        while (*p >= '0' && *p <= '9') {
            count = count * 10 + (*p - '0');
            p++;
        }
        if (count <= 0 || count > 100) {
            printf("Invalid count (must be 1-100)\n");
            return -1;
        }
    }

    printf("Countdown starting from %d...\n", count);

    /* Countdown loop */
    for (int i = count; i > 0; i--) {
        printf("%d...\n", i);

        /* Busy wait delay*/
        volatile int delay;
        for (delay = 0; delay < 5000; delay++) {
            __asm__ volatile("nop");
        }
    }

    printf("Countdown complete!\n");
    return 0;
}

int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) {
            printf(" ");
        }
    }
    printf("\n");
    return 0;
}

int cmd_clear(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* ANSI escape sequence to clear screen */
    printf("\033[2J\033[H");
    return 0;
}

int cmd_info(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("xOS System Information\n");
    printf("----------------------\n");
    printf("CPU:           LoongArch32R\n");
    printf("Board:         A7-Lite FPGA\n");
    printf("RAM:           512MB\n");
    printf("FRAMBUF:       0x1F000000\n");
    printf("FRAMEEND:      0x1FCFF000\n");
    printf("ROM:           8KB\n");
    printf("LIBC:          mylibc\n");
    printf("Keyboard:      PS2 (Interrupt-driven)\n");
    printf("Serial:        UART 9600 8N1\n");
    return 0;
}

int cmd_ps(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    const char *state_names[] = {"UNUSED", "READY ", "RUN   ", "DEAD  "};

    printf("TID STATE  FG NAME        OUTPUT\n");
    printf("--- ------ -- ----------- ------\n");

    int current = get_current_task();

    for (int i = 0; i < MAX_TASKS; i++) {
        const task_t *task = get_task_info(i);
        if (task && task->state != TASK_UNUSED) {
            /* Print marker */
            if (i == current) {
                printf("*");
            } else {
                printf(" ");
            }

            /* Print TID */
            printf("%d  ", i);

            /* Print state */
            printf("%s ", state_names[task->state]);

            /* Print FG/BG */
            if (task->output.is_foreground) {
                printf("FG ");
            } else {
                printf("BG ");
            }

            /* Print name */
            printf("%s ", task->name);

            /* Print output size */
            uint32_t output_kb = task->output.total_bytes / 1024;
            printf("%uKB\n", output_kb);
        }
    }

    printf("\nActive tasks: %d/%d\n", get_num_tasks(), MAX_TASKS);
    printf("Current task: %d\n", current);
    printf("Foreground task: %d\n", get_foreground_task());

    return 0;
}

int cmd_fg(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: fg <task_id>\n");
        return -1;
    }

    /* Parse task ID */
    int tid = 0;
    char *p = argv[1];
    while (*p >= '0' && *p <= '9') {
        tid = tid * 10 + (*p - '0');
        p++;
    }

    if (tid < 0 || tid >= MAX_TASKS) {
        printf("Invalid task ID: %d\n", tid);
        return -1;
    }

    /* Set task to foreground */
    if (task_set_foreground(tid, 1) < 0) {
        printf("Failed to set task %d to foreground\n", tid);
        return -1;
    }

    printf("Task %d moved to foreground\n", tid);
    return 0;
}

int cmd_bg(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: bg <task_id>\n");
        return -1;
    }

    /* Parse task ID */
    int tid = 0;
    char *p = argv[1];
    while (*p >= '0' && *p <= '9') {
        tid = tid * 10 + (*p - '0');
        p++;
    }

    if (tid < 0 || tid >= MAX_TASKS) {
        printf("Invalid task ID: %d\n", tid);
        return -1;
    }

    if (tid == 0) {
        printf("Cannot move shell to background\n");
        return -1;
    }

    /* Set task to background */
    if (task_set_foreground(tid, 0) < 0) {
        printf("Failed to set task %d to background\n", tid);
        return -1;
    }

    printf("Task %d moved to background\n", tid);
    return 0;
}

int cmd_logs(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: logs <task_id>\n");
        return -1;
    }

    /* Parse task ID */
    int tid = 0;
    char *p = argv[1];
    while (*p >= '0' && *p <= '9') {
        tid = tid * 10 + (*p - '0');
        p++;
    }

    if (tid < 0 || tid >= MAX_TASKS) {
        printf("Invalid task ID: %d\n", tid);
        return -1;
    }

    const task_t *task = get_task_info(tid);
    if (!task || task->state == TASK_UNUSED) {
        printf("Task %d does not exist\n", tid);
        return -1;
    }

    printf("=== Task %d (%s) Output ===\n", tid, task->name);

    /* Print buffer contents (ring buffer) */
    // althought task->output.total_byte is increasing, but we use total to lock
    // the current output size
    uint32_t total = task->output.total_bytes;
    uint32_t buf_size = TASK_OUTPUT_BUF_SIZE;

    if (total == 0) {
        printf("(no output)\n");
        return 0;
    }

    /* Determine start position */
    uint32_t start_pos;
    uint32_t bytes_to_print;

    if (total <= buf_size) {
        /* Buffer not wrapped yet */
        start_pos = 0;
        bytes_to_print = total;
    } else {
        /* Buffer wrapped, start from write_pos (oldest data) */
        start_pos = task->output.write_pos;
        bytes_to_print = buf_size;
    }

    /* Print buffer contents */
    for (uint32_t i = 0; i < bytes_to_print; i++) {
        uint32_t pos = (start_pos + i) % buf_size;
        printf("%c", task->output.buffer[pos]);
    }

    printf("\n=== End of output (%u bytes) ===\n", total);
    return 0;
}

int cmd_heap(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    uint32_t total, used, free_size;
    heap_stats(&total, &used, &free_size);

    printf("Heap Memory Statistics\n");
    printf("----------------------\n");
    printf("Total:     %u MB (%u bytes)\n", total / (1024 * 1024), total);
    printf("Used:      %u KB (%u bytes)\n", used / 1024, used);
    printf("Free:      %u MB (%u bytes)\n", free_size / (1024 * 1024),
           free_size);
    printf("Usage:     %u%%\n", (used * 100) / total);

    return 0;
}

// when we want to kill a task that is running in background, we should use kill
// cmd, like kill 2

int cmd_kill(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: kill <task_id>\n");
        return -1;
    }

    /* Parse task ID */
    int tid = 0;
    char *p = argv[1];
    while (*p >= '0' && *p <= '9') {
        tid = tid * 10 + (*p - '0');
        p++;
    }

    if (tid < 0 || tid >= MAX_TASKS) {
        printf("Invalid task ID: %d\n", tid);
        return -1;
    }

    if (tid == 0) {
        printf("Cannot kill shell task\n");
        return -1;
    }

    /* Kill the task */
    if (task_kill(tid) < 0) {
        printf("Failed to kill task %d\n", tid);
        return -1;
    }

    printf("Task %d killed\n", tid);
    return 0;
}

// this cmd runs garbage collection on hdmi buffer
// user can chose to run it when necessary
// once called, it will free up space in hdmi buffer by removing old lines
// we should let this cmd run in background, and when there is no used hdmi
// buffer,it just call schedule(), so that it can yield cpu to other tasks

// how to yield cpu? just call schedule(NULL)?
// we have to trigger swi0 interrupt to yield cpu

int cmd_hdmi_buffer_gc(int argc, char *argv[]) {
    printf("hdmigc entered\n");
    printf("&display_start_row=%p, value=%d\n", &display_start_row,
           display_start_row);
    while (1) {
        // get current display_start_row,,
        // if it is already 0, we just yield cpu
        // or it is greater than 0, we should clear the lines above

        // but if terminal_reseted, we need to clean up until
        // shell_gc_pointer reaches display_start_row again

        // that is to say, unless shell_gc_pointer != display_start_row,
        // we need to clean.
        if (shell_gc_pointer < display_start_row ||
            shell_gc_pointer > display_start_row) {
            // clear one line
            int pixcel_row_start = shell_gc_pointer * TERMINAL_FONT_SIZE;
            int pixcel_row_end = pixcel_row_start + TERMINAL_FONT_SIZE;
            hdmi_clear_line(pixcel_row_start, pixcel_row_end, current_bg_color);
            shell_gc_pointer = (shell_gc_pointer + 1) % TERMINAL_TOTAL_ROWS;
        } else {
            // by calling task_yield(), we can yield cpu to other tasks
            task_yield();
            // by saving easy context, we can yield cpu to other tasks
            // there is a problem here, unsolved YET.
            // task_yield_simple();
        }
    }
    return 0;
}

// Mario game command - runs NES emulator
int cmd_mario(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

#ifdef QEMU_RUN
    extern unsigned char rom_mario_nes[];

    printf("==========================================\n");
    printf("  Super Mario Bros - LiteNES (QEMU)\n");
    printf("==========================================\n");
    printf("Controls:\n");
    printf("  W/A/S/D  - Move\n");
    printf("  J/K      - A/B\n");
    printf("  U/I      - Select/Start\n");
    printf("  Q or ESC - Quit emulator\n");
    printf("==========================================\n");

    ioe_init();
    if (fce_load_rom((char *)rom_mario_nes) < 0) {
        printf("ERROR: LiteNES ROM load failed.\n");
        return -1;
    }
    fce_init();
    fce_run();

    printf("\n[LiteNES] Exit to shell.\n");
    return 0;
#else
    // ROM data
    extern unsigned char rom_mario_nes[];
    extern unsigned int rom_mario_nes_len;

    printf("==========================================\n");
    printf("  Super Mario Bros - NES Hardware\n");
    printf("==========================================\n");
    printf("Controls:\n");
    printf("  W/A/S/D  - Move (Up/Left/Down/Right)\n");
    printf("  J        - Jump (A button)\n");
    printf("  K        - Run (B button)\n");
    printf("  U        - Select\n");
    printf("  I        - Start\n");
    printf("  ESC      - Exit (not implemented yet)\n");
    printf("==========================================\n\n");

    printf("Loading Mario ROM...\n");
    uint32_t mapper_flags = 0;
    if (nes_hw_load_rom((const uint8_t *)rom_mario_nes, rom_mario_nes_len, &mapper_flags) < 0) {
        printf("ERROR: Failed to load Mario ROM into Boot RAM!\n");
        return -1;
    }

    printf("Mapper flags: 0x%08x\n", mapper_flags);

    // Switch HDMI output to NES path
    hdmi_enable(1);
    hdmi_set_source(HDMI_SOURCE_NES);

    // Configure NES control
    bsp_nes_init();
    bsp_nes_set_mode(BSP_NES_MODE_PAUSE);
    bsp_nes_set_freq(BSP_NES_DIV_NTSC_APPROX);
    bsp_nes_set_mapper_flags(mapper_flags);
    nes_joypad_state = 0;
    bsp_nes_set_joypad(0);
    bsp_nes_set_start_pc(0x8000);

    // Reset pulse
    bsp_nes_set_reset(1);
    delay_cycles(2000);
    bsp_nes_set_reset(0);
    // Let the game continue after reset
    // only in this way we can ensure CPU can really fetch the start pc
    // after that we can clear the start_pc_valid flag
    delay_cycles(2000);
    bsp_nes_clear_start_pc_valid();

    // Run mode
    bsp_nes_set_mode(BSP_NES_MODE_RUN);
    printf("NES running based on the hardwore accellerator, you can kill this task to free loongarch CPU now.\n");

    while (1) {
        task_yield();
    }
#endif
}

// Change framebuffer command
int cmd_change(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: change <buffer>\n");
        printf("  buffer: A, B, or S\n");
        printf("  A - Buffer A (0x1F000000)\n");
        printf("  B - Buffer B (0x1F400000)\n");
        printf("  S - Buffer S (0x1F800000, Shell)\n");
        return -1;
    }

    char buffer_name = argv[1][0];
    enum BUFFER target_buffer;

    // 解析 buffer 参数
    if (buffer_name == 'A' || buffer_name == 'a') {
        target_buffer = BUFFER_A;
        printf("Switching to Buffer A...\n");
    } else if (buffer_name == 'B' || buffer_name == 'b') {
        target_buffer = BUFFER_B;
        printf("Switching to Buffer B...\n");
    } else if (buffer_name == 'S' || buffer_name == 's') {
        target_buffer = BUFFER_S;
        printf("Switching to Buffer S (Shell)...\n");
    } else {
        printf("Error: Invalid buffer '%c'\n", buffer_name);
        printf("Valid options: A, B, S\n");
        return -1;
    }

    // here, we change show and write buffer to the same target buffer
    // if the buffer is Shell buffer, it is OK
    // else the buffer is game buffer, it is also OK,casue the game will
    // handle the double buffer by itself
    hdmi_fb_show_base_set(target_buffer);
    hdmi_fb_write_base_set(target_buffer);

    printf("Framebuffer switched successfully!\n");
    return 0;
}

// HDMI source select command
int cmd_hdmi_src(int argc, char *argv[]) {
    if (argc < 2) {
        int src = hdmi_get_source();
        printf("Usage: hdmisrc <ddr|nes>\n");
        printf("Current source: %s\n", src ? "NES" : "DDR");
        return -1;
    }

    if (strcmp(argv[1], "ddr") == 0 || strcmp(argv[1], "0") == 0) {
        hdmi_enable(1);
        hdmi_set_source(HDMI_SOURCE_DDR);
        printf("HDMI source set to DDR framebuffer.\n");
        return 0;
    }

    if (strcmp(argv[1], "nes") == 0 || strcmp(argv[1], "1") == 0) {
        hdmi_enable(1);
        hdmi_set_source(HDMI_SOURCE_NES);
        printf("HDMI source set to NES framebuffer.\n");
        return 0;
    }

    printf("Invalid source: %s\n", argv[1]);
    printf("Usage: hdmisrc <ddr|nes>\n");
    return -1;
}

// Tetris game command
int cmd_tetris(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // 初始化游戏
    tetris_init();

    // 运行游戏
    tetris_run();

    return 0;
}
