#ifndef __API_REST_H__
#define __API_REST_H__

#include <stdint.h>

#define WIFI_SSID "PabloWiFi"
#define WIFI_PASSWORD "seminario"
// #define WIFI_SSID "Dacha Juncal 2.4"
// #define WIFI_PASSWORD "0123456789"

// Definición de las posibles fuentes de datos
typedef enum {
    SOURCE_PIO,
    SOURCE_USB,
    SOURCE_REST
} input_source_t;

// Variable global expuesta para que el main sepa qué fuente leer
extern volatile input_source_t current_source;

/**
 * @brief Conecta al Wi-Fi e inicializa el servidor HTTP REST
 */
void api_rest_init(void);

#endif // __API_REST_H__