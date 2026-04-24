#include <sd.h>

//=============================================================================
// Register Access Macros
//=============================================================================
#define REG_READ(addr)        (*(volatile uint32_t *)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(addr) = (val))

//=============================================================================
// Timeout Constants
//=============================================================================
#define SD_INIT_TIMEOUT    10000000  // 10M cycles (~200ms at 50MHz)
#define SD_READ_TIMEOUT    5000000   // 5M cycles (~100ms at 50MHz)

//=============================================================================
// SD Card Driver Implementation
//=============================================================================

/**
 * @brief Initialize SD card driver and wait for hardware initialization
 * @return 0 on success, -1 on timeout/error
 */
int sd_init(void) {
    uint32_t timeout = SD_INIT_TIMEOUT;
    uint32_t status;

    // Wait for SD card initialization to complete
    while (timeout--) {
        status = REG_READ(SD_STATUS_REG);
        if (status & SD_STATUS_INIT_DONE) {
            return 0;  // Success
        }
    }

    return -1;  // Timeout
}

/**
 * @brief Check if SD card is initialized
 * @return 1 if initialized, 0 if not
 */
int sd_is_init(void) {
    uint32_t status = REG_READ(SD_STATUS_REG);
    return (status & SD_STATUS_INIT_DONE) ? 1 : 0;
}

/**
 * @brief Check if SD card is busy
 * @return 1 if busy, 0 if idle
 */
int sd_is_busy(void) {
    uint32_t status = REG_READ(SD_STATUS_REG);
    return (status & SD_STATUS_BUSY) ? 1 : 0;
}

/**
 * @brief Read a single sector (512 bytes) from SD card
 * @param sector_addr Sector address (0-based)
 * @param buffer Output buffer (must be at least 512 bytes)
 * @return 0 on success, -1 on error
 */
int sd_read_sector(uint32_t sector_addr, uint8_t *buffer) {
    uint32_t timeout;
    uint32_t status;
    int i;

    // Check if SD card is initialized
    if (!sd_is_init()) {
        return -1;
    }

    // Wait for SD card to be idle
    timeout = SD_READ_TIMEOUT;
    while (sd_is_busy() && timeout--) {
        // Wait
    }
    if (timeout == 0) {
        return -1;  // Timeout waiting for idle
    }

    // Write sector address
    REG_WRITE(SD_SEC_ADDR_REG, sector_addr);

    // Start read operation
    REG_WRITE(SD_CTRL_REG, 0x1);  // Set bit0 to start read

    // Read 512 bytes
    for (i = 0; i < SD_SECTOR_SIZE; i++) {
        // Wait for data valid
        timeout = SD_READ_TIMEOUT;
        while (timeout--) {
            status = REG_READ(SD_STATUS_REG);
            if (status & SD_STATUS_DATA_VALID) {
                break;
            }
        }
        if (timeout == 0) {
            return -1;  // Timeout waiting for data
        }

        // Read data byte
        buffer[i] = (uint8_t)REG_READ(SD_DATA_REG);
    }

    // Wait for read done
    timeout = SD_READ_TIMEOUT;
    while (timeout--) {
        status = REG_READ(SD_STATUS_REG);
        if (status & SD_STATUS_READ_DONE) {
            break;
        }
    }
    if (timeout == 0) {
        return -1;  // Timeout waiting for read done
    }

    return 0;  // Success
}

/**
 * @brief Read multiple sectors from SD card
 * @param start_sector Starting sector address
 * @param num_sectors Number of sectors to read
 * @param buffer Output buffer (must be at least num_sectors * 512 bytes)
 * @return 0 on success, -1 on error
 */
int sd_read_sectors(uint32_t start_sector, uint32_t num_sectors, uint8_t *buffer) {
    uint32_t i;
    int result;

    for (i = 0; i < num_sectors; i++) {
        result = sd_read_sector(start_sector + i, buffer + i * SD_SECTOR_SIZE);
        if (result != 0) {
            return -1;  // Error reading sector
        }
    }

    return 0;  // Success
}

/**
 * @brief Read arbitrary number of bytes from SD card
 * @param byte_offset Byte offset in SD card
 * @param buffer Output buffer
 * @param length Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
int sd_read(uint32_t byte_offset, uint8_t *buffer, uint32_t length) {
    uint32_t start_sector = byte_offset / SD_SECTOR_SIZE;
    uint32_t offset_in_sector = byte_offset % SD_SECTOR_SIZE;
    uint32_t bytes_read = 0;
    uint8_t sector_buffer[SD_SECTOR_SIZE];
    int result;

    // Handle first partial sector
    if (offset_in_sector != 0) {
        result = sd_read_sector(start_sector, sector_buffer);
        if (result != 0) {
            return -1;
        }

        uint32_t bytes_to_copy = SD_SECTOR_SIZE - offset_in_sector;
        if (bytes_to_copy > length) {
            bytes_to_copy = length;
        }

        // Copy data from sector buffer to output buffer
        for (uint32_t i = 0; i < bytes_to_copy; i++) {
            buffer[bytes_read++] = sector_buffer[offset_in_sector + i];
        }

        length -= bytes_to_copy;
        start_sector++;
    }

    // Read full sectors
    while (length >= SD_SECTOR_SIZE) {
        result = sd_read_sector(start_sector, buffer + bytes_read);
        if (result != 0) {
            return -1;
        }

        bytes_read += SD_SECTOR_SIZE;
        length -= SD_SECTOR_SIZE;
        start_sector++;
    }

    // Handle last partial sector
    if (length > 0) {
        result = sd_read_sector(start_sector, sector_buffer);
        if (result != 0) {
            return -1;
        }

        for (uint32_t i = 0; i < length; i++) {
            buffer[bytes_read++] = sector_buffer[i];
        }
    }

    return bytes_read;
}
