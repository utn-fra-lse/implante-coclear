import serial
import serial.tools.list_ports

import matplotlib.pyplot as plt
import matplotlib.animation as animation
import sys

from time import sleep
import logging
# Configure logger
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler()
    ]
)

logger = logging.getLogger(__name__)

# === Configuration ===
PORT = 'COM8'
BAUDRATE = 115200
available_ports = serial.tools.list_ports.comports()
port_names = [port.device for port in available_ports]
print(f"Puertos disponibles: {port_names}")
if PORT not in port_names:
    print(f"Error: El puerto {PORT} no está disponible.")
    sys.exit(1)
# === Set up serial ===
ser = serial.Serial(PORT, BAUDRATE, timeout=1)
fig, ax = plt.subplots()

# === Plot setup ===
def setup_plot(ax: plt.Axes, freq: list, mag: list):
    ax.clear()
    ax.set_xlim(0, max(freq) * 1.01)
    ax.set_ylim(0, max(mag) * 1.01)
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Magnitude")
    ax.set_title("Real-Time FFT Magnitudes from Pico")
    logger.info(f"Plotting: {len(freq)} frequency points.")
    bar_plot = ax.bar(freq, mag, width=freq[1] - freq[0], align='center')
    return bar_plot

# === Live update function ===
def get_fft_data():
    line = ser.readline().decode(errors="ignore").strip()
    
    if not line.startswith("[") or not line.endswith("]"):
        return [], []

    # Remove brackets and split by comma
    stripped = line[1:-1]
    try:
        freqs = []
        magnitudes = []
        for pair in stripped.split(","):
            freq, mag = pair.split(":")
            freqs.append(float(freq.strip()))
            magnitudes.append(float(mag.strip()))

        return freqs, magnitudes
    
    except ValueError as e:
        logger.error(f"No se obtuvieron los valores seriales: {e}")
        return [], []

def update_plot(frame):
    ser.reset_input_buffer() 
    global ax
    frequencies, magnitudes = get_fft_data()
    while len(frequencies) == 0 or len(frequencies) != len(magnitudes):
        logger.info('Waiting for valid data...')
        frequencies, magnitudes = get_fft_data()
        sleep(0.01)

    return setup_plot(ax, frequencies, magnitudes)

ani = animation.FuncAnimation(fig, update_plot, interval=25)
plt.show()
