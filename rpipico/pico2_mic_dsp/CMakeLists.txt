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

# CMSIS-DSP setup
set(CMSIS_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/CMSIS/CMSIS)
set(CMSIS_DSP_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/lib/CMSIS-DSP)

include_directories(
    ${CMSIS_CORE_DIR}/Core/Include
    ${CMSIS_DSP_DIR}/Include
    ${CMSIS_DSP_DIR}/PrivateInclude
)

file(GLOB CMSIS_DSP_SRC
    "${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_rfft_fast_f32.c"
    "${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_rfft_fast_init_f32.c"
    "${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_cfft_f32.c"
    "${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_cfft_init_f32.c"  # <-- ADD THIS
    "${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_cfft_radix8_f32.c"
    "${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_bitreversal2.c"
    "${CMSIS_DSP_DIR}/Source/CommonTables/arm_common_tables.c"
    "${CMSIS_DSP_DIR}/Source/CommonTables/arm_const_structs.c"
    "${CMSIS_DSP_DIR}/Source/BasicMathFunctions/arm_mult_f32.c"
    "${CMSIS_DSP_DIR}/Source/BasicMathFunctions/arm_abs_f32.c"
    "${CMSIS_DSP_DIR}/Source/ComplexMathFunctions/arm_cmplx_mag_f32.c"
)


# Main project sources
set(PROJECT_SOURCES
    pico2_mic_dsp.c
    ${CMSIS_DSP_SRC}
)

# Create the executable with all sources
add_executable(pico2_mic_dsp ${PROJECT_SOURCES})

# Project info
pico_set_program_name(pico2_mic_dsp "pico2_mic_dsp")
pico_set_program_version(pico2_mic_dsp "0.1")

# USB stdio output only
pico_enable_stdio_uart(pico2_mic_dsp 0)
pico_enable_stdio_usb(pico2_mic_dsp 1)

# Link with required libraries
target_link_libraries(pico2_mic_dsp
    pico_stdlib
    hardware_adc
    hardware_dma
    pico_multicore
)

# Include project dir
target_include_directories(pico2_mic_dsp PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Define target CPU macro for CMSIS-DSP
target_compile_definitions(pico2_mic_dsp PRIVATE ARM_MATH_CM33)

# Generate UF2 and other outputs
pico_add_extra_outputs(pico2_mic_dsp)
