# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT: VSCode auto-detects Pico SDK via this include ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ===============================================================

set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK
include(pico_sdk_import.cmake)

project(pico2_mic_dsp C CXX ASM)

# Initialize Pico SDK
pico_sdk_init()

# Create the executable with all sources
add_executable(pico2_mic_dsp pico2_mic_dsp.c)

# Project info
pico_set_program_name(pico2_mic_dsp "pico2_mic_dsp")
pico_set_program_version(pico2_mic_dsp "0.1")

# USB stdio output only
pico_enable_stdio_uart(pico2_mic_dsp 0)
pico_enable_stdio_usb(pico2_mic_dsp 1)


add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../utils ${CMAKE_BINARY_DIR}/utils
)

# Link with required libraries
target_link_libraries(pico2_mic_dsp
    pico_stdlib
    hardware_adc
    hardware_dma
    hardware_pwm
    pico_multicore
    utils
)

# Include project dir
target_include_directories(pico2_mic_dsp PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)


# Generate UF2 and other outputs
pico_add_extra_outputs(pico2_mic_dsp)


add_subdirectory(dsp)
add_subdirectory(trama)
add_subdirectory(pio_tx)
target_link_libraries(${PROJECT_NAME} dsp trama pio_tx)