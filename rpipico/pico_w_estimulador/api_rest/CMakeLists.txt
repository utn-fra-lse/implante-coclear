# api_rest/CMakeLists.txt

# Empaquetar los archivos web (HTML, SHTML) en el sistema de archivos de lwIP
pico_add_library(pico_httpd_content NOFLAG)
pico_set_lwip_httpd_content(pico_httpd_content INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/content/404.html
        ${CMAKE_CURRENT_LIST_DIR}/content/index.shtml
)

# Crear la librería estática de la API REST
add_library(api_rest STATIC
    api_rest.c
)

# Incluir el directorio actual (donde está lwipopts.h) y los headers del httpd
target_include_directories(api_rest PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${PICO_LWIP_CONTRIB_PATH}/apps/httpd
)

# Enlazar las dependencias necesarias de lwIP y el contenido web generado
target_link_libraries(api_rest PUBLIC
        pico_stdlib
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_http
        pico_lwip_mdns
        pico_httpd_content
)