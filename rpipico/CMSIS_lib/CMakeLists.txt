# Crear libreria estatica
set(CMSIS_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS/CMSIS)
set(CMSIS_DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DSP)

add_library(cmsis_lib STATIC
    ${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_rfft_fast_f32.c
    ${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_rfft_fast_init_f32.c
    ${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_cfft_f32.c
    ${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_cfft_init_f32.c
    ${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_cfft_radix8_f32.c
    ${CMSIS_DSP_DIR}/Source/TransformFunctions/arm_bitreversal2.c
    ${CMSIS_DSP_DIR}/Source/CommonTables/arm_common_tables.c
    ${CMSIS_DSP_DIR}/Source/CommonTables/arm_const_structs.c
    ${CMSIS_DSP_DIR}/Source/BasicMathFunctions/arm_mult_f32.c
    ${CMSIS_DSP_DIR}/Source/BasicMathFunctions/arm_abs_f32.c
    ${CMSIS_DSP_DIR}/Source/ComplexMathFunctions/arm_cmplx_mag_f32.c
    ${CMSIS_DSP_DIR}/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
    ${CMSIS_DSP_DIR}/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
)

# Define target CPU macro for CMSIS-DSP
target_compile_definitions(cmsis_lib PRIVATE ARM_MATH_CM33)

# Incluir las cabeceras de la biblioteca
target_include_directories(cmsis_lib PUBLIC
    ${CMSIS_CORE_DIR}/Core/Include
    ${CMSIS_DSP_DIR}/Include
    ${CMSIS_DSP_DIR}/PrivateInclude
)
