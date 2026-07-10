# Tareas de Implementación

- [ ] **1. Modificar `utils/include/utils.h` y `utils/src/utils.c`**
  - Asegurar que la estructura `fft_usb_packet_t` y `send_fft_data_usb` estén completas y con alocación estática y alineada. *(Ya iniciado)*

- [ ] **2. Modificar `pico2_mic_dsp.c` (Arquitectura Multicore)**
  - Crear un arreglo de doble buffer: `fft_usb_packet_t usb_packets[2];`.
  - Reconfigurar `queue_init` para que acepte punteros a `fft_usb_packet_t *`.
  - En `core1_fft()`, copiar `fft_output` (parte real e imaginaria separada o como venga) hacia el buffer disponible y enviarlo por la cola con `queue_try_add`.
  - En `main()` (Core 0), hacer pop de la cola con `queue_try_remove` y llamar a `send_fft_data_usb()`.
