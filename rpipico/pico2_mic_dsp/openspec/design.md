# Diseño Técnico: Transmisión USB CDC (Memoria Contigua)

## Componentes y Modelado de Datos

Dado que se requiere enviar todo en crudo en un solo envío (`fwrite` único), la estructura **no puede tener punteros**. Si enviamos un puntero por puerto serie, la PC recibirá una dirección de memoria RAM de la Pico (ej. `0x20001000`), no los valores flotantes.

Para que la memoria sea contigua, proponemos este diseño ordenado para mantener alineación natural a 4 bytes:

```c
// Se asume que FFT_SIZE está definido globalmente (ej. en un CMake o config.h)
#ifndef FFT_SIZE
#define FFT_SIZE 1024
#endif

// Alineación y empaquetado para evitar padding indeseado
typedef struct __attribute__((packed, aligned(4))) {
    uint8_t  sync[2];       // 2 bytes (0xAA, 0x55)
    uint16_t num_bins;      // 2 bytes -> Total hasta acá: 4 bytes
    uint32_t sample_rate;   // 4 bytes -> Total: 8 bytes
    float    real_part[FFT_SIZE / 2]; // Arreglo contiguo
    float    imag_part[FFT_SIZE / 2]; // Arreglo contiguo
} fft_usb_packet_t;
```

## Arquitectura de Transmisión

La implementación en `utils.c` se simplifica a una única operación:

```c
void send_fft_data_usb(const fft_usb_packet_t *packet) {
    if (!packet) return;
    // Envía TODO el bloque de memoria contiguo en una sola instrucción
    fwrite(packet, sizeof(fft_usb_packet_t), 1, stdout);
    fflush(stdout);
}
```

### Integración Multicore (Core 1 a Core 0)
El cálculo de la FFT reside en el **Core 1**, mientras que la transmisión hacia USB debe realizarse desde el núcleo principal (**Core 0**). 

Para lograr esto sin condiciones de carrera (race conditions):
1. Se definirán dos buffers ping-pong estáticos globales: `fft_usb_packet_t usb_packets[2];`.
2. El **Core 1** rellenará un paquete con la parte real e imaginaria, y enviará **el puntero** de ese paquete a través del sistema de colas seguro (`pico/util/queue.h`).
3. El **Core 0** estará en su bucle `while(true)` esperando datos en la cola con `queue_try_remove`.
4. Al recibir el puntero, el Core 0 llamará a `send_fft_data_usb(packet)`.

### Protocolo de PC (Receptor)
La PC recibirá exactamente la memoria dumpeada:
1. `0xAA 0x55` (2 bytes)
2. `num_bins` (2 bytes)
3. `sample_rate` (4 bytes)
4. Bloque de floats reales
5. Bloque de floats imaginarios
