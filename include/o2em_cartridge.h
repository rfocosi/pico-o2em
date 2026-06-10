#ifndef O2EM_CARTRIDGE_H
#define O2EM_CARTRIDGE_H

#include <stdint.h>
#include <stddef.h>

#define ROM_BUFFER_MAX_SIZE 8192

// Global static buffer holding the dumped ROM
extern uint8_t rom_buffer[ROM_BUFFER_MAX_SIZE];
extern size_t rom_size_bytes;

// Perform the cartridge dumping routine
// Generates address lines via 74HC595 and reads DB0-DB7 parallel data
void cartridge_dump(void);

#endif // O2EM_CARTRIDGE_H
