#ifndef _CMDS_H
#define _CMDS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <tusb.h>

#include "constants.h"

/**
 * @brief convert 2 HEX bytes of instruction to 8 bit unsigned integer
 * @param command word
 * @param offset in command
 */
uint8_t get_uint8(const char* instruction, uint8_t offset);

/**
 * @brief convert 4 HEX bytes of instruction to 16 bit unsigned integer
 * @param command word
 * @param offset in command
 */
uint16_t get_uint16(const char* instruction, uint8_t offset);

/**
 * @brief Check if part of two strings are equal
 * @param original string
 * @param reference string
 * @param offset in original string
 * @param length to read
 */
void echo_command(const char* inst, uint8_t size);

/**
 * @brief Check if part of two strings are equal
 * @param original string
 * @param reference string
 * @param offset in original string
 * @param length to read
 */
bool check_command(const char* cmd, const char* ref, uint8_t offset, uint8_t length);

/**
 * @brief Release the FPGA from its reset state and check whether CDONE is pulled
 *        to HIGH correctly. If so, let the LED reflect that state.
 * 
 */
void fpga_release_and_check_boot();

/**
 * @brief Put the FPGA back into its reset state
 * 
 */
void fpga_reset_hold(void);

#endif // _CMDS_H