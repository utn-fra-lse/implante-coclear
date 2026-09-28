add_library(mcp4725 STATIC
    mcp4725.c
)

# Linkeo dependencias de la bibliotecas
target_link_libraries(mcp4725
    pico_stdlib
    hardware_i2c
)

# Incluir las cabeceras de la biblioteca
target_include_directories(mcp4725 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)