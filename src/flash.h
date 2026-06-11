#ifndef _FLASH_H
#define _FLASH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <hardware/spi.h>
#include <pico/stdlib.h>
#include <tusb.h>

#include "constants.h"

/**
 * @brief Read BOARD id
 * 
 */
void write_board_id();

/**
 * @brief Read JEDEC id of W25Q16JVSNIQ
 * 
 * Outputs 3-byte result to UART
 */
void flash_read_jedec_id();

/**
 * @brief Erase a 64 KiB block on the chip
 * 
 * @param block 64 KiB block-increment
 */
void flash_erase_block_64k(uint8_t block);

/**
 * @brief Resets the W25Q16JVSNIQ chip
 * 
 */
void flash_reset();

/**
 * @brief Check whether a 64 KiB block has been correctly erased
 * 
 * @param block 64 KiB block increment block
 * 
 * Outputs 0x00 when succesfully erased, 0xFF otherwise
 */
void flash_check_block_erased(uint8_t block);

/**
 * @brief Read a single 256-byte page from chip
 * 
 * @param page page index
 * 
 * Outputs 256 bytes over UART
 */
void flash_read_page(uint16_t page);

/**
 * @brief Write a 4096-byte sector to flash chip
 * 
 * @param sector 4096-byte sector increment
 */
void flash_write_sector(uint8_t sector);

//==========================================================
// AUXILIARY FUNCTIONS
//==========================================================

/**
 * @brief Select the W25Q16JVSNIQ by pulling its /CS low
 * 
 */
void flash_cs_select();

/**
 * @brief Deselect the W25Q16JVSNIQ by pulling its /CS high
 * 
 */
void flash_cs_deselect();

/**
 * @brief Enables write instructions for W25Q16JVSNIQ
 * 
 */
void flash_write_enable();

/**
 * @brief Low-level read page function
 * 
 * @param address starting address
 * @param buffer 256-byte storage buffer
 */
void flash_read_page_256(uint32_t address, uint8_t *buffer);

/**
 * @brief Waits until chip becomes available again
 * 
 */
void flash_wait_busy();

/**
 * @brief Generates CRC16 XMODEM checksum for data
 * 
 */
uint16_t crc16_xmodem(uint8_t *data, uint16_t length);

#endif // _FLASH_H