/*
 * qemu_fb.c - QEMU bochs-display framebuffer driver
 * Simple driver for bochs-display PCI device in QEMU
 */

#ifdef QEMU_RUN

#include <qemu_fb.h>
#include <stdio.h>
#include <string.h>

// PCI configuration space access
#define PCI_ECAM_BASE       0x20000000

// PCI configuration registers
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_BAR0            0x10
#define PCI_BAR2            0x18

// Bochs display PCI IDs
#define BOCHS_VGA_VENDOR    0x1234
#define BOCHS_VGA_DEVICE    0x1111

// VBE registers offset in MMIO space
#define VBE_MMIO_OFFSET     0x500

// Global framebuffer info
static qemu_fb_info_t fb_info = {0};

// PCI ECAM address calculation
static inline uint32_t pci_ecam_addr(int bus, int dev, int func, int reg) {
    return PCI_ECAM_BASE | (bus << 20) | (dev << 15) | (func << 12) | reg;
}

// Read PCI config register
static uint32_t pci_read32(int bus, int dev, int func, int reg) {
    volatile uint32_t *addr = (volatile uint32_t *)pci_ecam_addr(bus, dev, func, reg);
    return *addr;
}

static uint16_t pci_read16(int bus, int dev, int func, int reg) {
    volatile uint16_t *addr = (volatile uint16_t *)pci_ecam_addr(bus, dev, func, reg);
    return *addr;
}

// Write PCI config register
static void pci_write32(int bus, int dev, int func, int reg, uint32_t val) {
    volatile uint32_t *addr = (volatile uint32_t *)pci_ecam_addr(bus, dev, func, reg);
    *addr = val;
}

static void pci_write16(int bus, int dev, int func, int reg, uint16_t val) {
    volatile uint16_t *addr = (volatile uint16_t *)pci_ecam_addr(bus, dev, func, reg);
    *addr = val;
}

// VBE register access - bochs-display maps registers directly
static void vbe_write(uint16_t index, uint16_t value) {
    volatile uint16_t *reg = (volatile uint16_t *)(fb_info.mmio_addr + VBE_MMIO_OFFSET + index * 2);
    *reg = value;
}

static uint16_t vbe_read(uint16_t index) {
    volatile uint16_t *reg = (volatile uint16_t *)(fb_info.mmio_addr + VBE_MMIO_OFFSET + index * 2);
    return *reg;
}

// Scan PCI bus for bochs-display device
static int pci_find_bochs_display(int *out_bus, int *out_dev) {
    for (int bus = 0; bus < 1; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_read16(bus, dev, 0, PCI_VENDOR_ID);
            uint16_t device = pci_read16(bus, dev, 0, PCI_DEVICE_ID);

            if (vendor == BOCHS_VGA_VENDOR && device == BOCHS_VGA_DEVICE) {
                *out_bus = bus;
                *out_dev = dev;
                return 0;
            }
        }
    }
    return -1;
}

// Initialize QEMU framebuffer
int qemu_fb_init(void) {
    // 防止重复初始化
    if (fb_info.initialized) {
        printf("[QEMU_FB] Already initialized\n");
        return 0;
    }

    int bus, dev;

    printf("[QEMU_FB] Scanning PCI bus...\n");

    if (pci_find_bochs_display(&bus, &dev) < 0) {
        printf("[QEMU_FB] bochs-display not found\n");
        return -1;
    }

    printf("[QEMU_FB] Found bochs-display at %d:%d\n", bus, dev);

    // Get raw BAR values
    uint32_t bar0_raw = pci_read32(bus, dev, 0, PCI_BAR0);
    uint32_t bar2_raw = pci_read32(bus, dev, 0, PCI_BAR2);

    // If BAR not assigned, assign manually
    if ((bar0_raw & ~0xF) == 0) {
        pci_write32(bus, dev, 0, PCI_BAR0, 0x40000000);
    }
    if ((bar2_raw & ~0xF) == 0) {
        pci_write32(bus, dev, 0, PCI_BAR2, 0x41000000);
    }

    // Enable memory access
    uint16_t cmd = pci_read16(bus, dev, 0, PCI_COMMAND);
    pci_write16(bus, dev, 0, PCI_COMMAND, cmd | 0x03);

    // Get final BAR values
    uint32_t bar0 = pci_read32(bus, dev, 0, PCI_BAR0) & ~0xF;
    uint32_t bar2 = pci_read32(bus, dev, 0, PCI_BAR2) & ~0xF;

    fb_info.fb_addr = bar0;
    fb_info.mmio_addr = bar2;

    printf("[QEMU_FB] FB=%08x MMIO=%08x\n", bar0, bar2);

    // Check VBE ID
    uint16_t vbe_id = vbe_read(VBE_DISPI_INDEX_ID);
    printf("[QEMU_FB] VBE ID=%04x\n", vbe_id);

    // Set resolution
    qemu_fb_set_resolution(640, 480);

    fb_info.initialized = true;
    return 0;
}

// Set display resolution
int qemu_fb_set_resolution(uint16_t width, uint16_t height) {
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    vbe_write(VBE_DISPI_INDEX_XRES, width);
    vbe_write(VBE_DISPI_INDEX_YRES, height);
    vbe_write(VBE_DISPI_INDEX_BPP, 32);
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    fb_info.width = width;
    fb_info.height = height;
    fb_info.bpp = 32;
    fb_info.pitch = width * 4;

    printf("[QEMU_FB] Resolution: %dx%d\n", width, height);
    return 0;
}

// Get framebuffer pointer
volatile uint32_t* qemu_fb_get_pointer(void) {
    return (volatile uint32_t *)fb_info.fb_addr;
}

// Get framebuffer info
qemu_fb_info_t* qemu_fb_get_info(void) {
    return &fb_info;
}

// Draw pixel
void qemu_fb_draw_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= fb_info.width || y < 0 || y >= fb_info.height)
        return;
    volatile uint32_t *fb = (volatile uint32_t *)fb_info.fb_addr;
    fb[y * fb_info.width + x] = color;
}

// Clear screen
void qemu_fb_clear(uint32_t color) {
    volatile uint32_t *fb = (volatile uint32_t *)fb_info.fb_addr;
    int size = fb_info.width * fb_info.height;
    for (int i = 0; i < size; i++) {
        fb[i] = color;
    }
}

// Fill rectangle
void qemu_fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    volatile uint32_t *fb = (volatile uint32_t *)fb_info.fb_addr;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= fb_info.height) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= fb_info.width) continue;
            fb[yy * fb_info.width + xx] = color;
        }
    }
}

// Blit buffer to framebuffer
void qemu_fb_blit(int x, int y, uint32_t *pixels, int w, int h) {
    volatile uint32_t *fb = (volatile uint32_t *)fb_info.fb_addr;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= fb_info.height) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= fb_info.width) continue;
            fb[yy * fb_info.width + xx] = pixels[j * w + i];
        }
    }
}

#endif // QEMU_RUN
