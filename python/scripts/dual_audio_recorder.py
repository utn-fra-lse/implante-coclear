import sys
import argparse
import serial
import numpy as np
import scipy.signal as scipy_signal
from scipy.io import wavfile

def main():
    parser = argparse.ArgumentParser(description="Grabador Dual: FFT Original vs Simulador Coclear (Vocoder)")
    parser.add_argument("port", type=str, help="Puerto serial (ej. COM3 o /dev/ttyUSB0)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate (ignorado por USB CDC)")
    parser.add_argument("--out-fft", type=str, default="grabacion_fft.wav", help="Archivo de salida FFT")
    parser.add_argument("--out-vocoder", type=str, default="grabacion_vocoder.wav", help="Archivo de salida Vocoder")
    parser.add_argument("--gain", type=float, default=1.0, help="Multiplicador de ganancia de audio (ej. 2.0, 10.0)")
    parser.add_argument("--cutoff", type=float, default=7000.0, help="Frecuencia de corte del filtro pasa bajos (Hz)")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baudrate, timeout=1)
        print(f"Conectado a {args.port}...")
    except Exception as e:
        print(f"Error abriendo puerto serial: {e}")
        return

    # Buffers para Overlap-Add
    ola_buffer_fft = np.zeros(256, dtype=np.float32)
    ola_buffer_vocoder = np.zeros(256, dtype=np.float32)

    # Inicialización del filtro pasa bajos (Butterworth) a 7000 Hz
    nyq = 16000 / 2.0
    if args.cutoff > 0 and args.cutoff < nyq:
        b, a = scipy_signal.butter(4, args.cutoff / nyq, btype='low')
        zi_fft = scipy_signal.lfilter_zi(b, a)
        zi_vocoder = scipy_signal.lfilter_zi(b, a)
    else:
        b, a, zi_fft, zi_vocoder = None, None, None, None

    buffer = bytearray()
    packet_size = 2056
    sync_header = b'\xAA\x55'
    
    # Arrays en memoria para guardar el audio final
    recorded_fft = []
    recorded_vocoder = []

    # Límites de bandas del vocoder
    bands = [
        (6, 10),    # Banda 1: ~187 - 312 Hz
        (10, 16),   # Banda 2: ~312 - 500 Hz
        (16, 25),   # Banda 3: ~500 - 781 Hz
        (25, 40),   # Banda 4: ~781 - 1250 Hz
        (40, 64),   # Banda 5: ~1250 - 2000 Hz
        (64, 102),  # Banda 6: ~2000 - 3187 Hz
        (102, 162), # Banda 7: ~3187 - 5062 Hz
        (162, 256)  # Banda 8: ~5062 - 8000 Hz
    ]

    print("--------------------------------------------------")
    print("🎧 Grabando ambos audios en tiempo real (sin reproducción en vivo)...")
    print(f"💾 FFT -> {args.out_fft}")
    print(f"💾 Vocoder -> {args.out_vocoder}")
    print("🛑 Presiona Ctrl+C para detener y guardar los archivos")
    print("--------------------------------------------------")

    try:
        while True:
            if ser.in_waiting > 0:
                buffer.extend(ser.read(ser.in_waiting or 1))
                
            idx = buffer.find(sync_header)
            if idx == -1:
                # Retener último byte si es la mitad del sync
                if len(buffer) > 0 and buffer[-1] == 0xAA:
                    buffer = buffer[-1:]
                else:
                    buffer.clear()
                continue
            
            if len(buffer) >= idx + packet_size:
                packet = buffer[idx : idx + packet_size]
                buffer = buffer[idx + packet_size:]
                
                floats_data = packet[8:]
                all_floats = np.frombuffer(floats_data, dtype=np.float32)
                
                if len(all_floats) == 512:
                    real_part = all_floats[:256]
                    imag_part = all_floats[256:]
                    
                    # ----------------------------------------------------
                    # 1. RECONSTRUCCIÓN FFT ORIGINAL (Señal Cruda)
                    # ----------------------------------------------------
                    complex_spec = real_part + 1j * imag_part
                    signal_fft = np.fft.irfft(complex_spec, n=512)
                    
                    # OLA FFT
                    audio_out_fft = signal_fft[:256] + ola_buffer_fft
                    ola_buffer_fft = signal_fft[256:]
                    
                    if b is not None:
                        audio_out_fft, zi_fft = scipy_signal.lfilter(b, a, audio_out_fft, zi=zi_fft)
                    
                    scaled_fft = np.clip(audio_out_fft * args.gain, -1.0, 1.0)
                    recorded_fft.append(scaled_fft)

                    # ----------------------------------------------------
                    # 2. RECONSTRUCCIÓN VOCODER (8 Bandas Cocleares)
                    # ----------------------------------------------------
                    mag = np.sqrt(real_part**2 + imag_part**2)
                    band_energies = np.zeros(8, dtype=np.float32)
                    
                    for i, (start, end) in enumerate(bands):
                        band_energies[i] = np.mean(mag[start:end])
                        
                    vocoder_complex = np.zeros(256, dtype=np.complex64)
                    for i, (start, end) in enumerate(bands):
                        # Fases aleatorias entre -pi y pi para emular el ruido blanco de esa banda
                        phases = np.random.uniform(-np.pi, np.pi, end - start)
                        vocoder_complex[start:end] = band_energies[i] * np.exp(1j * phases)
                        
                    vocoder_signal = np.fft.irfft(vocoder_complex, n=512)
                    
                    # OLA Vocoder
                    audio_out_vocoder = vocoder_signal[:256] + ola_buffer_vocoder
                    ola_buffer_vocoder = vocoder_signal[256:]
                    
                    if b is not None:
                        audio_out_vocoder, zi_vocoder = scipy_signal.lfilter(b, a, audio_out_vocoder, zi=zi_vocoder)
                    
                    scaled_vocoder = np.clip(audio_out_vocoder * args.gain, -1.0, 1.0)
                    recorded_vocoder.append(scaled_vocoder)
                    
            else:
                if idx > 0:
                    buffer = buffer[idx:]
    except KeyboardInterrupt:
        print("\n\nDeteniendo grabación...")
    finally:
        ser.close()
        
        if recorded_fft:
            full_audio_fft = np.concatenate(recorded_fft)
            int16_fft = np.int16(full_audio_fft * 32767)
            wavfile.write(args.out_fft, 16000, int16_fft)
            print(f"✅ ¡Audio Original (FFT) guardado en: {args.out_fft}! ({len(int16_fft) / 16000.0:.2f}s)")
        
        if recorded_vocoder:
            full_audio_vocoder = np.concatenate(recorded_vocoder)
            int16_vocoder = np.int16(full_audio_vocoder * 32767)
            wavfile.write(args.out_vocoder, 16000, int16_vocoder)
            print(f"✅ ¡Audio Coclear (Vocoder) guardado en: {args.out_vocoder}! ({len(int16_vocoder) / 16000.0:.2f}s)")

if __name__ == '__main__':
    main()
