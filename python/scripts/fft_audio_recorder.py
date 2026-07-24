import sys
import argparse
import serial
import queue
import numpy as np
import sounddevice as sd
from scipy.io import wavfile

# Cola para reproducir el audio en tiempo real
audio_queue = queue.Queue(maxsize=50)

def audio_callback(outdata, frames, time, status):
    if status:
        pass
    try:
        # Obtenemos 512 samples de la IFFT
        data = audio_queue.get_nowait()
        
        # Escalado rudimentario
        scaled = np.clip(data / 512.0, -1.0, 1.0)
        outdata[:] = scaled.reshape(-1, 1)
    except queue.Empty:
        # Si la cola está vacía, reproducimos silencio
        outdata[:] = np.zeros((frames, 1), dtype=np.float32)

def main():
    parser = argparse.ArgumentParser(description="FFT Audio Recorder & Player")
    parser.add_argument("port", type=str, help="Puerto serial (ej. COM3)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate (ignorado por USB CDC)")
    parser.add_argument("--output", type=str, default="grabacion.wav", help="Archivo de salida (ej. audio.wav)")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baudrate, timeout=1)
        print(f"Conectado a {args.port}...")
    except Exception as e:
        print(f"Error abriendo puerto serial: {e}")
        return

    # Iniciar el stream de parlantes
    stream = sd.OutputStream(
        samplerate=16000, 
        channels=1, 
        blocksize=512, 
        callback=audio_callback
    )
    stream.start()
    
    buffer = bytearray()
    packet_size = 2056
    sync_header = b'\xAA\x55'
    
    # Array en memoria para ir guardando TODO el audio
    recorded_audio = []

    print("--------------------------------------------------")
    print("🎧 Escuchando y grabando en tiempo real...")
    print(f"💾 Se guardará al finalizar en: {args.output}")
    print("🛑 Presiona Ctrl+C para detener y guardar el archivo")
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
                    
                    complex_spec = real_part + 1j * imag_part
                    # iRFFT a 512 puntos
                    signal = np.fft.irfft(complex_spec, n=512)
                    
                    # 1. Guardar para el .WAV (escalado)
                    scaled_for_wav = np.clip(signal / 512.0, -1.0, 1.0)
                    recorded_audio.append(scaled_for_wav)
                    
                    # 2. Mandar a los parlantes en tiempo real
                    try:
                        audio_queue.put_nowait(signal)
                    except queue.Full:
                        pass # Descartamos frame del parlante si va lento, pero NO de la grabación
            else:
                if idx > 0:
                    buffer = buffer[idx:]
    except KeyboardInterrupt:
        print("\n\nDeteniendo grabación...")
    finally:
        stream.stop()
        stream.close()
        ser.close()
        
        if recorded_audio:
            # Concatenar todos los bloques de 512 muestras
            full_audio = np.concatenate(recorded_audio)
            
            # Convertir de float32 (-1.0 a 1.0) a int16 para formato WAV estándar
            int16_audio = np.int16(full_audio * 32767)
            
            # Guardar el .wav usando scipy
            wavfile.write(args.output, 16000, int16_audio)
            print(f"✅ ¡Audio guardado exitosamente en: {args.output}!")
            print(f"📊 Duración total: {len(int16_audio) / 16000.0:.2f} segundos.")
        else:
            print("❌ No se grabaron datos.")

if __name__ == '__main__':
    main()
