#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include <libretro.h>
#include "o2em_cartridge.h"
#include "o2em_input.h"
#include "ff.h"
#include "diskio.h"

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

// Declared in pico_msc.c
extern void usb_msc_mode_run(void);

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

static bool is_rom_empty(void) {
    // If the card slot is empty, the bus linesGP6-GP13 (pulled up) will read all 0xFF.
    // We check if all bytes in the dumped ROM are 0xFF.
    for (size_t i = 0; i < rom_size_bytes; i++) {
        if (rom_buffer[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

void cartridge_dump(void) {
    // 1. Check if user is holding Player 1 Action button (Active high in input_get_joypad_state)
    // to force boot into USB Mass Storage Mode
    input_poll();
    if (input_get_joypad_state(0, RETRO_DEVICE_ID_JOYPAD_B)) {
        printf("[Odyssey Pico] Player 1 Action button held at boot. Launching USB Mass Storage Mode...\n");
        usb_msc_mode_run(); // This never returns
    }

    printf("[Dumper] Starting Cartridge Dump...\n");

    // 2. Initialise Output Control Pins
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

    // 3. Initialise Data Bus Input Pins (GPIO 6 to 13)
    for (int i = 0; i < 8; i++) {
        gpio_init(DATA_BUS_BASE + i);
        gpio_set_dir(DATA_BUS_BASE + i, GPIO_IN);
        gpio_pull_up(DATA_BUS_BASE + i); // Enable pull-ups
    }

    // 4. Synchronously read the 4 banks of 2KB (Total 8KB)
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
    printf("[Dumper] Cartridge Dump Finished. Read %u bytes.\n", (unsigned)rom_size_bytes);

    // 5. Disable and release cartridge lines to save power
    gpio_put(PIN_CART_P10, false);
    gpio_put(PIN_CART_P11, false);
    gpio_put(PIN_CART_PSEN, true);

    // 6. Check if physical cartridge is present
    if (is_rom_empty()) {
        printf("[Dumper] No physical cartridge detected (all 0xFF). Checking internal flash VFAT...\n");
        
        // Initialize FatFs
        FATFS fs;
        disk_initialize(0);
        FRESULT fr = f_mount(&fs, "", 1); // 1 = force mount to verify filesystem
        if (fr == FR_NO_FILESYSTEM) {
            printf("[FatFs] No filesystem found. Formatting internal flash virtual partition...\n");
            BYTE work[FF_MAX_SS];
            fr = f_mkfs("", NULL, work, sizeof(work));
            if (fr == FR_OK) {
                printf("[FatFs] Format complete. Re-mounting...\n");
                fr = f_mount(&fs, "", 1);
                if (fr == FR_OK) {
                    // Create a readme file to guide the user
                    FIL readme;
                    if (f_open(&readme, "README.TXT", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
                        const char text[] = "Odyssey 2 Emulator for RP2040\r\n\r\n"
                                            "If no physical cartridge is inserted, this emulator "
                                            "will search this virtual flash drive for any file "
                                            "with a .bin or .rom extension (e.g. game.bin) and load it.\r\n";
                        UINT bw;
                        f_write(&readme, text, sizeof(text) - 1, &bw);
                        f_close(&readme);
                        disk_ioctl(0, CTRL_SYNC, NULL);
                        printf("[FatFs] Created README.TXT on virtual drive.\n");
                    }
                }
            } else {
                printf("[FatFs] Format failed: %d\n", fr);
            }
        }

        // Search the root directory for the first file with .bin or .rom extension
        char rom_filename[256] = {0};
        DIR dir;
        FILINFO fno;
        fr = f_opendir(&dir, "/");
        if (fr == FR_OK) {
            while (1) {
                fr = f_readdir(&dir, &fno);
                if (fr != FR_OK || fno.fname[0] == 0) break;
                if (fno.fattrib & AM_DIR) continue;

                char *ext = strrchr(fno.fname, '.');
                if (ext && (strcasecmp(ext, ".bin") == 0 || strcasecmp(ext, ".rom") == 0)) {
                    strncpy(rom_filename, fno.fname, sizeof(rom_filename) - 1);
                    break;
                }
            }
            f_closedir(&dir);
        }

        // Try to load the found ROM file into rom_buffer
        bool rom_loaded = false;
        if (rom_filename[0] != 0) {
            printf("[FatFs] Found ROM file: %s. Loading...\n", rom_filename);
            FIL fil;
            fr = f_open(&fil, rom_filename, FA_READ);
            if (fr == FR_OK) {
                UINT br;
                size_t file_size = f_size(&fil);
                size_t max_to_read = (file_size > ROM_BUFFER_MAX_SIZE) ? ROM_BUFFER_MAX_SIZE : file_size;
                
                fr = f_read(&fil, rom_buffer, max_to_read, &br);
                if (fr == FR_OK) {
                    rom_size_bytes = br;
                    rom_loaded = true;
                    printf("[FatFs] Successfully loaded %u bytes from %s.\n", (unsigned)rom_size_bytes, rom_filename);
                } else {
                    printf("[FatFs] Error reading ROM file: %d\n", fr);
                }
                f_close(&fil);
            } else {
                printf("[FatFs] Error opening ROM file: %d\n", fr);
            }
        }

        // Unmount
        f_mount(NULL, "", 0);

        // If no ROM was found or successfully loaded from internal flash, boot into USB Mass Storage Mode
        if (!rom_loaded) {
            printf("[Odyssey Pico] No ROM found or loaded from internal flash. Booting to USB Mass Storage Mode...\n");
            usb_msc_mode_run(); // This never returns
        }
    }
}
