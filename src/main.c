#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <libretro.h>

#include "o2em_system.h"
#include "o2em_video.h"
#include "o2em_audio.h"
#include "o2em_input.h"
#include "o2em_cartridge.h"

// Log callback from retro core
static void log_cb(enum retro_log_level level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// Video refresh callback from retro core
static void video_refresh_cb(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (data) {
        video_render_frame((const uint16_t *)data, width, height, pitch);
    }
}

// Audio sample batch callback from retro core
static size_t audio_sample_batch_cb(const int16_t *data, size_t frames) {
    if (data && frames > 0) {
        audio_play_samples(data, frames);
    }
    return frames;
}

// Audio sample callback (fallback, mono/stereo single sample)
static void audio_sample_cb(int16_t left, int16_t right) {
    int16_t samples[2] = { left, right };
    audio_play_samples(samples, 1);
}

// Input polling callback from retro core
static void input_poll_cb(void) {
    input_poll();
}

// Input state retrieval callback from retro core
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (device == RETRO_DEVICE_JOYPAD) {
        return input_get_joypad_state(port, id);
    }
    return 0;
}

// Environment callback to handle VFS, log, configuration options
static bool environ_cb(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            struct retro_log_callback *cb = (struct retro_log_callback *)data;
            cb->log = log_cb;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: {
            struct retro_vfs_interface_info *vfs_info = (struct retro_vfs_interface_info *)data;
            if (vfs_info && vfs_info->required_interface_version <= 3) {
                vfs_info->iface = system_get_vfs_interface();
                return true;
            }
            return false;
        }
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
            const char **dir = (const char **)data;
            *dir = ""; // Dummy directory for BIOS lookup
            return true;
        }
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            enum retro_pixel_format *fmt = (enum retro_pixel_format *)data;
            return (*fmt == RETRO_PIXEL_FORMAT_RGB565);
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            struct retro_variable *var = (struct retro_variable *)data;
            if (var->key) {
                if (strcmp(var->key, "o2em_bios") == 0) {
                    var->value = "o2rom.bin";
                    return true;
                }
                if (strcmp(var->key, "o2em_regional_mode") == 0) {
                    var->value = "NTSC";
                    return true;
                }
                if (strcmp(var->key, "o2em_swap_gamepads") == 0) {
                    var->value = "disabled";
                    return true;
                }
            }
            return false;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
            bool *updated = (bool *)data;
            *updated = false;
            return true;
        }
        default:
            return false;
    }
}

int main() {
    // 1. Initialise the core system (clocks, debug console, timing variables)
    system_init();
    printf("[Odyssey Pico] Initializing Subsystems...\n");

    // 2. Initialise the hardware adapters (I/O, Video, Audio)
    video_init();
    audio_init();
    input_init();

    // 3. Setup core callbacks
    retro_set_environment(environ_cb);
    retro_set_video_refresh(video_refresh_cb);
    retro_set_audio_sample(audio_sample_cb);
    retro_set_audio_sample_batch(audio_sample_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);

    // 4. Initialise the retro core
    retro_init();
    printf("[Odyssey Pico] Core Init Complete.\n");

    // 5. Read physical cartridge contents into rom_buffer via Shift Registers
    cartridge_dump();

    // 6. Define the game info struct and load the game
    struct retro_game_info game_info;
    game_info.path = "cart.bin";
    game_info.data = rom_buffer;
    game_info.size = rom_size_bytes;
    game_info.meta = NULL;

    printf("[Odyssey Pico] Loading Game ROM size: %u bytes\n", (unsigned)rom_size_bytes);
    if (!retro_load_game(&game_info)) {
        printf("[Odyssey Pico] ERROR: Failed to load game ROM!\n");
        while (true) {
            // Tight loop on failure
        }
    }
    printf("[Odyssey Pico] Game Loaded. Starting emulation loop...\n");

    // 7. Main emulation loop
    while (true) {
        // Core reads the joystick and keyboard states internally via input callbacks
        retro_run();

        // Enforce frame synchronization (60Hz NTSC / 50Hz PAL)
        system_sync_frame();
    }

    return 0;
}
