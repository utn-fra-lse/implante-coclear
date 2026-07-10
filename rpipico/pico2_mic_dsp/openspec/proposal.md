# Propuesta: Transmisión de datos FFT por UART (Binario)

## Contexto
Actualmente, el proyecto envía datos a través de `stdout` usando texto formateado. El objetivo es mejorar este método de transmisión enviando las componentes real e imaginaria de la FFT sin formateo (crudo/binario), agrupando la información mediante un `struct` y encapsulando el envío en una función dedicada.

## Diseño Propuesto

### Estructura de Datos
Se definirá un `struct` en `utils.h` para agrupar los datos a transmitir:

```c
typedef struct {
    uint32_t sample_rate;
    uint16_t num_bins;      // Cantidad de elementos en los arreglos
    float *real_part;       // Puntero al arreglo de componentes reales
    float *imag_part;       // Puntero al arreglo de componentes imaginarias
} fft_uart_payload_t;
```

### Función de Transmisión
Se implementará una nueva función en `utils.c`:

```c
void send_fft_data_struct(const fft_uart_payload_t *payload);
```

**Comportamiento de la función:**
1. Enviar Header de sincronización (e.g. `0xAA 0x55`).
2. Enviar metadata cruda (`sample_rate` y `num_bins`).
3. Enviar bloque de memoria de `real_part` (`fwrite(payload->real_part, sizeof(float), payload->num_bins, stdout)`).
4. Enviar bloque de memoria de `imag_part` (`fwrite(payload->imag_part, sizeof(float), payload->num_bins, stdout)`).
5. Flush (`fflush(stdout)`).

### Análisis de Riesgos y Dudas
- **Salida estándar vs UART físico**: El `CMakeLists.txt` deshabilita el UART físico y habilita el USB CDC (`pico_enable_stdio_usb`). La transmisión se hará hacia `stdout` enviando datos binarios al puerto serie virtual del USB. Si se requiere salir por el UART de hardware (pines TX/RX), se deberá inicializar el periférico `uart0` o `uart1`.
