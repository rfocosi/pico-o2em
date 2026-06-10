#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"

#include "o2em_audio.h"

#define AUDIO_PIN_LEFT   28  // GPIO 28
#define AUDIO_PIN_RIGHT  27  // GPIO 27

#define PWM_WRAP_VAL     1024 // 10-bit audio resolution
#define FIFO_SIZE        4096

// PWM Configuration slices
static uint slice_left;
static uint chan_left;
static uint slice_right;
static uint chan_right;

// Circular audio buffer
static int16_t audio_fifo[FIFO_SIZE];
static volatile uint32_t fifo_head = 0;
static volatile uint32_t fifo_tail = 0;

static inline void fifo_push(int16_t sample) {
    uint32_t next = (fifo_head + 1) % FIFO_SIZE;
    if (next != fifo_tail) {
        audio_fifo[fifo_head] = sample;
        fifo_head = next;
    }
}

static inline int16_t fifo_pop(void) {
    if (fifo_tail == fifo_head) {
        return 0; // Empty
    }
    int16_t sample = audio_fifo[fifo_tail];
    fifo_tail = (fifo_tail + 1) % FIFO_SIZE;
    return sample;
}

// Timer callback to output audio samples at the target sample rate (~42240 Hz NTSC)
static bool audio_timer_callback(struct repeating_timer *t) {
    int16_t sample = fifo_pop();

    // Convert signed 16-bit (-32768 to 32767) to unsigned 10-bit PWM level (0 to 1024)
    uint32_t pwm_level = ((int32_t)sample + 32768) * PWM_WRAP_VAL / 65536;

    pwm_set_chan_level(slice_left, chan_left, pwm_level);
    pwm_set_chan_level(slice_right, chan_right, pwm_level);

    return true; // Keep repeating
}

static struct repeating_timer audio_timer;

void audio_init(void) {
    // 1. Configure GPIO pins for PWM function
    gpio_set_function(AUDIO_PIN_LEFT, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_PIN_RIGHT, GPIO_FUNC_PWM);

    // 2. Query PWM slice and channel numbers
    slice_left  = pwm_gpio_to_slice_num(AUDIO_PIN_LEFT);
    chan_left   = pwm_gpio_to_channel(AUDIO_PIN_LEFT);
    slice_right = pwm_gpio_to_slice_num(AUDIO_PIN_RIGHT);
    chan_right  = pwm_gpio_to_channel(AUDIO_PIN_RIGHT);

    // 3. Configure PWM wrap and enable slices
    // PWM frequency = 250 MHz system clock / PWM_WRAP_VAL = 244 KHz
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, PWM_WRAP_VAL);
    pwm_config_set_clkdiv(&config, 1.0f); // Run at full clock speed

    pwm_init(slice_left, &config, true);
    if (slice_right != slice_left) {
        pwm_init(slice_right, &config, true);
    }

    // Set initial levels to mid (0V analog differential)
    pwm_set_chan_level(slice_left, chan_left, PWM_WRAP_VAL / 2);
    pwm_set_chan_level(slice_right, chan_right, PWM_WRAP_VAL / 2);

    // 4. Start repeating timer at ~42240 Hz
    // Period = 1,000,000 / 42,240 = 23.67 microseconds. We use 24 microseconds.
    add_repeating_timer_us(-24, audio_timer_callback, NULL, &audio_timer);
}

void audio_play_samples(const int16_t *samples, size_t num_frames) {
    // Samples are interleaved stereo (L, R, L, R)
    for (size_t i = 0; i < num_frames; i++) {
        // Average Left and Right channels to produce Mono sample
        int16_t l = samples[2 * i];
        int16_t r = samples[2 * i + 1];
        int16_t mono = (int16_t)(((int32_t)l + r) / 2);

        fifo_push(mono);
    }
}
