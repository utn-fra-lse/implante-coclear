# Presupuesto de Datos y Ancho de Banda (Data Budget)

## 1. Parámetros Base del Sistema
Extraídos del archivo `dsp.h` y del diseño propuesto:
- **`FFT_SIZE`**: 512 muestras.
- **`ADC_CLK_HZ`**: 4800 Hz (según la macro `MAX_FREQ * SAMPLE_MULTIPLIER` = 2400 * 2) *Nota: el comentario dice 8kHz, calcularemos también el peor caso*.
- **`NUM_BINS`**: 256 (FFT_SIZE / 2).

## 2. Tamaño del Paquete (Payload por Frame)
| Dato | Tamaño |
|---|---|
| Cabecera (Sync, Sample Rate, Num Bins) | 8 bytes |
| Parte Real (256 floats de 32 bits) | 1024 bytes |
| Parte Imaginaria (256 floats de 32 bits) | 1024 bytes |
| **Total por Frame de FFT** | **2056 bytes** |

## 3. Tasa de Generación (Throughput Requerido)

### Escenario A: `ADC_CLK_HZ` = 4800 Hz (Valor de las macros)
- **Tiempo por Frame (Llenar el buffer ADC):** `512 muestras / 4800 Hz = 106.66 ms`
- **Frames por segundo (FPS):** `4800 / 512 = 9.375 FPS`
- **Ancho de banda requerido (Bytes/s):** `9.375 FPS * 2056 bytes = 19.275 KB/s`
- **Ancho de banda requerido (bps):** `~154.2 kbps`

### Escenario B: `ADC_CLK_HZ` = 8000 Hz (Peor caso / Comentario en código)
- **Tiempo por Frame:** `512 / 8000 = 64 ms`
- **Frames por segundo:** `15.625 FPS`
- **Ancho de banda requerido:** `15.625 * 2056 = 32.125 KB/s`
- **Ancho de banda requerido (bps):** `~257 kbps`

## 4. Análisis de Viabilidad e Impacto

**Si hubiéramos usado el hardware UART físico (Pines TX/RX):**
El baud rate estándar de `115200 bps` te da una tasa máxima real de unos `11.5 KB/s`. 
- Requerimiento: `19.2 KB/s` (o `32 KB/s`).
- **Conclusión UART físico:** Habrías tenido un cuello de botella gravísimo. Se te hubiera colgado el programa y perdido buffers por timeout del DMA a menos que subieras el baud rate del hardware UART a `460800` o `921600`.

**Usando el USB CDC (Virtual COM vía `stdout` / `pico_enable_stdio_usb`):**
El estándar USB 1.1 Full Speed de la Raspberry Pi Pico tiene un bus de `12 Mbps`. La tasa de transferencia *Bulk* práctica para el CDC ronda los `1 MB/s` (o `1000 KB/s`).
- Requerimiento: `32 KB/s` (máximo).
- Capacidad USB: `1000 KB/s`.
- Ocupación del bus USB: **~3.2%**.
- **Conclusión USB CDC:** Estás completamente sobrado de ancho de banda. Transmitir el paquete de 2 KB tomará menos de 2 milisegundos, dándole a tu procesador (y al Core 1) unos 104 ms de tiempo ocioso antes de que llegue el siguiente frame. La adquisición de audio no se va a interrumpir en lo absoluto.
