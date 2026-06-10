#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include <libretro.h>

#include "o2em_system.h"

#include "ff.h"
#include "diskio.h"

// Placeholder BIOS array of 1024 bytes
// In a physical build, the user can replace this with the real Odyssey 2 BIOS (o2rom.bin)
static const uint8_t default_bios_rom[1024] = {
    0x00 // Fill with placeholder bytes
};

// Virtual VFS File Handle abstraction
struct retro_vfs_file_handle {
    FIL fil;
    bool is_fatfs;
    size_t offset;
    size_t size;
    const uint8_t *data;
};

static struct retro_vfs_file_handle virtual_bios_handle;
static FATFS bios_fs;
static bool bios_fs_mounted = false;

// Mock VFS functions
static const char* vfs_get_path_impl(struct retro_vfs_file_handle *stream) {
    return "o2rom.bin";
}

static struct retro_vfs_file_handle* vfs_open_impl(const char *path, unsigned mode, unsigned hints) {
    // Only intercept reads for the BIOS files
    if (strstr(path, "o2rom.bin") != NULL || strstr(path, "c52.bin") != NULL || 
        strstr(path, "g7400.bin") != NULL || strstr(path, "jopac.bin") != NULL) {
        
        // Attempt to mount the VFAT partition in internal flash
        if (!bios_fs_mounted) {
            disk_initialize(0);
            if (f_mount(&bios_fs, "", 1) == FR_OK) {
                bios_fs_mounted = true;
            }
        }
        
        // Extract filename from the path
        const char *filename = strrchr(path, '/');
        if (filename) filename++;
        else filename = path;
        
        if (bios_fs_mounted) {
            if (f_open(&virtual_bios_handle.fil, filename, FA_READ) == FR_OK) {
                virtual_bios_handle.is_fatfs = true;
                virtual_bios_handle.size = f_size(&virtual_bios_handle.fil);
                virtual_bios_handle.offset = 0;
                printf("[VFS] Opened BIOS '%s' from virtual flash drive (size: %u bytes)\n", filename, (unsigned)virtual_bios_handle.size);
                return &virtual_bios_handle;
            }
        }
        
        // Fallback to static embedded placeholder if not found on drive
        printf("[VFS] WARNING: BIOS '%s' not found on virtual drive. Using embedded placeholder.\n", filename);
        virtual_bios_handle.is_fatfs = false;
        virtual_bios_handle.offset = 0;
        virtual_bios_handle.size = 1024;
        virtual_bios_handle.data = default_bios_rom;
        return &virtual_bios_handle;
    }
    return NULL;
}

static int vfs_close_impl(struct retro_vfs_file_handle *stream) {
    if (stream && stream->is_fatfs) {
        f_close(&stream->fil);
    }
    return 0;
}

static int64_t vfs_size_impl(struct retro_vfs_file_handle *stream) {
    if (stream) {
        return stream->size;
    }
    return -1;
}

static int64_t vfs_tell_impl(struct retro_vfs_file_handle *stream) {
    if (stream) {
        if (stream->is_fatfs) {
            return f_tell(&stream->fil);
        } else {
            return stream->offset;
        }
    }
    return -1;
}

static int64_t vfs_seek_impl(struct retro_vfs_file_handle *stream, int64_t offset, int seek_position) {
    if (!stream) return -1;
    
    if (stream->is_fatfs) {
        FRESULT fr = FR_OK;
        switch (seek_position) {
            case RETRO_VFS_SEEK_POSITION_START:
                fr = f_lseek(&stream->fil, offset);
                break;
            case RETRO_VFS_SEEK_POSITION_CURRENT:
                fr = f_lseek(&stream->fil, f_tell(&stream->fil) + offset);
                break;
            case RETRO_VFS_SEEK_POSITION_END:
                fr = f_lseek(&stream->fil, f_size(&stream->fil) + offset);
                break;
            default:
                return -1;
        }
        return (fr == FR_OK) ? 0 : -1;
    } else {
        size_t new_offset = stream->offset;
        switch (seek_position) {
            case RETRO_VFS_SEEK_POSITION_START:
                new_offset = offset;
                break;
            case RETRO_VFS_SEEK_POSITION_CURRENT:
                new_offset = stream->offset + offset;
                break;
            case RETRO_VFS_SEEK_POSITION_END:
                new_offset = stream->size + offset;
                break;
            default:
                return -1;
        }
        
        if (new_offset > stream->size) {
            new_offset = stream->size;
        }
        stream->offset = new_offset;
        return 0;
    }
}

static int64_t vfs_read_impl(struct retro_vfs_file_handle *stream, void *s, uint64_t len) {
    if (!stream || !s) return -1;
    
    if (stream->is_fatfs) {
        UINT br;
        FRESULT fr = f_read(&stream->fil, s, len, &br);
        return (fr == FR_OK) ? (int64_t)br : -1;
    } else {
        int64_t available = stream->size - stream->offset;
        if ((int64_t)len > available) {
            len = available;
        }
        
        if (len > 0) {
            memcpy(s, stream->data + stream->offset, len);
            stream->offset += len;
            return len;
        }
        return 0;
    }
}

static int64_t vfs_write_impl(struct retro_vfs_file_handle *stream, const void *s, uint64_t len) {
    return -1; // Read-only VFS for system files
}

static int vfs_flush_impl(struct retro_vfs_file_handle *stream) {
    return 0;
}

static int vfs_remove_impl(const char *path) {
    return -1;
}

static int vfs_rename_impl(const char *old_path, const char *new_path) {
    return -1;
}

// Custom VFS Interface definition
static struct retro_vfs_interface vfs_interface = {
    .get_path = vfs_get_path_impl,
    .open     = vfs_open_impl,
    .close    = vfs_close_impl,
    .size     = vfs_size_impl,
    .tell     = vfs_tell_impl,
    .seek     = vfs_seek_impl,
    .read     = vfs_read_impl,
    .write    = vfs_write_impl,
    .flush    = vfs_flush_impl,
    .remove   = vfs_remove_impl,
    .rename   = vfs_rename_impl
};

void system_init(void) {
    // 1. Initialize standard I/O (UART & USB stdout)
    stdio_init_all();

    // 2. Overclock the RP2040 system clock to 250 MHz to support emulation speed
    // Default is 133 MHz. 250 MHz is safe, stable and increases processing margin.
    bool success = set_sys_clock_khz(250000, true);
    if (success) {
        printf("[Odyssey Pico] System overclocked to 250 MHz successfully.\n");
    } else {
        printf("[Odyssey Pico] Failed to overclock system. Running at default frequency.\n");
    }
}

struct retro_vfs_interface* system_get_vfs_interface(void) {
    return &vfs_interface;
}

void system_sync_frame(void) {
    static absolute_time_t target_time;
    static bool init = false;

    if (!init) {
        target_time = get_absolute_time();
        init = true;
    }

    // Wait until target absolute time (yielding CPU cycles)
    sleep_until(target_time);

    // Odyssey 2 runs NTSC at roughly 60Hz. Delay target by 16,666 microseconds (1/60th second)
    target_time = delayed_by_us(target_time, 16666);
}

// ============================================================================
// Newlib/Bare-metal Libretro-common VFS Linker Stubs
// ============================================================================
#include <vfs/vfs_implementation.h>

libretro_vfs_implementation_file *retro_vfs_file_open_impl(const char *path, unsigned mode, unsigned hints) { return NULL; }
int retro_vfs_file_close_impl(libretro_vfs_implementation_file *stream) { return -1; }
int retro_vfs_file_error_impl(libretro_vfs_implementation_file *stream) { return -1; }
int64_t retro_vfs_file_size_impl(libretro_vfs_implementation_file *stream) { return -1; }
int64_t retro_vfs_file_truncate_impl(libretro_vfs_implementation_file *stream, int64_t length) { return -1; }
int64_t retro_vfs_file_tell_impl(libretro_vfs_implementation_file *stream) { return -1; }
int64_t retro_vfs_file_seek_impl(libretro_vfs_implementation_file *stream, int64_t offset, int seek_position) { return -1; }
int64_t retro_vfs_file_read_impl(libretro_vfs_implementation_file *stream, void *s, uint64_t len) { return -1; }
int64_t retro_vfs_file_write_impl(libretro_vfs_implementation_file *stream, const void *s, uint64_t len) { return -1; }
int retro_vfs_file_flush_impl(libretro_vfs_implementation_file *stream) { return -1; }
int retro_vfs_file_remove_impl(const char *path) { return -1; }
int retro_vfs_file_rename_impl(const char *old_path, const char *new_path) { return -1; }
const char *retro_vfs_file_get_path_impl(libretro_vfs_implementation_file *stream) { return NULL; }
int retro_vfs_stat_impl(const char *path, int32_t *size) { return -1; }
int retro_vfs_mkdir_impl(const char *dir) { return -1; }
libretro_vfs_implementation_dir *retro_vfs_opendir_impl(const char *dir, bool include_hidden) { return NULL; }
bool retro_vfs_readdir_impl(libretro_vfs_implementation_dir *dirstream) { return false; }
const char *retro_vfs_dirent_get_name_impl(libretro_vfs_implementation_dir *dirstream) { return NULL; }
bool retro_vfs_dirent_is_dir_impl(libretro_vfs_implementation_dir *dirstream) { return false; }
int retro_vfs_closedir_impl(libretro_vfs_implementation_dir *dirstream) { return -1; }
