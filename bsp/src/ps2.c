/*------------------------------------------------------------------------------
 * PS2 Keyboard Driver Implementation
 *
 * Board Support Package (BSP)
 *----------------------------------------------------------------------------*/

#include <ps2.h>

/*============================================================================
 * Scancode to ASCII Lookup Table (Scan Code Set 2, US Layout)
 *============================================================================*/
static const char scancode_table[128] = {
    /*0x00*/ 0,    0,    0,    0,    0,    0,    0,    0,
    /*0x08*/ 0,    0,    0,    0,    0,    '\t', '`',  0,
    /*0x10*/ 0,    0,    0,    0,    0,    'q',  '1',  0,
    /*0x18*/ 0,    0,    'z',  's',  'a',  'w',  '2',  0,
    /*0x20*/ 0,    'c',  'x',  'd',  'e',  '4',  '3',  0,
    /*0x28*/ 0,    ' ',  'v',  'f',  't',  'r',  '5',  0,
    /*0x30*/ 0,    'n',  'b',  'h',  'g',  'y',  '6',  0,
    /*0x38*/ 0,    0,    'm',  'j',  'u',  '7',  '8',  0,
    /*0x40*/ 0,    ',',  'k',  'i',  'o',  '0',  '9',  0,
    /*0x48*/ 0,    '.',  '/',  'l',  ';',  'p',  '-',  0,
    /*0x50*/ 0,    0,    '\'', 0,    '[',  '=',  0,    0,
    /*0x58*/ 0,    0,    '\n', ']',  0,    '\\', 0,    0,
    /*0x60*/ 0,    0,    0,    0,    0,    0,    '\b', 0,
    /*0x68*/ 0,    '1',  0,    '4',  '7',  0,    0,    0,
    /*0x70*/ '0',  '.',  '2',  '5',  '6',  '8',  0x1B, 0,
    /*0x78*/ 0,    '+',  '3',  '-',  '*',  '9',  0,    0,
};

/*============================================================================
 * API Implementations
 *============================================================================*/

int bsp_ps2_init(int enable_int) {
#ifdef QEMU_RUN
    (void)enable_int;
    return 0;  // QEMU 模式下不初始化 PS2
#else
    uint32_t ctrl = PS2_CTRL_ENABLE;

    if (enable_int) {
        ctrl |= PS2_CTRL_INT_ENABLE;
    }
    PS2_CTRL_REG = ctrl;
    return 0;
#endif
}

int bsp_ps2_data_available(void) {
#ifdef QEMU_RUN
    return 0;  // QEMU 模式下 PS2 不可用
#else
    return (PS2_STATUS_REG & PS2_STATUS_VALID) != 0;
#endif
}

int bsp_ps2_read(void) {
#ifdef QEMU_RUN
    return -1;  // QEMU 模式下无数据
#else
    if (PS2_STATUS_REG & PS2_STATUS_VALID) {
        return (int)(PS2_DATA_REG & 0xFF);
    }
    return -1;
#endif
}

uint8_t bsp_ps2_read_blocking(void) {
#ifdef QEMU_RUN
    return 0;  // QEMU 模式下返回 0
#else
    while (!(PS2_STATUS_REG & PS2_STATUS_VALID)) {
        /* Wait for data */
    }
    return (uint8_t)PS2_DATA_REG;
#endif
}

uint32_t bsp_ps2_get_errors(void) {
#ifdef QEMU_RUN
    return 0;
#else
    return PS2_STATUS_REG & (PS2_STATUS_PARITY_ERR |
                            PS2_STATUS_FRAME_ERR |
                            PS2_STATUS_OVERFLOW);
#endif
}

void bsp_ps2_clear_errors(void) {
#ifndef QEMU_RUN
    uint32_t ctrl = PS2_CTRL_REG;
    PS2_CTRL_REG = ctrl | PS2_CTRL_CLEAR_ERR;
#endif
}

void bsp_ps2_enable(int enable) {
#ifndef QEMU_RUN
    if (enable) {
        PS2_CTRL_REG |= PS2_CTRL_ENABLE;
    } else {
        PS2_CTRL_REG &= ~PS2_CTRL_ENABLE;
    }
#endif
    (void)enable;
}

void bsp_ps2_int_enable(int enable) {
#ifndef QEMU_RUN
    if (enable) {
        PS2_CTRL_REG |= PS2_CTRL_INT_ENABLE;
    } else {
        PS2_CTRL_REG &= ~PS2_CTRL_INT_ENABLE;
    }
#endif
    (void)enable;
}

char bsp_ps2_to_ascii(uint8_t scancode) {
    if (scancode < 128) {
        return scancode_table[scancode];
    }
    return 0;
}


