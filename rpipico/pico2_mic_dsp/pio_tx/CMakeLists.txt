add_library(pio_tx STATIC src/pio_tx.c)
target_include_directories(pio_tx PUBLIC include)
target_link_libraries(pio_tx pico_stdlib hardware_pio hardware_clocks)
pico_generate_pio_header(pio_tx ${CMAKE_CURRENT_LIST_DIR}/src/pio_tx.pio)