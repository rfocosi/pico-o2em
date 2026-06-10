#ifndef O2EM_INPUT_H
#define O2EM_INPUT_H

#include <stdint.h>
#include <stdbool.h>

// Initialize joysticks (74HC165) and keyboard expander (MCP23017)
void input_init(void);

// Poll inputs and update internal states
void input_poll(void);

// Get state of a button/axis for a device (player 0 or 1)
// device: 0 (Player 1), 1 (Player 2)
// button_id: RETRO_DEVICE_ID_JOYPAD_X
int16_t input_get_joypad_state(unsigned device, unsigned id);

// Check keyboard state for matrix key codes (row x col)
// Returns true if pressed
bool input_is_key_pressed(uint8_t row, uint8_t col);

#endif // O2EM_INPUT_H
