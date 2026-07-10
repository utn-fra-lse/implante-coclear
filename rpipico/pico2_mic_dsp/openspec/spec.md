# Especificación: Transmisión USB CDC Estructurada

## Requisitos y Alcance
- Volver a utilizar `stdout` para la transmisión, de forma que los datos salgan por el puerto serie virtual (USB CDC).
- Definir un `struct` empaquetado (packed) que contenga la metadata y los datos alineados, de manera que ocupe un bloque contiguo en memoria.
- Proveer la función `send_fft_data_usb(const fft_usb_packet_t *packet)` que envíe toda la estructura en una única llamada.

## Estructura
La estructura de datos debe evitar usar punteros, ya que enviar un puntero por USB envía la dirección de memoria y no los datos reales. Debe incluir los arreglos directamente o utilizar un `Flexible Array Member` si los datos están intercalados.

## Criterios de Aceptación
1. La cabecera `utils.h` debe definir el struct garantizando alineación de 4 bytes (e.g. `__attribute__((packed, aligned(4)))`).
2. El envío utilizará una sola llamada a `fwrite` apuntando a la estructura completa.
3. Ya no se usa la API de hardware de UART nativa.
