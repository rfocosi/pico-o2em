#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "video.h"
#include "composite_video.pio.h"

#define VIDEO_GPIO_BASE 20  // GPIO 20, 21, 22

// NTSC horizontal timing at 6.16 MHz sample clock
#define SAMPLES_PER_LINE  392
#define H_SYNC_SAMPLES    29  // ~4.7 us (0V)
#define BACK_PORCH        29  // ~4.7 us (0.3V blanking)
#define ACTIVE_SAMPLES    324 // ~52.6 us
#define FRONT_PORCH       10  // ~1.5 us (0.3V blanking)

// 3-bit DAC Levels (GPIO 20, 21, 22)
#define LEVEL_SYNC   0x00  // 0.0V
#define LEVEL_BLANK  0x01  // 0.3V
#define LEVEL_BLACK  0x01  // 0.3V
#define LEVEL_GRAY1  0x02
#define LEVEL_GRAY2  0x03
#define LEVEL_GRAY3  0x04
#define LEVEL_GRAY4  0x05
#define LEVEL_GRAY5  0x06
#define LEVEL_WHITE  0x07  // 1.0V

// Line buffers to save RAM (only 1 line is active at a time)
static uint8_t active_line_buffer[SAMPLES_PER_LINE];
static uint8_t sync_line_buffer[SAMPLES_PER_LINE];

static PIO video_pio = pio0;
static uint video_sm = 0;
static int video_dma_chan = 0;

// Set up static lines
static void init_line_buffers(void) {
    // 1. Active line template: H-Sync (low), Back Porch (blank), Active (black), Front Porch (blank)
    int idx = 0;
    for (int i = 0; i < H_SYNC_SAMPLES; i++) active_line_buffer[idx++] = LEVEL_SYNC;
    for (int i = 0; i < BACK_PORCH;     i++) active_line_buffer[idx++] = LEVEL_BLANK;
    for (int i = 0; i < ACTIVE_SAMPLES; i++) active_line_buffer[idx++] = LEVEL_BLACK;
    for (int i = 0; i < FRONT_PORCH;    i++) active_line_buffer[idx++] = LEVEL_BLANK;

    // 2. V-Sync/Blank line: low H-Sync, and blanking for the rest
    idx = 0;
    for (int i = 0; i < H_SYNC_SAMPLES; i++) sync_line_buffer[idx++] = LEVEL_SYNC;
    for (int i = 0; i < (SAMPLES_PER_LINE - H_SYNC_SAMPLES); i++) {
        sync_line_buffer[idx++] = LEVEL_BLANK;
    }
}

void video_init(void) {
    // 1. Load the PIO program into the selected PIO instance
    uint offset = pio_add_program(video_pio, &composite_video_program);

    // 2. Initialize the state machine
    composite_video_program_init(video_pio, video_sm, offset, VIDEO_GPIO_BASE);

    // 3. Initialize line buffers
    init_line_buffers();

    // 4. Setup DMA channel to transfer scanlines to PIO TX FIFO
    video_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(video_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(video_pio, video_sm, true));

    dma_channel_configure(
        video_dma_chan,
        &c,
        &video_pio->txf[video_sm], // Write address
        NULL,                      // Read address (set before each transfer)
        SAMPLES_PER_LINE,          // Number of transfers
        false                      // Don't start yet
    );
}

// Helper to convert RGB565 pixel to 3-bit Grayscale Luma level
static inline uint8_t rgb565_to_luma_3bit(uint16_t rgb) {
    // Extract RGB components from RGB565
    uint8_t r = (rgb >> 11) & 0x1F; // 5 bits
    uint8_t g = (rgb >> 5)  & 0x3F; // 6 bits
    uint8_t b = rgb         & 0x1F; // 5 bits

    // Standard Luma formula: Y = 0.299R + 0.587G + 0.114B
    // Scale components to 0-255: R*8, G*4, B*8
    uint32_t luma = (r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8; // 0-255

    // Map 0-255 to 3-bit DAC active range (LEVEL_BLACK to LEVEL_WHITE, i.e., 1 to 7)
    // 0 -> LEVEL_BLACK (1), 255 -> LEVEL_WHITE (7)
    return LEVEL_BLACK + (luma * 6 / 255);
}

void video_render_frame(const uint16_t *frame_buf, int width, int height, size_t pitch) {
    // Pitch is in bytes. Pixels are 16-bit (2 bytes)
    int pitch_pixels = pitch >> 1;

    // NTSC has 262 total lines:
    // Lines 1-9: V-Sync / Equalization
    // Lines 10-21: V-Blanking
    // Lines 22-261: Active frames (240 lines)
    // Line 262: Bottom Blanking

    // Transmit frame line-by-line
    for (int line = 1; line <= 262; line++) {
        const uint8_t *src_line = sync_line_buffer;

        // Active lines mapping (Lines 22 to 261)
        if (line >= 22 && line <= 261) {
            int emu_line = line - 22; // 0 to 239

            // If the emulator height is smaller, pad or repeat lines
            if (emu_line < height) {
                const uint16_t *src_pixels = frame_buf + (emu_line * pitch_pixels);

                // Map active pixel region
                int start_offset = H_SYNC_SAMPLES + BACK_PORCH;
                for (int x = 0; x < ACTIVE_SAMPLES; x++) {
                    if (x < width) {
                        active_line_buffer[start_offset + x] = rgb565_to_luma_3bit(src_pixels[x]);
                    } else {
                        active_line_buffer[start_offset + x] = LEVEL_BLANK;
                    }
                }
                src_line = active_line_buffer;
            }
        }

        // Wait for previous line DMA transfer to complete
        dma_channel_wait_for_finish_blocking(video_dma_chan);

        // Start DMA for the current line
        dma_channel_set_read_addr(video_dma_chan, src_line, true);
    }
}
