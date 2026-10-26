#include "api_rest.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"

#include "lwip/ip4_addr.h"
#include "lwip/init.h"
#include "lwip/apps/httpd.h"

#include <string.h>
#include <stdlib.h>

// Variable global que define el origen actual (PIO por defecto)
volatile input_source_t current_source = SOURCE_PIO;

// Referencia a la cola definida en el main para inyectar datos por REST
extern queue_t g_queue;

#if LWIP_HTTPD_SUPPORT_POST
// Puntero para gestionar la conexión POST actual de lwIP
static void *current_connection = NULL;
static char current_uri[32];
#endif


// 1. Inicialización de Wi-Fi y Servidor HTTP
void api_rest_init(void) {
    printf("Iniciando Wi-Fi (Modo STA)...\n");
    cyw43_arch_enable_sta_mode();

    // WIFI_SSID y WIFI_PASSWORD vienen definidos desde el CMakeLists.txt
    uint8_t attempts = 0;
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 5000) && attempts < 5) {
        printf("Error: No se pudo conectar al Wi-Fi. Reintentando...\n");
        sleep_ms(1000);
        attempts++;
    }
    if (attempts == 5) {
        printf("Error: No se pudo conectar al Wi-Fi después de varios intentos.\n");
        return;
    }
    printf("Conectado! IP Asignada: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

    // Iniciar servidor HTTP
    cyw43_arch_lwip_begin();
    httpd_init();
    cyw43_arch_lwip_end();
    
    printf("Servidor REST iniciado en puerto 80.\n");
}

#if LWIP_HTTPD_SUPPORT_POST

// 2. Filtro de inicio de petición POST
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
        u16_t http_request_len, int content_len, char *response_uri,
        u16_t response_uri_len, u8_t *post_auto_wnd) {
            
    // Bloquear si ya hay una conexión procesándose
    if (current_connection != NULL) return ERR_VAL;

    // Aceptar solo nuestros endpoints de la API
    if (strcmp(uri, "/api/source") == 0 || strcmp(uri, "/api/estimulo") == 0) {
        current_connection = connection;
        strncpy(current_uri, uri, sizeof(current_uri) - 1);
        *post_auto_wnd = 1;
        return ERR_OK;
    }
    return ERR_VAL;
}

// Helper para extraer parámetros 'x-www-form-urlencoded' (ej. source=USB)
static char *httpd_param_value(struct pbuf *p, const char *param_name, char *value_buf, size_t value_buf_len) {
    size_t param_len = strlen(param_name);
    u16_t param_pos = pbuf_memfind(p, param_name, param_len, 0);
    if (param_pos != 0xFFFF) {
        u16_t param_value_pos = param_pos + param_len;
        u16_t param_value_len = 0;
        u16_t tmp = pbuf_memfind(p, "&", 1, param_value_pos);
        if (tmp != 0xFFFF) {
            param_value_len = tmp - param_value_pos;
        } else {
            param_value_len = p->tot_len - param_value_pos;
        }
        if (param_value_len > 0 && param_value_len < value_buf_len) {
            char *result = (char *)pbuf_get_contiguous(p, value_buf, value_buf_len, param_value_len, param_value_pos);
            if (result) {
                result[param_value_len] = 0;
                return result;
            }
        }
    }
    return NULL;
}

// 3. Recepción y procesamiento del Payload
err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    err_t ret = ERR_VAL;
    if (p == NULL) return ret;
    
    if (current_connection == connection) {
        char buf[16]; // Buffer de extracción para los valores (ej. "REST" o "0xABCD")
        
        if (strcmp(current_uri, "/api/source") == 0) {
            char *val = httpd_param_value(p, "source=", buf, sizeof(buf));
            if (val) {
                if (strcmp(val, "PIO") == 0) current_source = SOURCE_PIO;
                else if (strcmp(val, "USB") == 0) current_source = SOURCE_USB;
                else if (strcmp(val, "REST") == 0) current_source = SOURCE_REST;
                ret = ERR_OK;
                printf("Fuente de datos cambiada a: %s\n", val);
            }
        } 
        else if (strcmp(current_uri, "/api/estimulo") == 0) {
            char *val = httpd_param_value(p, "data=", buf, sizeof(buf));
            if (val) {
                // Convertir string hex "0xABCD" a entero y encolar
                uint16_t rx_trama = (uint16_t)strtol(val, NULL, 16);
                printf("Trama REST recibida: 0x%04X\n", rx_trama);
                queue_try_add(&g_queue, &rx_trama);
                ret = ERR_OK;
            }
        }
    }
    
    pbuf_free(p); // Liberar siempre el buffer de red
    return ret;
}

// 4. Finalización y Respuesta HTTP
void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    if (current_connection == connection) {
        // Redirigir la respuesta a un HTML de éxito (o index) alojado en el servidor
        snprintf(response_uri, response_uri_len, "/index.shtml");
        current_connection = NULL;
    }
}
#endif