# Implementacion adquisicion de datos y FFT
Los datos son leidos por el adc para la lectura de valores `uint8_t` utilizando el fifo integrado de 8 elementos, los datos se almacenan utilizando el DMA.
Los buffers de datos rotan continuamente manejando indices de lectura y escritura para mantener la secuencia.

El core 1 de la pico2 normaliza el vector y utiliza `float32_t`, para luego realizar el calculo de la FFT.

La informacion se transmite en formato binario estructurado por USB CDC (`stdout`) para garantizar la máxima velocidad de transferencia, enviando la parte real e imaginaria de forma cruda.

## Configuracion principal
- FFT_SIZE: entero potencia de 2 (2^n). Configurado en 512.
- ADC_CLK_HZ: frecuencia de muestreo. Configurado a 16000 Hz.
- N_DATA_BUFFERS: cantidad de buffers a utilizar.
- __MEASURE_FFT_TIME__: definicion para habilitar el muestreo de tiempo de fft y desactivar el envio de datos. 

## Análisis de Rendimiento y Ancho de Banda
Al enviar un paquete binario de **2056 bytes** por frame (cabecera + parte real + parte imaginaria), los requerimientos de transmisión cambian según la frecuencia de muestreo elegida. Usar USB CDC (12 Mbps / ~1 MB/s real) evita el cuello de botella físico de un puerto UART estándar.

| Frecuencia (Fs) | Ancho de banda (Nyquist) | Resolución (Hz/bin) | Delay Adquisición (ms) | Tasa de transmisión | Ancho de Banda USB |
|---|---|---|---|---|---|
| **4800 Hz** | 2.4 kHz | ~9.37 Hz | 106.67 ms | ~19.3 KB/s | ~154 kbps |
| **8000 Hz** | 4.0 kHz | ~15.62 Hz | 64.00 ms | ~32.1 KB/s | ~257 kbps |
| **16000 Hz** | 8.0 kHz | ~31.25 Hz | 32.00 ms | ~64.3 KB/s | ~514 kbps |
| **22050 Hz** (AM) | 11.025 kHz | ~43.07 Hz | 23.22 ms | ~88.5 KB/s | ~708 kbps |
| **44100 Hz** (CD) | 22.05 kHz | ~86.13 Hz | 11.61 ms | ~177.1 KB/s | ~1.4 Mbps |
| **48000 Hz** (DVD) | 24.0 kHz | ~93.75 Hz | 10.67 ms | ~192.8 KB/s | ~1.5 Mbps |

*(Para mantener el tiempo real a 16 kHz, USB CDC es estrictamente necesario, ocupando apenas el ~6.4% de su capacidad total de 1 MB/s).*

## Verificacion de funcionamiento
Se incluye en la carpeta `python` a nivel base del proyecto que cuenta con el archivo `plotter.py` para verificar los valores transmitidos por serial en una grafica de barras.