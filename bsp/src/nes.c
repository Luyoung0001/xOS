#include <nes.h>

static uint32_t nes_ctrl_shadow = 0;

#ifdef QEMU_RUN
/*
 * QEMU virt does not provide the custom NES accelerator registers mapped at
 * 0x1FD0F0xx. Keep a software shadow so higher layers can run without faults.
 */
static uint32_t nes_status_shadow = 0;
static uint32_t nes_freq_shadow = 0;
static uint32_t nes_start_pc_shadow = 0;
static uint32_t nes_mapper_shadow = 0;
static uint32_t nes_joypad_shadow = 0;

static void qemu_sync_status(void) {
    if ((nes_ctrl_shadow & NES_CTRL_MODE_MASK) == NES_CTRL_MODE_RUN) {
        nes_status_shadow |= NES_STATUS_RUNNING;
    } else {
        nes_status_shadow &= ~NES_STATUS_RUNNING;
    }
}
#endif

void bsp_nes_init(void) {
    nes_ctrl_shadow = 0;
#ifdef QEMU_RUN
    nes_status_shadow = 0;
    nes_freq_shadow = 0;
    nes_start_pc_shadow = 0;
    nes_mapper_shadow = 0;
    nes_joypad_shadow = 0;
#else
    NES_CTRL_REG = nes_ctrl_shadow;
#endif
}

void bsp_nes_set_mode(bsp_nes_mode_t mode) {
    nes_ctrl_shadow = (nes_ctrl_shadow & ~NES_CTRL_MODE_MASK) |
                      ((uint32_t)mode & NES_CTRL_MODE_MASK);
#ifdef QEMU_RUN
    qemu_sync_status();
#else
    NES_CTRL_REG = nes_ctrl_shadow;
#endif
}

void bsp_nes_step_once(void) {
    uint32_t val = (nes_ctrl_shadow & ~NES_CTRL_MODE_MASK) | NES_CTRL_MODE_STEP;
#ifdef QEMU_RUN
    nes_ctrl_shadow =
        val & (NES_CTRL_MODE_MASK | NES_CTRL_RESET | NES_CTRL_STEP_IRQ_EN);
    nes_status_shadow |= NES_STATUS_STEP_DONE;
    if (nes_ctrl_shadow & NES_CTRL_STEP_IRQ_EN) {
        nes_status_shadow |= NES_STATUS_IRQ_PENDING;
    }
    qemu_sync_status();
#else
    NES_CTRL_REG = val;
    nes_ctrl_shadow =
        val & (NES_CTRL_MODE_MASK | NES_CTRL_RESET | NES_CTRL_STEP_IRQ_EN);
#endif
}

void bsp_nes_set_reset(int on) {
    if (on) {
        nes_ctrl_shadow |= NES_CTRL_RESET;
    } else {
        nes_ctrl_shadow &= ~NES_CTRL_RESET;
    }
#ifdef QEMU_RUN
    qemu_sync_status();
#else
    NES_CTRL_REG = nes_ctrl_shadow;
#endif
}

void bsp_nes_set_freq(uint16_t div) {
#ifdef QEMU_RUN
    nes_freq_shadow = (uint32_t)div;
#else
    NES_FREQ_REG = (uint32_t)div;
#endif
}

void bsp_nes_set_start_pc(uint16_t pc) {
#ifdef QEMU_RUN
    nes_start_pc_shadow = (uint32_t)pc;
#else
    NES_START_PC_REG = (uint32_t)pc;
#endif
}

void bsp_nes_clear_start_pc_valid(void) {
#ifdef QEMU_RUN
    (void)nes_start_pc_shadow;
#else
    NES_CTRL_REG = nes_ctrl_shadow | NES_CTRL_CLR_PC_VALID;
#endif
}

void bsp_nes_irq_enable(int on) {
    if (on) {
        nes_ctrl_shadow |= NES_CTRL_STEP_IRQ_EN;
    } else {
        nes_ctrl_shadow &= ~NES_CTRL_STEP_IRQ_EN;
    }
#ifdef QEMU_RUN
    qemu_sync_status();
#else
    NES_CTRL_REG = nes_ctrl_shadow;
#endif
}

void bsp_nes_set_mapper_flags(uint32_t flags) {
#ifdef QEMU_RUN
    nes_mapper_shadow = flags;
#else
    NES_MAPPER_REG = flags;
#endif
}

void bsp_nes_set_joypad(uint8_t state) {
#ifdef QEMU_RUN
    nes_joypad_shadow = (uint32_t)state;
#else
    NES_JOYPAD_REG = (uint32_t)state;
#endif
}

uint32_t bsp_nes_read_status(void) {
#ifdef QEMU_RUN
    uint32_t status = nes_status_shadow;
    /* Match hardware semantics: reading status clears step/irq pending bits. */
    nes_status_shadow &= ~(NES_STATUS_STEP_DONE | NES_STATUS_IRQ_PENDING);
    return status;
#else
    return NES_STATUS_REG;
#endif
}

void bsp_nes_clear_step_irq(void) {
#ifdef QEMU_RUN
    nes_status_shadow &= ~NES_STATUS_IRQ_PENDING;
#else
    NES_CTRL_REG = nes_ctrl_shadow | NES_CTRL_CLR_STEP_IRQ;
#endif
}

void bsp_nes_wait_step_done(void) {
#ifdef QEMU_RUN
    nes_status_shadow |= NES_STATUS_STEP_DONE;
    (void)bsp_nes_read_status();
#else
    while ((bsp_nes_read_status() & NES_STATUS_STEP_DONE) == 0) {
        // spin
    }
#endif
}
