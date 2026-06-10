# This is a copy of <PICO_SDK_PATH>/external/pico_sdk_import.cmake

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif ()

# Default to the location in the workspace pico folder if not set
if (NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "/home/rfocosi/workspace/pico-o2em/pico/pico-sdk")
endif ()

set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK")

if (NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "Directory '${PICO_SDK_PATH}' not found or does not contain pico_sdk_init.cmake")
endif ()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
