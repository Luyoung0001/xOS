#ifndef QEMU_FB_H
#define QEMU_FB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef QEMU_RUN

// QEMU virt machine PCI configuration
#define PCI_ECAM_BASE       0x20000000
#define PCI_MMIO_BASE       0x40000000

// Bochs VBE display registers (offset from MMIO BAR2)
#define VBE_DISPI_INDEX_ID          0x00
#define VBE_DISPI_INDEX_XRES        0x01
#define VBE_DISPI_INDEX_YRES        0x02
#define VBE_DISPI_INDEX_BPP         0x03
#define VBE_DISPI_INDEX_ENABLE      0x04
#define VBE_DISPI_INDEX_BANK        0x05
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x06
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x07
#define VBE_DISPI_INDEX_X_OFFSET    0x08
#define VBE_DISPI_INDEX_Y_OFFSET    0x09

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_LFB_ENABLED       0x40

// QEMU framebuffer info
typedef struct {
    uint32_t fb_addr;       // Framebuffer physical address
    uint32_t mmio_addr;     // MMIO registers address
    uint16_t width;
    uint16_t height;
    uint16_t bpp;           // Bits per pixel (32)
    uint32_t pitch;         // Bytes per line
    bool initialized;
} qemu_fb_info_t;

// Initialize QEMU framebuffer (bochs-display)
int qemu_fb_init(void);

// Get framebuffer pointer
volatile uint32_t* qemu_fb_get_pointer(void);

// Get framebuffer info
qemu_fb_info_t* qemu_fb_get_info(void);

// Set display resolution
int qemu_fb_set_resolution(uint16_t width, uint16_t height);

// Draw pixel (ARGB format)
void qemu_fb_draw_pixel(int x, int y, uint32_t color);

// Fill rectangle
void qemu_fb_fill_rect(int x, int y, int w, int h, uint32_t color);

// Clear screen
void qemu_fb_clear(uint32_t color);

// Blit buffer to framebuffer
void qemu_fb_blit(int x, int y, uint32_t *pixels, int w, int h);

#endif // QEMU_RUN

#endif // QEMU_FB_H
