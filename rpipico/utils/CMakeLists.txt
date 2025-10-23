# Crear la biblioteca estática "lcd" con los archivos fuente
add_library(utils STATIC
    src/utils.c
)

# Linkeo dependencias de la bibliotecas
target_link_libraries(utils
    pico_stdlib
    hardware_pwm
)

# Incluir las cabeceras de la biblioteca
target_include_directories(utils PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)
