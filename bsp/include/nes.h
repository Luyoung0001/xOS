#ifndef __BSP_NES_H__
#define __BSP_NES_H__

#include <stdint.h>

// NES control registers (confreg base)
#define NES_CONF_BASE        0x1FD00000UL
#define NES_CTRL_REG         (*(volatile uint32_t *)(NES_CONF_BASE + 0xF080))
#define NES_STATUS_REG       (*(volatile uint32_t *)(NES_CONF_BASE + 0xF084))
#define NES_START_PC_REG     (*(volatile uint32_t *)(NES_CONF_BASE + 0xF088))
#define NES_FREQ_REG         (*(volatile uint32_t *)(NES_CONF_BASE + 0xF08C))
#define NES_MAPPER_REG       (*(volatile uint32_t *)(NES_CONF_BASE + 0xF090))
#define NES_JOYPAD_REG       (*(volatile uint32_t *)(NES_CONF_BASE + 0xF094))

// Clock/Divider helpers (aclk = 62.5 MHz)
// NES CPU frequency = NES_ACLK_HZ / (3 * (div + 1))
#define BSP_NES_ACLK_HZ          62500000UL
#define BSP_NES_DIV_MAX          0u
#define BSP_NES_CPU_HZ(div)      (BSP_NES_ACLK_HZ / (3u * ((div) + 1u)))
#define BSP_NES_CPU_HZ_MAX       BSP_NES_CPU_HZ(BSP_NES_DIV_MAX)

// Closest integer divider to 1.7897725 MHz with 62.5 MHz aclk
// div=11 -> CPU ≈ 1.736111 MHz (low by ~3.0%)
#define BSP_NES_DIV_NTSC_APPROX  11u
#define BSP_NES_CPU_HZ_NTSC_APPROX BSP_NES_CPU_HZ(BSP_NES_DIV_NTSC_APPROX)

// Mapper flags helper (matches NES_Nexys4 loader format)
// Mario (NROM-256, vertical mirroring): 0x00004100
#define BSP_NES_MAPPER_FLAGS_MARIO  0x00004100u

// NES_CTRL bit definitions
#define NES_CTRL_MODE_MASK       0x3
#define NES_CTRL_MODE_PAUSE      0x0
#define NES_CTRL_MODE_RUN        0x1
#define NES_CTRL_MODE_STEP       0x2
#define NES_CTRL_RESET           (1u << 2)
#define NES_CTRL_CLR_PC_VALID    (1u << 3)
#define NES_CTRL_STEP_IRQ_EN     (1u << 4)
#define NES_CTRL_CLR_STEP_IRQ    (1u << 5)

// NES_STATUS bit definitions (read clears step_done/irq)
#define NES_STATUS_STEP_DONE     (1u << 0)
#define NES_STATUS_IRQ_PENDING   (1u << 1)
#define NES_STATUS_MODE_MASK     (0x3u << 2)
#define NES_STATUS_RUNNING       (1u << 4)

typedef enum {
    BSP_NES_MODE_PAUSE = 0,
    BSP_NES_MODE_RUN   = 1,
    BSP_NES_MODE_STEP  = 2
} bsp_nes_mode_t;

// Initialize control shadow and put NES into a known state (pause, no reset).
void bsp_nes_init(void);

// Control functions
void bsp_nes_set_mode(bsp_nes_mode_t mode);
void bsp_nes_step_once(void);
void bsp_nes_set_reset(int on);
void bsp_nes_set_freq(uint16_t div);
void bsp_nes_set_start_pc(uint16_t pc);
void bsp_nes_clear_start_pc_valid(void);
void bsp_nes_irq_enable(int on);
void bsp_nes_set_mapper_flags(uint32_t flags);
void bsp_nes_set_joypad(uint8_t state);

// Status / interrupt handling
uint32_t bsp_nes_read_status(void);
void bsp_nes_clear_step_irq(void);
void bsp_nes_wait_step_done(void);

#endif
