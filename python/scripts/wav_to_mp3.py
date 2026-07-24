import argparse
import subprocess
import os
import sys

def check_ffmpeg():
    """Verifica si FFmpeg está instalado y accesible en el PATH del sistema."""
    try:
        subprocess.run(["ffmpeg", "-version"], capture_output=True, check=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False

def convert_wav_to_mp3(input_file, output_file, bitrate="192k"):
    if not check_ffmpeg():
        print("❌ Error: Necesitás tener 'ffmpeg' instalado en tu sistema (y agregado al PATH).")
        print("Podés descargarlo desde: https://ffmpeg.org/download.html")
        sys.exit(1)

    if not os.path.exists(input_file):
        print(f"❌ Error: El archivo '{input_file}' no existe.")
        sys.exit(1)

    print(f"🔄 Convirtiendo: {input_file} -> {output_file} (Bitrate: {bitrate})")
    
    # Comando para FFmpeg: 
    # -i: archivo entrada | -vn: descartar video (por si acaso) 
    # -b:a: bitrate de audio | -y: sobrescribir destino
    command = [
        "ffmpeg", 
        "-i", input_file, 
        "-vn", 
        "-b:a", bitrate, 
        "-y", 
        output_file
    ]

    try:
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0:
            print(f"✅ ¡Conversión exitosa! Archivo guardado en: {output_file}")
        else:
            print("❌ Error de FFmpeg durante la conversión:")
            print(result.stderr)
    except Exception as e:
        print(f"❌ Error inesperado: {e}")

def main():
    parser = argparse.ArgumentParser(description="Conversor de WAV a MP3 usando FFmpeg")
    parser.add_argument("input", type=str, help="Archivo WAV de entrada")
    parser.add_argument("-o", "--output", type=str, help="Archivo MP3 de salida (opcional)")
    parser.add_argument("-b", "--bitrate", type=str, default="192k", help="Bitrate del MP3 (ej. 192k, 320k)")
    
    args = parser.parse_args()
    
    input_file = args.input
    output_file = args.output
    
    # Autogenerar el nombre de salida si no lo pasan
    if not output_file:
        base, _ = os.path.splitext(input_file)
        output_file = f"{base}.mp3"
        
    convert_wav_to_mp3(input_file, output_file, args.bitrate)

if __name__ == "__main__":
    main()
