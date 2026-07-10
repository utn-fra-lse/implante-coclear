import serial
import struct
import matplotlib.pyplot as plt
import sys
import serial.tools.list_ports
import numpy as np

PORT = 'COM8'
BAUDRATE = 115200

# Verificación de puerto
ports = [p.device for p in serial.tools.list_ports.comports()]
print(f"Puertos disponibles: {ports}")
if PORT not in ports:
    print(f"Error: El puerto {PORT} no está disponible.")
    sys.exit(1)

ser = serial.Serial(PORT, BAUDRATE, timeout=2)

def read_packet():
    # Buscar patrón de sincronización 0xAA 0x55
    while True:
        b1 = ser.read(1)
        if not b1: return None
        if b1 == b'\xAA':
            b2 = ser.read(1)
            if b2 == b'\x55':
                break
                
    header = ser.read(6)
    if len(header) < 6: return None
    num_bins, sample_rate = struct.unpack('<HI', header)
    
    data_size = num_bins * 4
    real_bytes = ser.read(data_size)
    imag_bytes = ser.read(data_size)
    
    if len(real_bytes) < data_size or len(imag_bytes) < data_size: 
        return None
    
    real_part = struct.unpack('<' + 'f'*num_bins, real_bytes)
    imag_part = struct.unpack('<' + 'f'*num_bins, imag_bytes)
    
    return sample_rate, num_bins, real_part, imag_part

def main():
    print("Esperando sincronización y capturando 1 trama completa...")
    ser.reset_input_buffer()
    
    data = read_packet()
    if not data:
        print("Error: No se pudo capturar la trama o hubo un timeout.")
        return
        
    sample_rate, num_bins, real_part, imag_part = data
    print(f"Trama capturada: Fs={sample_rate}Hz, Bins={num_bins}")
    
    # Valores esperados según el MOCK_USB_DATA en el código C
    expected_real = [float(i) for i in range(num_bins)]
    expected_imag = [float(num_bins - i) for i in range(num_bins)]
    
    # Comparación (usamos allclose por si hay algún tema mínimo de precisión de float)
    real_match = np.allclose(real_part, expected_real, atol=1e-3)
    imag_match = np.allclose(imag_part, expected_imag, atol=1e-3)
    
    if real_match and imag_match:
        print("\n[✔] ¡ÉXITO! Los datos recibidos coinciden perfectamente con el mock esperado.")
    else:
        print("\n[X] ¡ERROR! Los datos recibidos NO coinciden con el mock esperado.")
        
    # Graficar para verificar visualmente
    freqs = [i * (sample_rate / (num_bins * 2)) for i in range(num_bins)]
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))
    
    ax1.set_title(f"Parte Real (Validación: {'OK' if real_match else 'FALLO'})")
    ax1.plot(freqs, expected_real, 'k--', label="Valor Esperado (Mock)", linewidth=3)
    ax1.plot(freqs, real_part, 'b-', label="Dato Recibido", alpha=0.7)
    ax1.set_ylabel("Magnitud")
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    ax2.set_title(f"Parte Imaginaria (Validación: {'OK' if imag_match else 'FALLO'})")
    ax2.plot(freqs, expected_imag, 'k--', label="Valor Esperado (Mock)", linewidth=3)
    ax2.plot(freqs, imag_part, 'r-', label="Dato Recibido", alpha=0.7)
    ax2.set_xlabel("Frecuencia (Hz)")
    ax2.set_ylabel("Magnitud")
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    main()
