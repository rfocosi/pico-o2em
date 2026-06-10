cmake_minimum_required(VERSION 3.13)

# Include the Pico SDK build system definitions
include(pico_sdk_import.cmake)

project(pico-o2em C CXX ASM)

# Initialize the Pico SDK
pico_sdk_init()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Define project executable
add_executable(pico-o2em)

# Define project include directories
target_include_directories(pico-o2em PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../libretro-o2em
    ${CMAKE_CURRENT_LIST_DIR}/../libretro-o2em/src
    ${CMAKE_CURRENT_LIST_DIR}/../libretro-o2em/allegrowrapper
    ${CMAKE_CURRENT_LIST_DIR}/../libretro-o2em/libretro-common/include
)

# Compiler Flags & Defines
target_compile_definitions(pico-o2em PRIVATE
    STATIC_LINKING=1
    HAVE_VOICE=0
    __LIBRETRO__
    __PICO__
)

# Add Source Files
target_sources(pico-o2em PRIVATE
    # Wrapper files (Adapters)
    src/main.c
    src/pico_system.c
    src/pico_video.c
    src/pico_audio.c
    src/pico_input.c
    src/pico_cartridge.c
    src/composite_video.pio

    # Unmodified core source files
    ../libretro-o2em/src/audio.c
    ../libretro-o2em/src/cpu.c
    ../libretro-o2em/src/cset.c
    ../libretro-o2em/src/keyboard.c
    ../libretro-o2em/src/score.c
    ../libretro-o2em/src/table.c
    ../libretro-o2em/src/vdc.c
    ../libretro-o2em/src/vmachine.c
    ../libretro-o2em/src/voice.c
    ../libretro-o2em/src/vpp.c
    ../libretro-o2em/src/vpp_cset.c
    ../libretro-o2em/libretro.c
    ../libretro-o2em/allegrowrapper/wrapalleg.c
    ../libretro-o2em/src/vkeyb/ui.c
    ../libretro-o2em/src/vkeyb/vkeyb.c
    ../libretro-o2em/src/vkeyb/vkeyb_config.c
    ../libretro-o2em/src/vkeyb/vkeyb_layout.c

    # Unmodified libretro-common source files
    ../libretro-o2em/libretro-common/compat/compat_posix_string.c
    ../libretro-o2em/libretro-common/compat/compat_snprintf.c
    ../libretro-o2em/libretro-common/compat/compat_strcasestr.c
    ../libretro-o2em/libretro-common/compat/compat_strl.c
    ../libretro-o2em/libretro-common/compat/fopen_utf8.c
    ../libretro-o2em/libretro-common/encodings/encoding_crc32.c
    ../libretro-o2em/libretro-common/encodings/encoding_utf.c
    ../libretro-o2em/libretro-common/file/file_path.c
    ../libretro-o2em/libretro-common/file/file_path_io.c
    ../libretro-o2em/libretro-common/streams/file_stream.c
    ../libretro-o2em/libretro-common/streams/file_stream_transforms.c
    ../libretro-o2em/libretro-common/string/stdstring.c
    ../libretro-o2em/libretro-common/time/rtime.c
)

# Generate the PIO header from composite_video.pio
pico_generate_pio_header(pico-o2em ${CMAKE_CURRENT_LIST_DIR}/src/composite_video.pio)

# Link the executable with required Pico SDK hardware modules
target_link_libraries(pico-o2em
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_pwm
    hardware_i2c
    hardware_gpio
    hardware_clocks
)

# Enable stdout redirection over UART and USB (picotool / serial monitor)
pico_enable_stdio_uart(pico-o2em 1)
pico_enable_stdio_usb(pico-o2em 1)

# Generate hex, bin, uf2 and map file outputs automatically
pico_add_extra_outputs(pico-o2em)
