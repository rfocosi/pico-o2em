#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include <libretro.h>

#include "input.h"

// 74HC165 Joystick Pins
#define PIN_JOY_CLK   10
#define PIN_JOY_LATCH 11
#define PIN_JOY_DATA  12

// MCP23017 Keyboard I2C Pins & Address
#define I2C_PORT      i2c0
#define PIN_I2C_SDA   4
#define PIN_I2C_SCL   5
#define MCP23017_ADDR 0x20

// MCP23017 Registers
#define REG_IODIRA   0x00
#define REG_IODIRB   0x01
#define REG_GPPUA    0x0C
#define REG_GPPUB    0x0D
#define REG_GPIOA    0x12
#define REG_GPIOB    0x13

// Joystick state variables (P1 and P2)
static uint16_t joy_state = 0xFFFF; // Active Low (unpressed = 1)

// Keyboard matrix state (6 rows, 8 columns)
static bool keyboard_matrix[6][8];

static void write_mcp23017_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    i2c_write_blocking(I2C_PORT, MCP23017_ADDR, buf, 2, false);
}

static uint8_t read_mcp23017_reg(uint8_t reg) {
    uint8_t val = 0;
    i2c_write_blocking(I2C_PORT, MCP23017_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, MCP23017_ADDR, &val, 1, false);
    return val;
}

void input_init(void) {
    // 1. Initialise 74HC165 GPIO Pins
    gpio_init(PIN_JOY_CLK);
    gpio_set_dir(PIN_JOY_CLK, GPIO_OUT);
    gpio_put(PIN_JOY_CLK, true);

    gpio_init(PIN_JOY_LATCH);
    gpio_set_dir(PIN_JOY_LATCH, GPIO_OUT);
    gpio_put(PIN_JOY_LATCH, true);

    gpio_init(PIN_JOY_DATA);
    gpio_set_dir(PIN_JOY_DATA, GPIO_IN);
    gpio_pull_up(PIN_JOY_DATA);

    // 2. Initialise I2C for MCP23017 Keyboard Expanders
    i2c_init(I2C_PORT, 400 * 1000); // 400 KHz
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);

    // Configure MCP23017:
    // GPA (GPA0-GPA5) are inputs (matrix rows)
    // GPB (GPB0-GPB7) are outputs (matrix columns)
    write_mcp23017_reg(REG_IODIRA, 0x3F); // GPA0-GPA5 inputs, others outputs
    write_mcp23017_reg(REG_IODIRB, 0x00); // GPB0-GPB7 outputs
    write_mcp23017_reg(REG_GPPUA,  0x3F); // Pull-ups enabled on GPA0-GPA5
    write_mcp23017_reg(REG_GPPUB,  0x00); // No pull-ups on GPB

    // Set GPB outputs high initially
    write_mcp23017_reg(REG_GPIOB,  0xFF);
}

void input_poll(void) {
    // 1. Poll 74HC165 Joysticks
    // Pulse Latch to lock button values
    gpio_put(PIN_JOY_LATCH, false);
    sleep_us(1);
    gpio_put(PIN_JOY_LATCH, true);

    uint16_t state = 0;
    for (int i = 0; i < 16; i++) {
        // Read current serial bit
        state = (state << 1) | (gpio_get(PIN_JOY_DATA) ? 1 : 0);

        // Pulse Clock
        gpio_put(PIN_JOY_CLK, false);
        sleep_us(1);
        gpio_put(PIN_JOY_CLK, true);
    }
    joy_state = state;

    // 2. Poll MCP23017 Keyboard Matrix (Scan 6 rows x 8 columns)
    for (int col = 0; col < 8; col++) {
        // Drive only column 'col' low, others high
        uint8_t col_mask = ~(1 << col);
        write_mcp23017_reg(REG_GPIOB, col_mask);

        // Read GPA rows (low means key is pressed due to row-col short circuit)
        uint8_t row_state = read_mcp23017_reg(REG_GPIOA) & 0x3F;

        for (int row = 0; row < 6; row++) {
            bool pressed = ((row_state & (1 << row)) == 0);
            keyboard_matrix[row][col] = pressed;
        }
    }
    // Re-set all columns high
    write_mcp23017_reg(REG_GPIOB, 0xFF);
}

int16_t input_get_joypad_state(unsigned device, unsigned id) {
    // joy_state bits are Active-Low.
    // Assuming Player 1 controls are mapped to bits 0-7, Player 2 controls to bits 8-15.
    // Bit mapping template: Up(0), Down(1), Left(2), Right(3), Action/B(4).
    uint8_t shift = (device == 0) ? 0 : 8;
    uint16_t player_state = (joy_state >> shift) & 0xFF;

    switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_UP:
            return (player_state & (1 << 0)) == 0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:
            return (player_state & (1 << 1)) == 0;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:
            return (player_state & (1 << 2)) == 0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:
            return (player_state & (1 << 3)) == 0;
        case RETRO_DEVICE_ID_JOYPAD_B:
            return (player_state & (1 << 4)) == 0; // Action button
        default:
            return 0;
    }
}

bool input_is_key_pressed(uint8_t row, uint8_t col) {
    if (row < 6 && col < 8) {
        return keyboard_matrix[row][col];
    }
    return false;
}
