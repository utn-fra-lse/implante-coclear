import serial
import struct
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import sys
import serial.tools.list_ports

PORT = 'COM8'
BAUDRATE = 115200

# Verificación de puerto
ports = [p.device for p in serial.tools.list_ports.comports()]
print(f"Puertos disponibles: {ports}")
if PORT not in ports:
    print(f"Error: El puerto {PORT} no está disponible.")
    sys.exit(1)

ser = serial.Serial(PORT, BAUDRATE, timeout=1)
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))

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

def update_plot(frame):
    ser.reset_input_buffer()
    data = read_packet()
    if not data: return
    sample_rate, num_bins, real_part, imag_part = data
    
    freqs = [i * (sample_rate / (num_bins * 2)) for i in range(num_bins)]
    
    ax1.clear()
    ax1.set_title(f"Mock Data: Parte Real (Fs={sample_rate}Hz, Bins={num_bins})")
    ax1.bar(freqs, real_part, width=freqs[1]-freqs[0] if len(freqs)>1 else 1, align='center')
    ax1.set_ylabel("Magnitud")
    
    ax2.clear()
    ax2.set_title("Mock Data: Parte Imaginaria")
    ax2.bar(freqs, imag_part, width=freqs[1]-freqs[0] if len(freqs)>1 else 1, align='center', color='orange')
    ax2.set_xlabel("Frecuencia (Hz)")
    ax2.set_ylabel("Magnitud")

ani = animation.FuncAnimation(fig, update_plot, interval=50)
plt.tight_layout()
plt.show()
