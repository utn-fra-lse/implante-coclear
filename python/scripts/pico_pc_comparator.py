"""
Comparador FFT / Banco de filtros: micrófono de la PC vs Pico.

Graba `--duration` segundos (6s por defecto) simultáneamente desde el micrófono de la PC
y desde el stream de la Pico, replica en Python la misma cadena DSP que corre en el
microcontrolador (HPF 250Hz -> LPF 6000Hz -> ventana deslizante -> Hanning -> FFT) sobre
el audio de la PC, y calcula:

  1. FFT PC vs FFT Pico: espectro de magnitud promedio (en el tiempo) de cada lado.
  2. Banco de filtros (3 vías): energía promedio por banda calculada desde (a) el audio
     de la PC, (b) la FFT cruda que manda la Pico pero con el algoritmo de bandas de
     Python, y (c) las bandas que la propia Pico calculó y envió (`band_energies`).

Este script solo graba y calcula los datos; para graficarlos hay que guardarlos con
`--save-csv` y correr el script compañero `plot_comparativa_csv.py` sobre ese CSV.

Limitación física (no de código): no hay forma de sincronizar en fase el micrófono de la
PC con el de la Pico (relojes independientes, distinta latencia, distinta sensibilidad y
posición de cada micrófono). Por eso la comparación es sobre espectros PROMEDIADOS en el
tiempo, no cuadro a cuadro — compara forma espectral y algoritmo, no fase ni nivel
absoluto exacto (cada serie se normaliza por su propio pico solo para graficar).
"""
import sys
import argparse
import csv
import time
import queue
import serial
import numpy as np
import scipy.signal as scipy_signal
import sounddevice as sd

FFT_SIZE = 512
HOP_SIZE = 256          # DMA_BLOCK_SIZE en el firmware
SAMPLE_RATE = 16000     # EFFECTIVE_SAMPLE_RATE en el firmware
N_BANDS = 8

# Ganancia lineal provisoria que usa el firmware (dsp.h: ENERGY_TO_AMPLITUDE_GAIN) para
# revertir out_data/band_energies a la misma escala que un promedio de magnitud FFT.
FIRMWARE_ENERGY_TO_AMPLITUDE_GAIN = 65535.0 / 16.0

# Mismos límites de bin que LOG_BAND_RANGES (dsp.h) / bands (cochlear_plotter.py).
BANDS = [
    (6, 10), (10, 16), (16, 25), (25, 40), (40, 64), (64, 102), (102, 162), (162, 256)
]
BAND_LABELS = [
    "200-300 Hz", "300-500 Hz", "500-780 Hz", "780-1.2 kHz",
    "1.2-2.0 kHz", "2.0-3.1 kHz", "3.1-5.0 kHz", "5.0-8.0 kHz",
]

# --- Cadena DSP replicada de rpipico/pico2_mic_dsp/dsp/src/dsp.c ---
# Coeficientes CMSIS-DSP literales {b0, b1, b2, a1, a2} (a0=1.0 implícito). CMSIS usa
# signo POSITIVO en la realimentación: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
#                                              + a1*y[n-1] + a2*y[n-2]
# mientras que scipy.signal.lfilter espera la convención estándar (signo negativo), así
# que para usar estos mismos coeficientes hay que armar a_scipy = [1, -a1, -a2].
HPF_SECTIONS_CMSIS = [
    (0.8795613790064433, -1.7591227580128865, 0.8795613790064433, 1.8250960051409633, -0.8339268642555546),
    (1.0, -2.0, 1.0, 1.9184107565980042, -0.927693125891298),
]
LPF_SECTIONS_CMSIS = [
    (0.5, 0.5, 0.0, -0.0, 0.0),
]


def cmsis_sections_to_scipy(sections):
    filters = []
    for b0, b1, b2, a1, a2 in sections:
        b = np.array([b0, b1, b2], dtype=np.float64)
        a = np.array([1.0, -a1, -a2], dtype=np.float64)
        filters.append((b, a))
    return filters


HPF_FILTERS = cmsis_sections_to_scipy(HPF_SECTIONS_CMSIS)
LPF_FILTERS = cmsis_sections_to_scipy(LPF_SECTIONS_CMSIS)


class CascadeFilter:
    """Replica arm_biquad_cascade_df1_f32: aplica varias secciones biquad en cascada,
    manteniendo el estado (zi) entre llamadas sucesivas — igual que la Pico, que filtra
    de forma continua entre frames en vez de resetear el filtro en cada bloque."""

    def __init__(self, sections):
        self.sections = sections
        self.zi = [scipy_signal.lfilter_zi(b, a) for b, a in sections]

    def process(self, x):
        y = x
        for i, (b, a) in enumerate(self.sections):
            y, self.zi[i] = scipy_signal.lfilter(b, a, y, zi=self.zi[i])
        return y


def compute_band_energies(mag):
    energies = np.zeros(N_BANDS, dtype=np.float64)
    for i, (start, end) in enumerate(BANDS):
        energies[i] = np.mean(mag[start:end])
    return energies


class PcAudioProcessor:
    """Replica el pipeline de core1_fft() (pico2_mic_dsp.c) sobre audio de la PC:
    HPF -> LPF -> ventana deslizante de 512 -> Hanning -> FFT, por bloques de HOP_SIZE
    muestras, acumulando el espectro y las bandas promedio."""

    def __init__(self):
        self.hpf = CascadeFilter(HPF_FILTERS)
        self.lpf = CascadeFilter(LPF_FILTERS)
        self.sliding_window = np.zeros(FFT_SIZE, dtype=np.float64)
        self.hann = np.hanning(FFT_SIZE)
        self.mag_sum = np.zeros(FFT_SIZE // 2, dtype=np.float64)
        self.band_sum = np.zeros(N_BANDS, dtype=np.float64)
        self.frame_count = 0

    def process_block(self, block):
        filtered = self.hpf.process(block)
        filtered = self.lpf.process(filtered)

        # Mismo desplazamiento que sliding_window en pico2_mic_dsp.c: la segunda mitad
        # pasa a ser la primera, el bloque nuevo entra en la segunda mitad.
        self.sliding_window[:HOP_SIZE] = self.sliding_window[HOP_SIZE:]
        self.sliding_window[HOP_SIZE:] = filtered

        frame = self.sliding_window * self.hann
        # rfft sin normalizar, mismo convenio que arm_rfft_fast_f32, para que las
        # magnitudes sean comparables en escala con las que manda la Pico.
        spectrum = np.fft.rfft(frame, n=FFT_SIZE)
        mag = np.abs(spectrum[:FFT_SIZE // 2])

        self.mag_sum += mag
        self.band_sum += compute_band_energies(mag)
        self.frame_count += 1

    def averages(self):
        if self.frame_count == 0:
            return np.zeros(FFT_SIZE // 2), np.zeros(N_BANDS)
        return self.mag_sum / self.frame_count, self.band_sum / self.frame_count


# Cola sin límite: acá priorizamos no perder ningún bloque de audio de la PC durante la
# ventana de grabación fija, a diferencia de otros scripts del repo que sí descartan
# bloques bajo presión porque están pensados para reproducción en vivo.
audio_queue = queue.Queue()


def audio_callback(indata, frames, time_info, status):
    if status:
        print(f"Audio status: {status}")
    audio_queue.put(indata[:, 0].astype(np.float64).copy())


def drain_audio_queue(pc_processor):
    try:
        while True:
            block = audio_queue.get_nowait()
            pc_processor.process_block(block)
    except queue.Empty:
        pass


def normalize(x):
    peak = np.max(x)
    return x / peak if peak > 1e-12 else x


def save_csv(freqs, pc_mag, pico_mag, pc_bands, pico_bands_fft, pico_bands_onboard, band_labels, csv_path):
    """Guarda los datos calculados en formato largo (una fila por bin de FFT y una fila
    por banda) para que `plot_comparativa_csv.py` los pueda graficar después."""
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["kind", "freq_hz", "band_label", "pc", "pico", "pico_fft", "pico_onboard"])

        for freq, pc, pico in zip(freqs, pc_mag, pico_mag):
            writer.writerow(["fft", freq, "", pc, pico, "", ""])

        for label, pc, pico_fft, pico_onboard in zip(band_labels, pc_bands, pico_bands_fft, pico_bands_onboard):
            writer.writerow(["band", "", label, pc, "", pico_fft, pico_onboard])

    print(f"\nCSV guardado en: {csv_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Compara FFT y banco de filtros entre el micrófono de la PC y la Pico"
    )
    parser.add_argument("port", type=str, help="Puerto serial de la Pico (ej. COM8)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate (ignorado por USB CDC)")
    parser.add_argument("--duration", type=float, default=6.0, help="Segundos a grabar (default 6.0)")
    parser.add_argument("--save-csv", type=str, default=None, help="Guardar los datos calculados como CSV (para graficarlos después con plot_comparativa_csv.py)")
    parser.add_argument("--device", type=int, default=None, help="Índice del dispositivo de entrada de audio (sounddevice); por defecto usa el micrófono default del sistema")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baudrate, timeout=1)
        print(f"Conectado a {args.port}...")
    except Exception as e:
        print(f"Error abriendo puerto serial: {e}")
        return

    pc_processor = PcAudioProcessor()

    pico_mag_sum = np.zeros(FFT_SIZE // 2, dtype=np.float64)
    pico_band_sum_from_fft = np.zeros(N_BANDS, dtype=np.float64)   # "PC/FFT Pico"
    pico_band_sum_onboard = np.zeros(N_BANDS, dtype=np.float64)    # "Pico"
    pico_frame_count = 0

    buffer = bytearray()
    packet_size = 2072  # 2056 (header + FFT) + 16 (8 x uint16 band_energies del firmware)
    sync_header = b'\xAA\x55'

    try:
        stream = sd.InputStream(
            samplerate=SAMPLE_RATE,
            channels=1,
            blocksize=HOP_SIZE,
            device=args.device,
            callback=audio_callback,
        )
    except Exception as e:
        print(f"Error abriendo el micrófono de la PC: {e}")
        ser.close()
        return

    print(f"Grabando {args.duration:.1f}s desde el micrófono de la PC y la Pico simultáneamente...")
    print("(Sin sincronización de fase entre ambos micrófonos: se comparan espectros promedio, no cuadro a cuadro.)")

    stream.start()
    t0 = time.time()

    try:
        while time.time() - t0 < args.duration:
            drain_audio_queue(pc_processor)

            if ser.in_waiting > 0:
                buffer.extend(ser.read(ser.in_waiting or 1))

            while True:
                idx = buffer.find(sync_header)
                if idx == -1:
                    if len(buffer) > 0 and buffer[-1] == 0xAA:
                        buffer = buffer[-1:]
                    else:
                        buffer.clear()
                    break

                if len(buffer) >= idx + packet_size:
                    packet = buffer[idx: idx + packet_size]
                    buffer = buffer[idx + packet_size:]

                    floats_data = packet[8:2056]
                    all_floats = np.frombuffer(floats_data, dtype=np.float32)
                    firmware_bands_raw = np.frombuffer(packet[2056:2072], dtype=np.uint16)

                    if len(all_floats) == 512 and len(firmware_bands_raw) == 8:
                        real_part = all_floats[:256].astype(np.float64)
                        imag_part = all_floats[256:].astype(np.float64)
                        mag = np.sqrt(real_part ** 2 + imag_part ** 2)

                        pico_mag_sum += mag
                        pico_band_sum_from_fft += compute_band_energies(mag)
                        pico_band_sum_onboard += firmware_bands_raw.astype(np.float64) / FIRMWARE_ENERGY_TO_AMPLITUDE_GAIN
                        pico_frame_count += 1
                else:
                    if idx > 0:
                        buffer = buffer[idx:]
                    break
    except KeyboardInterrupt:
        print("\nInterrumpido por el usuario.")
    finally:
        stream.stop()
        stream.close()
        ser.close()

    # Drenar lo que haya quedado en la cola de audio tras parar el stream
    drain_audio_queue(pc_processor)

    print(f"\nFrames Pico recibidos: {pico_frame_count}  |  Frames de audio PC procesados: {pc_processor.frame_count}")

    if pico_frame_count == 0:
        print("No se recibió ningún paquete completo de la Pico. Verificá la conexión y volvé a intentar.")
        return
    if pc_processor.frame_count == 0:
        print("No se recibió audio del micrófono de la PC. Probá con --device para elegir otro dispositivo.")
        return

    freqs = np.arange(FFT_SIZE // 2) * (SAMPLE_RATE / FFT_SIZE)

    pico_mag_avg = pico_mag_sum / pico_frame_count
    pico_bands_from_fft_avg = pico_band_sum_from_fft / pico_frame_count
    pico_bands_onboard_avg = pico_band_sum_onboard / pico_frame_count
    pc_mag_avg, pc_bands_avg = pc_processor.averages()

    print("\n[Banco de filtros] energía promedio sin normalizar (comparar forma, no nivel absoluto)")
    print(f"{'banda':>6} {'PC':>10} {'PC/FFT Pico':>12} {'Pico':>10}")
    for i in range(N_BANDS):
        print(f"{i:>6} {pc_bands_avg[i]:>10.5f} {pico_bands_from_fft_avg[i]:>12.5f} {pico_bands_onboard_avg[i]:>10.5f}")

    if args.save_csv:
        save_csv(freqs, pc_mag_avg, pico_mag_avg, pc_bands_avg, pico_bands_from_fft_avg, pico_bands_onboard_avg, BAND_LABELS, args.save_csv)
    else:
        print("\nPasá --save-csv PATH para guardar los datos y graficarlos después con plot_comparativa_csv.py")


if __name__ == "__main__":
    main()
