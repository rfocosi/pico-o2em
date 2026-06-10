# Pico-O2EM: Bare-Metal Magnavox Odyssey 2 Emulator for RP2040

Pico-O2EM is a high-performance, bare-metal port of the `libretro-o2em` Magnavox Odyssey 2 / Philips Videopac G7000 emulator, running on the Raspberry Pi Pico (RP2040) microcontroller. 

By utilizing a Clean Code architecture, hardware-specific adapters, and tight memory optimizations, Pico-O2EM runs the emulation in real-time with analog NTSC/PAL composite video output and PWM audio. It supports both physical cartridge dumping at boot and loading ROMs from an internal virtual VFAT flash partition exposed over USB.

---

## 1. Key Features

- **Pristine Core Dependency**: Compiles directly against the unmodified `libretro-o2em` emulator core.
- **Composite Video Output**: Bypasses display controllers using a custom RP2040 PIO state machine and a 3-pin resistor DAC to generate analog H-sync, V-sync, and luma levels.
- **PWM Audio**: Emits dual-mono PWM audio outputs with RC filtering.
- **Dual Boot Methods**:
  - **Physical Cartridge Dumper**: Reads original game cartridges on GP6-GP13 using address shift registers (74HC595).
  - **Internal Virtual Flash Drive**: Mounts a VFAT filesystem in the internal flash memory, auto-scanning for `.bin`/`.rom` files.
- **USB Mass Storage Fallback**: Behaves as a standard USB flash drive when plugged into a PC to easily drag-and-drop game ROMs (runs MSC-only TinyUSB mode).
- **Physical Input Support**:
  - **Joysticks**: Decodes two classic digital joysticks using a 74HC165 shift register daisy chain.
  - **Keyboard**: Scans the Odyssey alphanumeric keyboard matrix using an MCP23017 I2C port expander.

---

## 2. System Architecture

To keep the codebase maintainable and decoupled from target hardware, Pico-O2EM implements a **Clean Code Architecture**. 

- **Domain/Use-Case Layer**: Decoupled domain interfaces define the emulated hardware peripherals. The Libretro frontend orchestration logic is stored in [main.c](./src/main.c).
- **Interface Layer**: Abstract definitions for video, audio, inputs, system clocks, and cartridges ([o2em_video.h](./include/o2em_video.h), etc.).
- **Adapter Layer (Hardware-Specific)**: Concrete RP2040 implementations that bridge domain interfaces to the Pico SDK APIs.
- **Unmodified Emulator Core**: Link-time inclusion of `libretro-o2em`.

### Memory Footprint Optimization
The RP2040 has 264 KB of SRAM, whereas the core originally allocated >380 KB for static line buffers and textures. The adapter layer applies local overrides to fit within the memory map:
- **Texture Shadowing**: Shadowed `wrapalleg.h` resizes the allegro virtual screen buffer to 340x250 (saving ~70 KB).
- **Line Compression**: Shadowed `vmachine.h` crops maximum lines to 312 and compresses core `snapedlines` via bitmasks (saving ~288 KB).
- **Memory Sharing**: The 4KB FatFs sector cache in [pico_diskio.c](./src/pico_diskio.c) reuses the global `rom_buffer` array. Since write caching is only active in USB MSC mode (when emulation is halted), this saves 4 KB of BSS RAM.

---

## 3. Hardware Mapping & Pinout Specification

Connect the Raspberry Pi Pico to external chips and RCA connectors according to the table below:

| Pico Pin | GPIO Pin | Device / Peripheral | Target Chip Pin | Technical Function / Signal |
| :--- | :--- | :--- | :--- | :--- |
| **Pin 36** | **3V3** | Logic ICs, MCP23017 | VCC / VDD | 3.3V Power Line |
| **Pin 40** | **VBUS** | Cartridge Slot | VCC | +5V Power Line for Cartridge |
| **Various** | **GND** | RCA Sockets, Slot, ICs | GND | Common ground reference |
| **Pin 34** | **GPIO 28** | Audio RCA (White) | RCA Left | PWM audio generation (Left) via RC filter |
| **Pin 32** | **GPIO 27** | Audio RCA (Red) | RCA Right | PWM audio generation (Right) via RC filter |
| **Pin 26** | **GPIO 20** | Video RCA (Yellow) | Resistor 1KΩ | Composite Video: Sync / Low Luma |
| **Pin 27** | **GPIO 21** | Video RCA (Yellow) | Resistor 470Ω | Composite Video: Mid Luma |
| **Pin 29** | **GPIO 22** | Video RCA (Yellow) | Resistor 220Ω | Composite Video: High Luma |
| **Pin 14** | **GPIO 10** | 2x 74HC165 (Joysticks) | Pin 2 (CP) | Clock signal |
| **Pin 15** | **GPIO 11** | 2x 74HC165 (Joysticks) | Pin 1 (PL/LD) | Latch/Load signal |
| **Pin 16** | **GPIO 12** | 2x 74HC165 (Joysticks) | Pin 9 (Q7) | Serial Data Input |
| **Pin 6** | **GPIO 4** | MCP23017 (Keyboard) | Pin 13 (SDA) | I2C Data line (needs 4.7KΩ pull-up) |
| **Pin 7** | **GPIO 5** | MCP23017 (Keyboard) | Pin 12 (SCL) | I2C Clock line (needs 4.7KΩ pull-up) |
| **Pin 19** | **GPIO 14** | 2x 74HC595 (Dumper) | Pin 14 (SER) | Serial Data Input (Address generator) |
| **Pin 20** | **GPIO 15** | 2x 74HC595 (Dumper) | Pin 11 (SRCLK) | Shift Register Clock |
| **Pin 21** | **GPIO 16** | 2x 74HC595 (Dumper) | Pin 12 (RCLK) | Latch / Storage Register Clock |
| **Pin 22** | **GPIO 17** | Cartridge Slot | Pin F (~PSEN) | Chip Enable (Active Low) |
| **Pin 24** | **GPIO 18** | Cartridge Slot | Pin 12 (P11) | Bank Select High |
| **Pin 25** | **GPIO 19** | Cartridge Slot | Pin 13 (P10) | Bank Select Low |
| **Pins 8-15** | **GPIO 6-13**| Cartridge Slot | Pins 2-9 (DB0-DB7)| Parallel data read bus |

---

## 4. How to Build

### Prerequisites
1. Install the GCC ARM Embedded Toolchain:
   ```bash
   sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
   ```
2. Clone this repository with submodules (for `libretro-o2em` and `pico-sdk`):
   ```bash
   git clone --recursive <repository_url>
   ```

### Compile
Ensure `PICO_SDK_PATH` is exported or configured correctly, then run:
```bash
cd pico-o2em
mkdir -p build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

This compiles the project and generates `pico-o2em.uf2` in the `build` directory.

---

## 5. How to Install

To load the compiled emulator onto your Raspberry Pi Pico:

1. Unplug the USB cable from the Pico.
2. Press and hold the **BOOTSEL** button on the Pico board.
3. While holding the button, connect the Pico to your PC using a USB cable.
4. Release the **BOOTSEL** button. The Pico will mount on your PC as a mass storage drive named **RPI-RP2**.
5. Copy or drag-and-drop the `pico-o2em.uf2` file from the `build/` directory onto the **RPI-RP2** drive.
6. The Pico will flash the firmware, automatically reboot, and begin executing.

---

## 6. How to Use

### Loading Game ROMs

1. **Physical Cartridge**:
   - Insert an Odyssey 2 cartridge into the slot and power up the Pico.
   - The Pico reads the cartridge parallel bus, loads the program into SRAM, and launches the emulator.

2. **Internal Virtual Flash Partition**:
   - If no cartridge is inserted, the parallel bus pulls high (reading all `0xFF`), and the bootloader falls back to mounting the internal VFAT partition.
   - The system automatically scans the root folder of the virtual drive and loads the first `.bin` or `.rom` file.

3. **Writing ROMs / USB Mass Storage Mode**:
   - To copy games to the internal drive, connect the Pico's USB port to a PC while **holding the Player 1 Joystick Action button** (button B) at boot, or boot the board with **no cartridge and no ROM files** present.
   - The Pico will mount as a standard drive named **"Odyssey 2 Flash Drive"**. 
   - Drag and drop your game files (e.g. `kansas.bin`) onto the drive. Unplug and reset the console to play.
   - If the virtual partition is empty/blank on first boot, the system formats it automatically and creates a `README.TXT` guide file.
