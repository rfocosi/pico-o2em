#include "ff.h"
#include "diskio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>
#include <stdio.h>

#define FS_FLASH_OFFSET   (1024 * 1024)   // 1MB offset in Flash
#define FS_SECTOR_SIZE    512
#define SECTORS_PER_BLOCK (FLASH_SECTOR_SIZE / FS_SECTOR_SIZE) // 8
#include "o2em_cartridge.h"
#define cache_buf rom_buffer
static int32_t cached_block = -1;  // Block index currently in cache (-1 if none)
static bool cache_dirty = false;
static bool disk_initialized = false;

// Internal helper to flush dirty cache block to flash
static void cache_flush(void) {
    if (cached_block == -1 || !cache_dirty) return;

    uint32_t flash_addr = FS_FLASH_OFFSET + cached_block * FLASH_SECTOR_SIZE;

    // flash_range_erase and flash_range_program require interrupts to be disabled
    // because execution/DMA from flash is suspended.
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_addr, FLASH_SECTOR_SIZE);
    flash_range_program(flash_addr, cache_buf, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    cache_dirty = false;
}

// Internal helper to read a 4KB block from flash into cache
static void cache_read_block(uint32_t block) {
    if (cached_block == (int32_t)block) return;

    // Flush current cache if dirty
    if (cache_dirty && cached_block != -1) {
        cache_flush();
    }

    const uint8_t *flash_addr = (const uint8_t *)(XIP_BASE + FS_FLASH_OFFSET + block * FLASH_SECTOR_SIZE);
    memcpy(cache_buf, flash_addr, FLASH_SECTOR_SIZE);
    cached_block = block;
    cache_dirty = false;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return disk_initialized ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    disk_initialized = true;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !disk_initialized) return RES_PARERR;

    for (UINT i = 0; i < count; i++) {
        LBA_t cur_sector = sector + i;
        uint32_t block = cur_sector / SECTORS_PER_BLOCK;
        uint32_t offset_in_block = (cur_sector % SECTORS_PER_BLOCK) * FS_SECTOR_SIZE;

        if (block == (uint32_t)cached_block) {
            // Read from the active write-back cache
            memcpy(buff + i * FS_SECTOR_SIZE, cache_buf + offset_in_block, FS_SECTOR_SIZE);
        } else {
            // Read directly from the memory-mapped flash address for speed
            const uint8_t *flash_addr = (const uint8_t *)(XIP_BASE + FS_FLASH_OFFSET + cur_sector * FS_SECTOR_SIZE);
            memcpy(buff + i * FS_SECTOR_SIZE, flash_addr, FS_SECTOR_SIZE);
        }
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !disk_initialized) return RES_PARERR;

    for (UINT i = 0; i < count; i++) {
        LBA_t cur_sector = sector + i;
        uint32_t block = cur_sector / SECTORS_PER_BLOCK;
        uint32_t offset_in_block = (cur_sector % SECTORS_PER_BLOCK) * FS_SECTOR_SIZE;

        cache_read_block(block);
        memcpy(cache_buf + offset_in_block, buff + i * FS_SECTOR_SIZE, FS_SECTOR_SIZE);
        cache_dirty = true;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != 0 || !disk_initialized) return RES_PARERR;

    switch (cmd) {
        case CTRL_SYNC:
            cache_flush();
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(LBA_t*)buff = (1024 * 1024) / FS_SECTOR_SIZE; // 2048 sectors (1MB)
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD*)buff = FS_SECTOR_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD*)buff = SECTORS_PER_BLOCK; // 8 sectors per erase block (4KB)
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

DWORD get_fattime(void) {
    // Return a dummy timestamp: 2026-06-10 12:00:00
    // MS-DOS time/date format:
    // Bits 31:25 - Year (relative to 1980, e.g. 46 for 2026)
    // Bits 24:21 - Month (1-12)
    // Bits 20:16 - Day of the month (1-31)
    // Bits 15:11 - Hour (0-23)
    // Bits 10:5  - Minute (0-59)
    // Bits 4:0   - Second / 2 (0-29)
    return ((DWORD)(2026 - 1980) << 25) |
           ((DWORD)6 << 21) |
           ((DWORD)10 << 16) |
           ((DWORD)12 << 11) |
           ((DWORD)0 << 5) |
           ((DWORD)0 >> 1);
}
