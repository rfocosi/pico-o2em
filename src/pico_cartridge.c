#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "cartridge.h"

// 74HC595 Address Shifter Pins
#define PIN_CART_SER    14  // Serial Data Input
#define PIN_CART_SRCLK  15  // Shift Register Clock
#define PIN_CART_RCLK   16  // Latch / Storage Register Clock

// Cartridge Control Lines
#define PIN_CART_PSEN   17  // ~PSEN (Active Low Chip Enable)
#define PIN_CART_P11    18  // Bank Select High
#define PIN_CART_P10    19  // Bank Select Low

// Data Bus Base (GPIO 6 to 13)
#define DATA_BUS_BASE   6

uint8_t rom_buffer[ROM_BUFFER_MAX_SIZE];
size_t rom_size_bytes = 0;

static void shift_address(uint16_t addr) {
    // Shift 12 address bits (A0-A11) into the cascaded 74HC595 registers
    for (int i = 11; i >= 0; i--) {
        gpio_put(PIN_CART_SER, (addr & (1 << i)) ? true : false);

        // Pulse Shift Register Clock
        gpio_put(PIN_CART_SRCLK, false);
        sleep_us(1);
        gpio_put(PIN_CART_SRCLK, true);
    }

    // Pulse Storage Register Clock (Latch) to update output pins
    gpio_put(PIN_CART_RCLK, false);
    sleep_us(1);
    gpio_put(PIN_CART_RCLK, true);
}

void cartridge_dump(void) {
    printf("[Dumper] Starting Cartridge Dump...\n");

    // 1. Initialise Output Control Pins
    gpio_init(PIN_CART_SER);
    gpio_set_dir(PIN_CART_SER, GPIO_OUT);
    
    gpio_init(PIN_CART_SRCLK);
    gpio_set_dir(PIN_CART_SRCLK, GPIO_OUT);
    
    gpio_init(PIN_CART_RCLK);
    gpio_set_dir(PIN_CART_RCLK, GPIO_OUT);
    
    gpio_init(PIN_CART_PSEN);
    gpio_set_dir(PIN_CART_PSEN, GPIO_OUT);
    gpio_put(PIN_CART_PSEN, true); // Keep disabled initially (High)

    gpio_init(PIN_CART_P10);
    gpio_set_dir(PIN_CART_P10, GPIO_OUT);
    gpio_put(PIN_CART_P10, false);

    gpio_init(PIN_CART_P11);
    gpio_set_dir(PIN_CART_P11, GPIO_OUT);
    gpio_put(PIN_CART_P11, false);

    // 2. Initialise Data Bus Input Pins (GPIO 6 to 13)
    for (int i = 0; i < 8; i++) {
        gpio_init(DATA_BUS_BASE + i);
        gpio_set_dir(DATA_BUS_BASE + i, GPIO_IN);
        gpio_pull_up(DATA_BUS_BASE + i); // Enable pull-ups
    }

    // 3. Sychronously read the 4 banks of 2KB (Total 8KB)
    for (int bank = 0; bank < 4; bank++) {
        // Set bank selection lines (P10/P11)
        gpio_put(PIN_CART_P10, (bank & 1) ? true : false);
        gpio_put(PIN_CART_P11, (bank & 2) ? true : false);
        sleep_ms(2); // Let voltage settle

        for (int addr = 0; addr < 2048; addr++) {
            // Shift address onto address lines A0-A11
            shift_address(addr);

            // Pulse ~PSEN low to enable cartridge ROM output
            gpio_put(PIN_CART_PSEN, false);
            sleep_us(2); // Wait for ROM propagation delay

            // Read the 8-bit parallel data bus (GPIO 6 to 13)
            uint8_t data_byte = 0;
            for (int bit = 0; bit < 8; bit++) {
                if (gpio_get(DATA_BUS_BASE + bit)) {
                    data_byte |= (1 << bit);
                }
            }

            // Disable ROM output
            gpio_put(PIN_CART_PSEN, true);

            // Store in buffer
            rom_buffer[bank * 2048 + addr] = data_byte;
        }
    }

    rom_size_bytes = ROM_BUFFER_MAX_SIZE; // Successfully read 8KB
    printf("[Dumper] Cartridge Dump Finished. Read %d bytes.\n", rom_size_bytes);

    // 4. Disable and release cartridge lines to save power
    gpio_put(PIN_CART_P10, false);
    gpio_put(PIN_CART_P11, false);
    gpio_put(PIN_CART_PSEN, true);
}
