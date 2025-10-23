add_library(dsp STATIC src/dsp.c)

add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../../CMSIS_lib ${CMAKE_BINARY_DIR}/CMSIS_lib
)

target_include_directories(dsp PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(dsp
    pico_stdlib
    cmsis_lib
)
