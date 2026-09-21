"""
Mide inteligibilidad objetiva (STOI) del audio reconstruido por la Pico contra una
referencia de banda completa grabada por el micrófono de la PC al mismo tiempo.

A diferencia de pico_pc_comparator.py (que promedia espectros y no le importa la fase),
STOI SÍ es sensible al alineado temporal entre ambas señales, así que acá se hace un
alineado por correlación cruzada antes de calcular el score.

La frecuencia de muestreo de la Pico se LEE del propio paquete USB (`sample_rate`), no se
hardcodea, para poder correr este mismo script sin cambios sin importar qué `MAX_FREQ` esté
flasheado en el firmware (parte del experimento de comparar distintas frecuencias de
muestreo, ver rpipico/pico2_mic_dsp/dsp/include/dsp.h: MAX_FREQ).

Uso:
    python pico_intelligibility_stoi.py COM8 --duration 8 --out-reference ref.wav --out-pico pico.wav
"""
import sys
import argparse
import time
import math
import queue
import struct
import serial
import numpy as np
import scipy.signal as scipy_signal
import sounddevice as sd
from scipy.io import wavfile
from pystoi import stoi

REFERENCE_SAMPLE_RATE = 48000  # Fija: banda completa de la PC, independiente de la Fs de la Pico
FFT_SIZE = 512
HOP_SIZE = 256
PACKET_SIZE = 2072  # 2056 (header + FFT) + 16 (band_energies, no usadas acá) — no depende de la Fs

audio_queue = queue.Queue()  # sin límite: no queremos perder audio de referencia durante la grabación


def audio_callback(indata, frames, time_info, status):
    if status:
        print(f"Audio status: {status}")
    audio_queue.put(indata[:, 0].astype(np.float64).copy())


def drain_audio_queue(chunks):
    try:
        while True:
            chunks.append(audio_queue.get_nowait())
    except queue.Empty:
        pass


def resample_to(signal_in, fs_in, fs_out):
    if fs_in == fs_out:
        return signal_in
    g = math.gcd(int(fs_in), int(fs_out))
    up = int(fs_out) // g
    down = int(fs_in) // g
    return scipy_signal.resample_poly(signal_in, up, down)


def align_signals(reference, test):
    """Alinea dos señales por correlación cruzada y las recorta al tramo solapado."""
    corr = scipy_signal.correlate(test, reference, mode='full')
    lags = scipy_signal.correlation_lags(len(test), len(reference), mode='full')
    best_lag = lags[np.argmax(corr)]

    if best_lag > 0:
        # 'test' está atrasada respecto a 'reference' por best_lag muestras
        test = test[best_lag:]
    elif best_lag < 0:
        reference = reference[-best_lag:]

    n = min(len(reference), len(test))
    return reference[:n], test[:n], best_lag


def main():
    parser = argparse.ArgumentParser(
        description="Mide STOI entre el audio reconstruido de la Pico y una referencia de la PC"
    )
    parser.add_argument("port", type=str, help="Puerto serial de la Pico (ej. COM8)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate (ignorado por USB CDC)")
    parser.add_argument("--duration", type=float, default=8.0, help="Segundos a grabar (default 8.0; hace falta habla real, no ruido)")
    parser.add_argument("--out-reference", type=str, default=None, help="Guardar la referencia de la PC como WAV")
    parser.add_argument("--out-pico", type=str, default=None, help="Guardar el audio reconstruido de la Pico como WAV")
    parser.add_argument("--device", type=int, default=None, help="Índice del dispositivo de entrada de audio de la PC")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baudrate, timeout=1)
        print(f"Conectado a {args.port}...")
    except Exception as e:
        print(f"Error abriendo puerto serial: {e}")
        return

    try:
        stream = sd.InputStream(
            samplerate=REFERENCE_SAMPLE_RATE,
            channels=1,
            blocksize=1024,
            device=args.device,
            callback=audio_callback,
        )
    except Exception as e:
        print(f"Error abriendo el micrófono de la PC: {e}")
        ser.close()
        return

    ola_buffer = np.zeros(FFT_SIZE // 2, dtype=np.float64)
    pico_audio_chunks = []
    pc_audio_chunks = []
    pico_fs = None  # se completa con el primer paquete recibido (sample_rate del header)
    pico_num_bins = None
    pico_frame_count = 0

    buffer = bytearray()
    sync_header = b'\xAA\x55'

    print(f"Grabando {args.duration:.1f}s desde el micrófono de la PC (referencia @ {REFERENCE_SAMPLE_RATE}Hz) y la Pico...")
    print("(Alineado por correlación cruzada antes de calcular STOI — ver docstring del script.)")

    stream.start()
    t0 = time.time()

    try:
        while time.time() - t0 < args.duration:
            drain_audio_queue(pc_audio_chunks)

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

                if len(buffer) >= idx + PACKET_SIZE:
                    packet = buffer[idx: idx + PACKET_SIZE]
                    buffer = buffer[idx + PACKET_SIZE:]

                    num_bins, sample_rate = struct.unpack_from('<HI', packet, 2)
                    floats_data = packet[8:2056]
                    all_floats = np.frombuffer(floats_data, dtype=np.float32)

                    if len(all_floats) == 512:
                        if pico_fs is None:
                            pico_fs = sample_rate
                            pico_num_bins = num_bins
                            print(f"Fs de la Pico detectada desde el header del paquete: {pico_fs} Hz ({num_bins} bins)")

                        real_part = all_floats[:256].astype(np.float64)
                        imag_part = all_floats[256:].astype(np.float64)
                        complex_spec = real_part + 1j * imag_part

                        signal_time = np.fft.irfft(complex_spec, n=FFT_SIZE)
                        audio_out = signal_time[:HOP_SIZE] + ola_buffer
                        ola_buffer = signal_time[HOP_SIZE:]

                        pico_audio_chunks.append(audio_out)
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

    drain_audio_queue(pc_audio_chunks)

    print(f"\nFrames Pico recibidos: {pico_frame_count}  |  Bloques de audio PC capturados: {len(pc_audio_chunks)}")

    if pico_frame_count == 0:
        print("No se recibió ningún paquete completo de la Pico. Verificá la conexión y volvé a intentar.")
        return
    if not pc_audio_chunks:
        print("No se recibió audio del micrófono de la PC. Probá con --device para elegir otro dispositivo.")
        return

    pico_audio = np.concatenate(pico_audio_chunks)
    pc_audio_full = np.concatenate(pc_audio_chunks)

    if args.out_pico:
        wavfile.write(args.out_pico, pico_fs, np.clip(pico_audio, -1.0, 1.0).astype(np.float32))
        print(f"Audio de la Pico guardado en: {args.out_pico}")
    if args.out_reference:
        wavfile.write(args.out_reference, REFERENCE_SAMPLE_RATE, np.clip(pc_audio_full, -1.0, 1.0).astype(np.float32))
        print(f"Referencia de la PC guardada en: {args.out_reference}")

    # Llevar la referencia a la misma Fs que la Pico para poder alinear y comparar
    reference_resampled = resample_to(pc_audio_full, REFERENCE_SAMPLE_RATE, pico_fs)

    reference_aligned, pico_aligned, lag = align_signals(reference_resampled, pico_audio)
    print(f"\nAlineado: lag encontrado = {lag} muestras @ {pico_fs}Hz ({lag / pico_fs * 1000:.1f} ms)")
    print(f"Tramo solapado para STOI: {len(pico_aligned) / pico_fs:.2f} s")

    if len(pico_aligned) < pico_fs * 0.5:
        print("Advertencia: el tramo solapado tras alinear es muy corto (<0.5s); el score de STOI puede no ser confiable.")

    score = stoi(reference_aligned, pico_aligned, pico_fs, extended=False)
    print(f"\n=== STOI @ {pico_fs} Hz: {score:.4f} ===  (0 = nada inteligible, 1 = igual a la referencia)")


if __name__ == "__main__":
    main()
