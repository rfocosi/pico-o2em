#ifndef O2EM_SYSTEM_H
#define O2EM_SYSTEM_H

#include <stdint.h>
#include <stddef.h>

struct retro_vfs_interface;

// Setup system clock, timers, and USB/UART debugging stdout
void system_init(void);

// Get the custom virtual VFS interface to handle bios reads
struct retro_vfs_interface* system_get_vfs_interface(void);

// Keep CPU execution running at constant ~60Hz / 50Hz frame timing
void system_sync_frame(void);

#endif // O2EM_SYSTEM_H
