# Herramientas DSP y Análisis FFT (Serial)

Este directorio contiene un conjunto de scripts diseñados para recibir, procesar y visualizar tramas binarias de Transformada Rápida de Fourier (FFT) calculadas en hardware (ej. Raspberry Pi Pico 2) y enviadas por puerto Serial (USB CDC).

Estos scripts están optimizados para **alto rendimiento**, calculando la matemática faltante (Magnitudes e IFFT) y renderizando los gráficos a tasas manejables (~3 FPS) mientras procesan el audio al 100% de la velocidad real (sin cuellos de botella).

## 🛠 Requisitos previos
Para usar cualquiera de los scripts, asegurate de tener instaladas las dependencias del proyecto:
```bash
pip install pyserial numpy pyqtgraph PyQt6 sounddevice scipy
```

## 📦 Formato del Paquete (Struct C)
Todos los scripts asumen que el microcontrolador envía iterativamente el siguiente bloque crudo de **2056 bytes** por el serial:
```c
typedef struct __attribute__((packed, aligned(4))) {
    uint8_t  sync[2];       // 0xAA, 0x55 (Header)
    uint16_t num_bins;      // 256
    uint32_t sample_rate;   // 16000
    float    real_part[256];
    float    imag_part[256];
} fft_usb_packet_t;
```

---

## 🖥 1. El Inspector FFT (`fft_plotter.py`)
Es la herramienta principal para depurar la matemática de la FFT cruda que sale de la placa. Muestra 4 gráficos simultáneos fijados estáticamente para un análisis fácil.

- **Qué hace:** Encuentra el paquete, grafica la parte Real, parte Imaginaria, calcula la Magnitud ($\sqrt{R^2 + I^2}$) y reconstruye la señal en el tiempo mediante la IFFT.
- **Uso:**
  ```bash
  python scripts/fft_plotter.py COM8
  ```
  *(Opcional: agregá `--play-audio` para escuchar la salida IFFT por los parlantes de tu PC).*

---

## 🎧 2. El Grabador de Audio (`fft_audio_recorder.py`)
Un script estrictamente por consola, ideal para cuando no querés consumir CPU renderizando gráficos y sólo te interesa testear la calidad de captura del micrófono.

- **Qué hace:** Reconstruye el audio temporal (IFFT), lo reproduce en vivo por los parlantes, y va acumulando todo en memoria RAM. Cuando pulsás `Ctrl+C`, escupe un archivo `.wav` limpio.
- **Uso:**
  ```bash
  python scripts/fft_audio_recorder.py COM8 --output grabacion_prueba.wav
  ```

---

## 🦻 3. El Simulador de Implante Coclear (`cochlear_plotter.py`)
Un simulador clásico (Vocoder de ruido blanco) que replica cómo escucha el mundo una persona con un implante coclear de 8 electrodos.

- **Qué hace:** Agrupa los 256 bins de la FFT en **8 bandas de frecuencia** distribuidas logarítmicamente (emulando la membrana basilar). Grafica la energía de esos 8 canales con barras de neón.
- **La magia del Vocoder:** Si lo corrés con audio, el script genera ruido blanco inyectando fases aleatorias al espectro, pero usando las magnitudes (energías) de tus 8 bandas. Al aplicar la IFFT, el resultado es ruido blanco perfectamente moldeado por tu voz en 8 bandas espectrales.
- **Uso:**
  ```bash
  # Agregá --gain para multiplicar el volumen si se escucha bajo
  python scripts/cochlear_plotter.py COM8 --play-audio --gain 5.0
  ```

---

## 💡 Notas de Arquitectura
* **El Mito del Baudrate**: Aunque los scripts aceptan el argumento `--baudrate` (por defecto 115200), al utilizar la Raspberry Pi Pico 2 a través de su puerto USB nativo (USB CDC / Virtual COM), **el baudrate es ignorado por el hardware**. La placa transmite a la velocidad nativa *USB Full Speed* (12 Mbps). Es por esto que los cuellos de botella clásicos del UART no aplican y no se pierden paquetes de audio.
* **Decimación de Interfaz**: Para no congelar tu PC, los scripts con GUI (`fft_plotter` y `cochlear_plotter`) tienen una capa de decimación. El hilo de fondo procesa audio para `sounddevice` a la velocidad brutal que imponga el USB, pero la ventana solo se dibuja 1 de cada 10 frames (~3 FPS).
