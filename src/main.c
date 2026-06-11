#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pico/stdlib.h>
#include <hardware/spi.h>
#include <tusb.h>

#include "flash.h"
#include "constants.h"
#include "cmds.h"

// command storage
char instruction[9];    // stores single 8-byte instruction
uint8_t inptr = 0;      // instruction pointer
uint8_t buf[64];        // usb buffer

// forward declarations
void parse_instructions();
void init();

int main() {
    init();

    while(true) {

        if (tud_cdc_connected()) {
            if (tud_cdc_available()) {
                char c = tud_cdc_read_char();
        
                // only capture alphanumerical data
                if((c >= 48 && c <= 57) || (c >= 65 && c <=90)) {
                    instruction[inptr] = c;
                    inptr++;
                }
                
                if(inptr == 8) {
                    parse_instructions();
                    inptr = 0;
                }
            }
        }

        tud_task();
    }
}

/*
 * @brief parse instructions received over serial
 */
void parse_instructions() {
    echo_command(instruction, 8);
    
    /*
    * Read identifier string from board
    */
    if(check_command(instruction, "READINFO", 0, 8)) {
        write_board_id();
        return;
    } else if(check_command(instruction, "DEVIDSST", 0, 8)) {
        flash_read_jedec_id();
        return;
    } else if(check_command(instruction, "ERASBK", 0, 6)) {
        flash_erase_block_64k(get_uint8(instruction, 6));
        return;
    } else if(check_command(instruction, "RESETCHP", 0, 8)) {
        flash_reset();
        return;
    } else if(check_command(instruction, "CHCKBK", 0, 6)) {
        flash_check_block_erased(get_uint8(instruction, 6));
        return;
    } else if(check_command(instruction, "RDPG", 0, 4)) {
        flash_read_page(get_uint16(instruction, 4));
        return;
    } else if(check_command(instruction, "WRSECT", 0, 6)) {
        flash_write_sector(get_uint8(instruction, 6));
        return;
    } else if (check_command(instruction, "BOOTFPGA", 0, 8)) {
        fpga_release_and_check_boot();
        return;
    } else if (check_command(instruction, "HOLDFPGA", 0, 8)) {
        fpga_reset_hold();
        return;
    }
}

void init() {
    stdio_init_all();

    // RPI PICO LED
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    
    // ice40up5k CDONE pin
    gpio_init(PIN_CDONE);
    gpio_set_dir(PIN_CDONE, GPIO_IN);
    gpio_pull_up(PIN_CDONE);

    // ice40up5k RESET pin
    gpio_init(PIN_CRESET);
    gpio_set_dir(PIN_CRESET, GPIO_OUT);
    gpio_put(PIN_CRESET, 0);

    // W25Q16JVSNIQ /CS pin
    gpio_init(PIN_SEL);
    gpio_set_dir(PIN_SEL, GPIO_OUT);
    flash_cs_deselect();

    // initialize SPI
    spi_init(SPI_PORT, 1 * 1000 * 1000); // 1 MHz
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
}