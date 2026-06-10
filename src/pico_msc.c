#include "tusb.h"
#include "ff.h"
#include "diskio.h"
#include <string.h>
#include <stdio.h>

static bool ejected = false;

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void) lun;
    const char vid[] = "PicoO2EM";
    const char pid[] = "Virtual Disk";
    const char rev[] = "1.0";

    memset(vendor_id, ' ', 8);
    memset(product_id, ' ', 16);
    memset(product_rev, ' ', 4);

    memcpy(vendor_id, vid, (strlen(vid) > 8) ? 8 : strlen(vid));
    memcpy(product_id, pid, (strlen(pid) > 16) ? 16 : strlen(pid));
    memcpy(product_rev, rev, (strlen(rev) > 4) ? 4 : strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g. SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void) lun;
    if (ejected) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00); // Medium not present
        return false;
    }
    return true;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
    (void) lun;
    LBA_t sectors = 0;
    disk_ioctl(0, GET_SECTOR_COUNT, &sectors);
    
    *block_count = sectors;
    *block_size  = 512;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void) lun;
    (void) power_condition;

    if (load_eject) {
        if (start) {
            ejected = false;
        } else {
            ejected = true;
        }
    }
    
    // Sync filesystem block cache when stopping/ejecting
    if (!start) {
        disk_ioctl(0, CTRL_SYNC, NULL);
    }
    return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    (void) lun;
    (void) offset; // offset is always 0 for standard block reads

    DRESULT res = disk_read(0, buffer, lba, bufsize / 512);
    return (res == RES_OK) ? (int32_t) bufsize : -1;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void) lun;
    return true;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void) lun;
    (void) offset; // offset is always 0 for standard block writes

    DRESULT res = disk_write(0, buffer, lba, bufsize / 512);
    
    // For small systems, flushing cache immediately keeps data safe on sudden unplug
    disk_ioctl(0, CTRL_SYNC, NULL);
    
    return (res == RES_OK) ? (int32_t) bufsize : -1;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize) {
    (void) bufsize;
    (void) buffer;

    // Handle cache sync command from SCSI
    if (scsi_cmd[0] == 0x35) { // SYNCHRONIZE_CACHE_10
        disk_ioctl(0, CTRL_SYNC, NULL);
        return 0;
    }

    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00); // Invalid Command Operation Code
    return -1;
}

// Dedicated loop for USB Mass Storage Mode
void usb_msc_mode_run(void) {
    printf("[USB MSC] Entering USB Mass Storage Mode. Mount partition on PC...\n");
    
    // Initialize standard disk I/O
    disk_initialize(0);
    
    // Initialize USB stack
    tusb_init();

    while (1) {
        tud_task();
    }
}
