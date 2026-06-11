#include "cmds.h"

/**
 * @brief convert 2 HEX bytes of instruction to 8 bit unsigned integer
 * @param command word
 * @param offset in command
 */
uint8_t get_uint8(const char* instruction, uint8_t offset) {
    char buffer[3];
    buffer[0] = instruction[offset];
    buffer[1] = instruction[offset+1];
    buffer[2] = '\0';
    return strtoul(buffer, NULL, 16);
}

/**
 * @brief convert 4 HEX bytes of instruction to 16 bit unsigned integer
 * @param command word
 * @param offset in command
 */
uint16_t get_uint16(const char* instruction, uint8_t offset) {
    char buffer[5];
    memcpy(buffer, &instruction[offset], 4);
    buffer[4] = '\0';
    return strtoul(buffer, NULL, 16);
}

/**
 * Return command back over serial
 */
void echo_command(const char* inst, uint8_t size) {
    tud_cdc_write(inst, size);
    tud_cdc_write_flush();
}

/**
 * @brief Check if part of two strings are equal
 * @param original string
 * @param reference string
 * @param offset in original string
 * @param length to read
 */
bool check_command(const char* cmd, const char* ref, uint8_t offset, uint8_t length) {
    for(uint8_t i=0; i<length; i++) {
        if(cmd[i+offset] != ref[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Release the FPGA from its reset state and check whether CDONE is pulled
 *        to HIGH correctly. If so, let the LED reflect that state.
 * 
 */
void fpga_release_and_check_boot(void) {
    // Release FPGA from reset
    gpio_put(PIN_CRESET, 1);

    // Wait enough time for configuration
    sleep_ms(150);

    // Check CDONE
    bool cdone = gpio_get(PIN_CDONE);

    // Send status over USB
    if (tud_cdc_connected()) {
        tud_cdc_write_char(cdone ? 1 : 0);
        tud_cdc_write_flush();
        tud_task();
    }

    // Optional: LED shows CDONE
    gpio_put(PIN_LED, cdone);
}

/**
 * @brief Put the FPGA back into its reset state
 * 
 */
void fpga_reset_hold(void) {
    // drive CRESET low to hold FPGA in reset
    gpio_put(PIN_CRESET, 0);

    // turn off CDONE indicator LED
    gpio_put(PIN_LED, 0);

    // Optional: send confirmation over USB
    if (tud_cdc_connected()) {
        tud_cdc_write_char(0);  // 0 = in reset
        tud_cdc_write_flush();
        tud_task();
    }
}