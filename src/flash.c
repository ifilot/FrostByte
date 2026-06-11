#include "flash.h"

/**
 * Return board identification string
 **/
void write_board_id() {
    static const char* board_id = BOARD_ID;
    tud_cdc_write(board_id, 16);
    tud_cdc_write_flush();
}

/**
 * @brief Read JEDEC id of W25Q16JVSNIQ
 * 
 * Outputs 3-byte result to UART
 */
void flash_read_jedec_id() {
    uint8_t cmd = FLASH_JEDEC_ID;
    uint8_t response[3] = {0};

    flash_cs_select();
    spi_write_blocking(SPI_PORT, &cmd, 1);
    spi_read_blocking(SPI_PORT, 0x00, response, 3);
    flash_cs_deselect();

    tud_cdc_write_char(response[0]);
    tud_cdc_write_char(response[1]);
    tud_cdc_write_char(response[2]);
    tud_cdc_write_flush();
}

/**
 * @brief Erase a 64 KiB block on the chip
 * 
 * @param block 64 KiB block-increment
 */
void flash_erase_block_64k(uint8_t block) {
    flash_write_enable();

    uint8_t cmd[] = {
        FLASH_CMD_ERASE_BLOCK_64K,
        block,
        0x00,
        0x00
    };

    flash_cs_select();
    spi_write_blocking(SPI_PORT, cmd, 4);
    flash_cs_deselect();

    flash_wait_busy();
    tud_cdc_write_char(block);
    tud_cdc_write_flush();
}

/**
 * @brief Resets the W25Q16JVSNIQ chip
 * 
 */
void flash_reset() {
    uint8_t cmd;
    flash_cs_select();
    cmd = FLASH_CMD_ENABLE_RESET;
    spi_write_blocking(SPI_PORT, &cmd, 1);
    flash_cs_deselect();
    flash_cs_select();
    cmd = FLASH_CMD_RESET;
    spi_write_blocking(SPI_PORT, &cmd, 1);
    flash_cs_deselect();
    sleep_ms(1); // Let it recover
}

/**
 * @brief Check whether a 64 KiB block has been correctly erased
 * 
 * @param block 64 KiB block increment block
 * 
 * Outputs 0x00 when succesfully erased, 0xFF otherwise
 */
void flash_check_block_erased(uint8_t block) {
    uint32_t address = block * BLOCK_SIZE;
    uint8_t buffer[BUFFER_SIZE];

    for (uint32_t offset = 0; offset < BLOCK_SIZE; offset += BUFFER_SIZE) {
        flash_read_page_256(address + offset, buffer);

        for (int i = 0; i < BUFFER_SIZE; i++) {
            if (buffer[i] != 0xFF) {
                tud_cdc_write_char(0xFF);
                tud_cdc_write_flush();
                return;
            }
        }
    }

    tud_cdc_write_char(0x00);
    tud_cdc_write_flush();
}

/**
 * @brief Read a single 256-byte page from chip
 * 
 * @param page page index
 * 
 * Outputs 256 bytes over UART
 */
void flash_read_page(uint16_t page) {
    uint32_t address = (uint32_t)page * 256;
    uint8_t buffer[256];
    flash_read_page_256(address, buffer);

    for (unsigned int i = 0; i < 16; i++) {
        // Wait until space is available
        while (tud_cdc_write_available() < 16) {
            tud_task();  // Run USB stack
        }

        tud_cdc_write(&buffer[i * 16], 16);
        tud_task();  // Let USB process the transfer
    }

    tud_cdc_write_flush();
    tud_task();  // Ensure flush is processed
}

/**
 * @brief Write a 4096-byte sector to flash chip
 * 
 * @param sector 4096-byte sector increment
 */
void flash_write_sector(uint8_t sector) {
    uint32_t bitsread = 0;
    uint8_t buffer[SECTOR_SIZE];

    // Receive exactly SECTOR_SIZE bytes over USB CDC
    while (bitsread < SECTOR_SIZE) {
        uint32_t available = tud_cdc_available();
        if (available > 0) {
            uint32_t to_read = SECTOR_SIZE - bitsread;
            if (to_read > available) to_read = available;
            bitsread += tud_cdc_read(&buffer[bitsread], to_read);
        }
        tud_task();
    }

    uint32_t base_addr = sector * SECTOR_SIZE;

    // Loop through 256-byte pages
    for (uint32_t offset = 0; offset < SECTOR_SIZE; offset += 256) {
        flash_write_enable();

        uint32_t addr = base_addr + offset;
        uint8_t cmd[4] = {
            0x02,
            (addr >> 16) & 0xFF,
            (addr >> 8) & 0xFF,
            addr & 0xFF
        };

        flash_cs_select();
        spi_write_blocking(SPI_PORT, cmd, 4);
        spi_write_blocking(SPI_PORT, &buffer[offset], 256);
        flash_cs_deselect();

        flash_wait_busy();
    }

    // Return CRC16 of written data
    uint16_t crc16checksum = crc16_xmodem(buffer, SECTOR_SIZE);
    tud_cdc_write_char(crc16checksum & 0xFF);
    tud_cdc_write_char((crc16checksum >> 8) & 0xFF);
    tud_cdc_write_flush();
    tud_task();
}

//==========================================================
// AUXILIARY FUNCTIONS
//==========================================================

/**
 * @brief Select the W25Q16JVSNIQ by pulling its /CS low
 * 
 */
void flash_cs_select() {
    gpio_put(PIN_SEL, 0);
}

/**
 * @brief Deselect the W25Q16JVSNIQ by pulling its /CS high
 * 
 */
void flash_cs_deselect() {
    gpio_put(PIN_SEL, 1);
}

/**
 * @brief Enables write instructions for W25Q16JVSNIQ
 * 
 */
void flash_write_enable() {
    flash_cs_select();
    uint8_t cmd = FLASH_CMD_WRITE_ENABLE;
    spi_write_blocking(SPI_PORT, &cmd, 1);
    flash_cs_deselect();
}

/**
 * @brief Low-level read page function
 * 
 * @param address starting address
 * @param buffer 256-byte storage buffer
 */
void flash_read_page_256(uint32_t address, uint8_t *buffer) {
    uint8_t cmd[4] = {
        0x03, // READ DATA command
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    };

    flash_cs_select();
    spi_write_blocking(SPI_PORT, cmd, 4);
    spi_read_blocking(SPI_PORT, 0x00, buffer, 256);
    flash_cs_deselect();
}

/**
 * @brief Waits until chip becomes available again
 * 
 */
void flash_wait_busy() {
    uint8_t status;
    const int timeout_ms = 1000;
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    do {
        flash_cs_select();
        uint8_t cmd = FLASH_CMD_READ_STATUS;
        spi_write_blocking(SPI_PORT, &cmd, 1);
        spi_read_blocking(SPI_PORT, 0x00, &status, 1);
        flash_cs_deselect();
        sleep_ms(1);
    } while ((status & FLASH_STATUS_BUSY) && !time_reached(deadline));
}

/**
 * @brief Generates CRC16 XMODEM checksum for data
 * 
 */
uint16_t crc16_xmodem(uint8_t *data, uint16_t length) {
    uint32_t crc = 0;
    static const uint16_t poly = 0x1021;

    for(uint16_t i=0; i<length; i++) {
      crc = crc ^ (data[i] << 8);
      for (uint8_t j=0; j<8; j++) {
        crc = crc << 1;
        if (crc & 0x10000) {
            crc = (crc ^ poly) & 0xFFFF;
        }
      }
    }

    return (uint16_t)crc;
}