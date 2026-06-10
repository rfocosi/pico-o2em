#ifndef O2EM_VIDEO_H
#define O2EM_VIDEO_H

#include <stdint.h>
#include <stddef.h>

// Initialize composite video subsystem (PIO, DMA, DAC pins)
void video_init(void);

// Send a frame to the composite video renderer
// frame_buf: pointer to the RGB565 framebuffer (TEX_WIDTH x TEX_HEIGHT)
// width, height: active dimensions
// pitch: bytes per scanline
void video_render_frame(const uint16_t *frame_buf, int width, int height, size_t pitch);

#endif // O2EM_VIDEO_H
