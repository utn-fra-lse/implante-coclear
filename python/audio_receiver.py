import serial
import wave

import serial.tools
import serial.tools.list_ports

SERIAL_PORT = "COM8"  # Seleccionar puerto serial
BAUDRATE = 115200
FFT_SIZE = 2048
SAMPLE_FREQ = 40000  # Frecuencia de muestreo
CHUNK_COUNT = 100  # Número de bloques a grabar

OUTPUT_WAV = "assets/grabacion.wav"

def find_header(ser):
    while True:
        if ser.read(1) == b'\xAA' and ser.read(1) == b'\x55':
            return

def main():
    available_ports = serial.tools.list_ports.comports()
    port_names = [port.device for port in available_ports]
    print(f"Puertos disponibles: {port_names}")
    if SERIAL_PORT not in port_names:
        print(f"Error: El puerto {SERIAL_PORT} no está disponible.")
        return
    with serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1) as ser:
        print("Esperando datos...")
        audio_data = bytearray()

        for _ in range(CHUNK_COUNT):
            find_header(ser)
            chunk = ser.read(FFT_SIZE)
            if len(chunk) == FFT_SIZE:
                audio_data.extend(chunk)
                print(f"Chunk recibido ({len(audio_data)} bytes totales)")
            else:
                print("Chunk incompleto")

    with wave.open(OUTPUT_WAV, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(1)  # 8 bits
        wav_file.setframerate(40000)  # o el sample rate que uses
        wav_file.writeframes(audio_data)

    print(f"Grabación guardada como {OUTPUT_WAV}")

if __name__ == "__main__":
    main()
