# Contexto del Proyecto: Demo Académica de Implante Coclear

Este documento sirve como guía de contexto de alto nivel para agentes de inteligencia artificial (y desarrolladores) que colaboran en el repositorio.

---

## 1. Visión General del Proyecto

Este proyecto es una **demostración académica e interdisciplinaria** sobre el funcionamiento de un **Implante Coclear (IC)** para personas con pérdida o disminución auditiva severa/profunda.

El sistema emula la cadena completa de procesamiento de señal auditiva y transmisión inalámbrica de datos y energía:

1. **Captura de Audio Analógico**: Micrófono electret MAX9814 con control automático de ganancia (AGC).
2. **Procesamiento Digital de Señales (DSP - Placa Externa)**:
   - Basado en **Raspberry Pi Pico 2** (microcontrolador RP2350 / ARM Cortex-M33).
   - **Core 0**: Adquisición por ADC a 16 kHz (o configurable 8–64 kHz) usando DMA y buffers circulares.
   - **Core 1**: Cálculo de la Transformada Rápida de Fourier (FFT de 512 puntos) con la biblioteca **CMSIS-DSP** (`arm_cfft_f32`) y canalización espectral en bandas logarítmicas (bandas cocleares).
3. **Enlace Inductivo / RF (Transmisión a Implante Demo)**:
   - Modulación ASK/PWM sobre portadora de 125 kHz para transmitir tramas seriales de datos (16 bits) e inducción de energía inalámbrica hacia el dispositivo implantado mediante acoplamiento por bobinas.
4. **Recepción e Implante Demostrativo**:
   - Decodificación en tiempo real utilizando la unidad PIO de la Pico 2 (`pwm_capture.pio`) para medir anchos de pulso y reconstruir los datos seriales (visualización opcional en LCD I2C).
5. **Telemetría y Análisis en PC**:
   - Envío de paquetes binarios de espectro crudo por USB CDC (12 Mbps, ~2056 bytes/trama).
   - Software en Python (PyQt6 / pyqtgraph / sounddevice) para visualización en tiempo real, IFFT, simulación de vocoder de ruido blanco (8 canales), pruebas de inteligibilidad (STOI) y comparativa de rendimiento Pico 2 vs PC.

---

## 2. Diagrama de Arquitectura

```
                    ┌────────────────────────────────────────────────────────┐
                    │                    UNIDAD EXTERNA                      │
 ┌──────────────┐   │  ┌────────────┐    ┌─────────────────┐    ┌─────────┐  │   ┌──────────────┐
 │ Micrófono    │──>│  │ ADC + DMA  │───>│ FFT (CMSIS-DSP) │───>│  Trama  │──┼──>│ Transmisor   │
 │ MAX9814      │   │  │ (Core 0)   │    │ 512p (Core 1)   │    │  ASK    │  │   │  RF / Bobina │
 └──────────────┘   │  └────────────┘    └─────────────────┘    └─────────┘  │   └──────┬───────┘
                    └──────────────────────────┬─────────────────────────────┘          │ Enlace Inductivo
                                               │ Telemetría USB CDC                     ▼ (Portadora 125 kHz)
                                               ▼ (2056 bytes/frame)              ┌──────────────┐
                                   ┌──────────────────────┐                      │ Receptor     │
                                   │ PC / Software GUI    │                      │  RF / Bobina │
                                   │ - Real-time Plotter  │                      └──────┬───────┘
                                   │ - 8-band Vocoder     │                             │
                                   │ - STOI / Comparativa │                             ▼
                                   └──────────────────────┘                      ┌──────────────┐
                                                                                 │ RP2040/2350  │
                                                                                 │ Rec. Implant │
                                                                                 └──────────────┘
```

---

## 3. Estructura del Repositorio

```text
implante-coclear/
├── rpipico/                      # Firmware C/C++ para Raspberry Pi Pico 2 (Pico SDK)
│   ├── pico2_mic_dsp/            # Proyecto principal: Adquisición ADC + FFT (CMSIS-DSP) + USB CDC
│   │   ├── dsp/                  # Módulos de filtrado Biquad, FFT y canalización espectral
│   │   ├── trama/                # Empaquetado de tramas de 16 bits para transmisión
│   │   ├── pio_tx/               # Transmisión por PIO (Modulación ASK)
│   │   └── openspec/             # Especificaciones técnicas, bandwidth budget y arquitectura
│   ├── pico2_tx/                 # Emisor ASK por PWM (125 kHz) para el enlace por bobina
│   ├── pico2_rx/                 # Receptor ASK usando PIO (medición de anchos de pulso) y LCD I2C
│   ├── CMSIS_lib/                # Submódulo CMSIS / CMSIS-DSP optimizado para ARM Cortex-M
│   └── utils/                    # Herramientas auxiliares y CMake scripts
│
├── python/                       # Entorno de análisis, visualización y pruebas en PC
│   ├── menu.py                   # Menú GUI principal (PySide6) para ejecutar scripts con parámetros
│   ├── scripts/                  # Suite de herramientas DSP y evaluación:
│   │   ├── cochlear_plotter.py   # Simulador de implante coclear (Vocoder de ruido en 8 bandas)
│   │   ├── fft_plotter.py        # Inspector espectral en tiempo real (Real, Imag, Magnitud, IFFT)
│   │   ├── fft_audio_recorder.py # Reconstrucción e IFFT de audio directo a WAV
│   │   ├── dual_audio_recorder.py# Grabación simultánea espectro FFT + Vocoder
│   │   ├── pico_intelligibility_stoi.py # Evaluación de inteligibilidad (métrica STOI)
│   │   ├── pico_pc_comparator.py # Comparativa de precisión FFT Pico 2 vs PC
│   │   ├── biquad_calculator.py  # Diseñador y generador de coeficientes Biquad
│   │   └── wav_to_mp3.py         # Utilidad de conversión de audio
│   ├── plot_real_time/           # Aplicación multihilo (SerialThread + PyQt) para gráficos continuos
│   └── images/                   # Capturas y gráficos comparativos de pruebas espectrales
│
└── agents/                       # Documentación para Agentes AI y seguimiento de tareas
    ├── AGENTS.md                 # (Este archivo) Contexto general del proyecto
    ├── CONTEXT.md                # Copia/enlace de contexto
    └── issue-16.md               # Tarea de frecuencia de muestreo dinámica (ADC 8kHz - 64kHz)
```

---

## 4. Protocolo de Datos Serial / USB (`fft_usb_packet_t`)

Para el análisis en PC por USB CDC, el firmware transmite iterativamente estructuras binarias empaquetadas de **2056 bytes**:

```c
typedef struct __attribute__((packed, aligned(4))) {
    uint8_t  sync[2];       // 0xAA, 0x55 (Cabecera de sincronización)
    uint16_t num_bins;      // 256 bins de frecuencia
    uint32_t sample_rate;   // Frecuencia de muestreo (ej: 16000 Hz)
    float    real_part[256]; // Componente Real de la FFT (32 bits IEEE 754)
    float    imag_part[256]; // Componente Imaginaria de la FFT (32 bits IEEE 754)
} fft_usb_packet_t;
```

---

## 5. Directrices para Agentes de Inteligencia Artificial

- **No reinventar lógica existente**: Consultar `python/scripts/` para utilidades DSP o `rpipico/pico2_mic_dsp/dsp/` antes de escribir nuevos algoritmos.
- **Desacoplamiento Multihilo**: Las aplicaciones GUI en Python deben procesar el puerto serie/USB en hilos secundarios (`QThread` / `threading`) con decimación en el dibujado gráfico (~3 FPS) para no interrumpir la reproducción continua de audio a 16 kHz.
- **Frecuencia de Muestreo Dinámica**: Cualquier modificación que altere la frecuencia del ADC en la Pico 2 debe actualizar sincrónicamente los coeficientes de filtrado y los parámetros de la FFT.
