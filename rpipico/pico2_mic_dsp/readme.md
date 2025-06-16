# Implementacion adquisicion de datos y FFT
Los datos son leidos por el adc para la lectura de valores `uint8_t` utilizando el fifo integrado de 8 elementos, los datos se almacenan utilizando el DMA.
Los buffers de datos rotan continuamente manejando indices de lectura y escritura para mantener la secuencia.

El core 1 de la pico2 normaliza el vector y utiliza `float32_t`, para luego realizar el calculo de la FFT.

La informacion se envia uilizando `send_freq_magnitude_pairs` donde envia los datos en csv con el formato `freq:mag` ambos flotantes.

## Configuracion principal
- FFT_SIZE: entero potencia de 2 (2^n).
- ADC_CLK_KHZ: frecuencia de muestreo en kHz.
- N_DATA_BUFFERS: cantidad de buffers a utilizar.
- __MEASURE_FFT_TIME__: definicion para habilitar el muestreo de tiempo de fft y desactivar el envio de datos. 

## Verificacion de funcionamiento
Se incluye en la carpeta `python` a nivel base del proyecto que cuenta con el archivo `plotter.py` para verificar los valores transmitidos por serial en una grafica de barras.