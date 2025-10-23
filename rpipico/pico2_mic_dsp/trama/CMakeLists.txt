add_library(trama STATIC src/trama.c)


target_include_directories(trama PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(trama
    pico_stdlib
)
