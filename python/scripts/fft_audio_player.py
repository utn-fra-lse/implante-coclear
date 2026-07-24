import sys
import argparse
import serial
import queue
import numpy as np
import sounddevice as sd

# Buffer ultra corto para forzar la menor latencia posible (0 delay acumulado)
audio_queue = queue.Queue(maxsize=2)
GLOBAL_GAIN = 1.0

def audio_callback(outdata, frames, time, status):
    if status:
        pass
    try:
        data = audio_queue.get_nowait()
        # Escalado rudimentario y protección contra clipping
        scaled = np.clip((data * GLOBAL_GAIN) / 512.0, -1.0, 1.0)
        outdata[:] = scaled.reshape(-1, 1)
    except queue.Empty:
        outdata[:] = np.zeros((frames, 1), dtype=np.float32)

def main():
    parser = argparse.ArgumentParser(description="FFT Audio Player (Ultra-low latency, sin GUI)")
    parser.add_argument("port", type=str, help="Puerto serial (ej. COM3)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate")
    parser.add_argument("--gain", type=float, default=1.0, help="Ganancia de audio")
    args = parser.parse_args()

    global GLOBAL_GAIN
    GLOBAL_GAIN = args.gain

    ola_buffer = np.zeros(256, dtype=np.float32)

    try:
        ser = serial.Serial(args.port, args.baudrate, timeout=1)
        print(f"✅ Conectado a {args.port} a {args.baudrate} baudios.")
    except Exception as e:
        print(f"❌ Error abriendo el puerto serial: {e}")
        return

    # Iniciar stream de audio pidiendo explícitamente latencia baja al SO
    try:
        stream = sd.OutputStream(
            samplerate=16000, 
            channels=1, 
            blocksize=256, 
            latency='low', 
            callback=audio_callback
        )
        stream.start()
        print("🔊 Reproducción de audio iniciada (Baja Latencia). Presioná Ctrl+C para salir.")
    except Exception as e:
        print(f"❌ Error iniciando audio: {e}")
        ser.close()
        return

    buffer = bytearray()
    packet_size = 2056
    sync_header = b'\xAA\x55'

    try:
        while True:
            if ser.in_waiting > 0:
                buffer.extend(ser.read(ser.in_waiting or 1))
                
            idx = buffer.find(sync_header)
            if idx == -1:
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
                    signal = np.fft.irfft(complex_spec, n=512)
                    
                    audio_out = signal[:256] + ola_buffer
                    ola_buffer = signal[256:]
                    
                    # Empujar a la cola, si está llena descartamos el viejo para mantener 0 delay
                    try:
                        audio_queue.put_nowait(audio_out.astype(np.float32))
                    except queue.Full:
                        try:
                            audio_queue.get_nowait()
                            audio_queue.put_nowait(audio_out.astype(np.float32))
                        except queue.Empty:
                            pass
            else:
                if idx > 0:
                    buffer = buffer[idx:]
    except KeyboardInterrupt:
        print("\n⏹️ Deteniendo el reproductor...")
    finally:
        stream.stop()
        stream.close()
        ser.close()

if __name__ == '__main__':
    main()
