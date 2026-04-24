#ifndef SD_H
#define SD_H

#include <stdint.h>

//=============================================================================
// SD Card Register Addresses (Memory-Mapped)
//=============================================================================
#define SD_CTRL_REG      0x1FD0F070  // SD control register (write, bit0=start_read)
#define SD_STATUS_REG    0x1FD0F074  // SD status register (read-only)
#define SD_SEC_ADDR_REG  0x1FD0F078  // SD sector address register (read/write)
#define SD_DATA_REG      0x1FD0F07C  // SD data register (read-only, 8-bit)

//=============================================================================
// SD Card Status Bits
//=============================================================================
#define SD_STATUS_INIT_DONE   (1 << 0)  // Initialization complete
#define SD_STATUS_BUSY        (1 << 1)  // SD card busy (read/write in progress)
#define SD_STATUS_DATA_VALID  (1 << 2)  // Read data valid
#define SD_STATUS_READ_DONE   (1 << 3)  // Read operation complete

//=============================================================================
// SD Card Constants
//=============================================================================
#define SD_SECTOR_SIZE   512   // Bytes per sector (standard for SD cards)
#define SD_BLOCK_SIZE    512   // Alias for sector size

//=============================================================================
// SD Card Driver Functions
//=============================================================================

/**
 * @brief Initialize SD card driver and wait for hardware initialization
 * @return 0 on success, -1 on timeout/error
 */
int sd_init(void);

/**
 * @brief Check if SD card is initialized
 * @return 1 if initialized, 0 if not
 */
int sd_is_init(void);

/**
 * @brief Check if SD card is busy
 * @return 1 if busy, 0 if idle
 */
int sd_is_busy(void);

/**
 * @brief Read a single sector (512 bytes) from SD card
 * @param sector_addr Sector address (0-based)
 * @param buffer Output buffer (must be at least 512 bytes)
 * @return 0 on success, -1 on error
 */
int sd_read_sector(uint32_t sector_addr, uint8_t *buffer);

/**
 * @brief Read multiple sectors from SD card
 * @param start_sector Starting sector address
 * @param num_sectors Number of sectors to read
 * @param buffer Output buffer (must be at least num_sectors * 512 bytes)
 * @return 0 on success, -1 on error
 */
int sd_read_sectors(uint32_t start_sector, uint32_t num_sectors, uint8_t *buffer);

/**
 * @brief Read arbitrary number of bytes from SD card
 * @param byte_offset Byte offset in SD card
 * @param buffer Output buffer
 * @param length Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
int sd_read(uint32_t byte_offset, uint8_t *buffer, uint32_t length);

#endif // SD_H
