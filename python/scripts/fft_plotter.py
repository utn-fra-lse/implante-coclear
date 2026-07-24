import sys
import argparse
import serial
import queue
import numpy as np
import pyqtgraph as pg
import sounddevice as sd
from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget, QGridLayout
from PyQt6.QtCore import QThread, pyqtSignal, Qt

# Queue for audio playback
audio_queue = queue.Queue(maxsize=10)
GLOBAL_GAIN = 1.0

def audio_callback(outdata, frames, time, status):
    if status:
        print(f"Audio status: {status}")
    try:
        # Ahora esperamos frames=256 gracias al Overlap-Add
        data = audio_queue.get_nowait()
        
        # Audio expects float32 between -1.0 and 1.0. 
        # Dado que la IFFT puede tirar valores altísimos si la FFT del micro no está normalizada,
        # hacemos un escalado básico (divide por 512). 
        # Si escuchás ruido saturado, disminuí la ganancia.
        scaled = np.clip((data * GLOBAL_GAIN) / 512.0, -1.0, 1.0)
        outdata[:] = scaled.reshape(-1, 1)
    except queue.Empty:
        outdata[:] = np.zeros((frames, 1), dtype=np.float32)

class SerialReaderThread(QThread):
    # Signals: real, imag, magnitude, signal
    data_ready = pyqtSignal(np.ndarray, np.ndarray, np.ndarray, np.ndarray)
    
    def __init__(self, port, baudrate, play_audio):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.play_audio = play_audio
        self.running = True
        self.ser = None
        self.ola_buffer = np.zeros(256, dtype=np.float32)

    def run(self):
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"Connected to {self.port} at {self.baudrate} baud.")
        except Exception as e:
            print(f"Error opening serial port: {e}")
            return

        buffer = bytearray()
        packet_size = 2056
        sync_header = b'\xAA\x55'
        frame_count = 0
        stream = None
        
        if self.play_audio:
            # Iniciamos stream de audio
            # blocksize=256 porque con Overlap-Add generamos 256 muestras netas por frame
            try:
                stream = sd.OutputStream(
                    samplerate=16000, 
                    channels=1, 
                    blocksize=256, 
                    callback=audio_callback
                )
                stream.start()
                print("Audio playback started.")
            except Exception as e:
                print(f"Failed to start audio stream: {e}")
                self.play_audio = False

        while self.running:
            try:
                if self.ser.in_waiting > 0:
                    buffer.extend(self.ser.read(self.ser.in_waiting or 1))
            except serial.SerialException:
                print("Serial connection lost.")
                break
                
            # Process buffer
            while self.running:
                idx = buffer.find(sync_header)
                if idx == -1:
                    # Sync not found. Keep the last byte in case it's 0xAA (half sync)
                    if len(buffer) > 0 and buffer[-1] == 0xAA:
                        buffer = buffer[-1:]
                    else:
                        buffer.clear()
                    break
                
                # Sync found at idx
                if len(buffer) >= idx + packet_size:
                    packet = buffer[idx : idx + packet_size]
                    # Consume data from buffer
                    buffer = buffer[idx + packet_size:]
                    
                    # Parse packet
                    # Ignorar sync (2), num_bins (2), sample_rate (4) = 8 bytes
                    floats_data = packet[8:]
                    all_floats = np.frombuffer(floats_data, dtype=np.float32)
                    
                    if len(all_floats) == 512:
                        real_part = all_floats[:256]
                        imag_part = all_floats[256:]
                        
                        complex_spec = real_part + 1j * imag_part
                        signal = np.fft.irfft(complex_spec, n=512)
                        
                        if self.play_audio:
                            # --- OVERLAP-ADD (OLA) ---
                            # Sumamos la primera mitad de la IFFT con la cola del frame anterior
                            audio_out = signal[:256] + self.ola_buffer
                            # Guardamos la segunda mitad para el próximo frame
                            self.ola_buffer = signal[256:]
                            
                            try:
                                audio_queue.put_nowait(audio_out.astype(np.float32))
                            except queue.Full:
                                pass # buffer lleno, descartamos este frame de audio
                                
                        frame_count += 1
                        
                        # Decimación UI: Graficar solo 1 de cada 10 paquetes para no tildar la app
                        # Esto asegura que la matemática y el audio sigan a 100% velocidad real,
                        # pero la UI solo dibuja a ~3 FPS (mucho más liviano).
                        if frame_count % 1 == 0:
                            # Computar magnitud acá para ahorrar cálculos de CPU en los frames descartados
                            magnitude = np.sqrt(real_part**2 + imag_part**2)
                            self.data_ready.emit(real_part, imag_part, magnitude, signal)
                else:
                    # Not enough data for a full packet yet
                    # Discard garbage before the sync header
                    if idx > 0:
                        buffer = buffer[idx:]
                    break

        if stream:
            stream.stop()
            stream.close()

        if self.ser and self.ser.is_open:
            self.ser.close()
            print("Serial port closed.")

    def stop(self):
        self.running = False
        self.wait()


class MainWindow(QMainWindow):
    def __init__(self, port, baudrate, play_audio):
        super().__init__()
        self.setWindowTitle(f"FFT Serial Plotter - {port}")
        self.resize(1000, 800)

        # Configuración pyqtgraph (Dark Theme)
        pg.setConfigOptions(antialias=False, background='#0d1117', foreground='#c9d1d9')

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QGridLayout(central_widget)

        # Inicializar 4 plots
        self.plot_real = pg.PlotWidget(title="Parte Real")
        self.plot_real.setLabel('bottom', 'Frecuencia (Hz)')
        self.plot_imag = pg.PlotWidget(title="Parte Imaginaria")
        self.plot_imag.setLabel('bottom', 'Frecuencia (Hz)')
        self.plot_mag = pg.PlotWidget(title="Magnitud")
        self.plot_mag.setLabel('bottom', 'Frecuencia (Hz)')
        self.plot_sig = pg.PlotWidget(title="Señal Temporal (IFFT)")
        self.plot_sig.setLabel('bottom', 'Muestras')

        # Configurar grillas
        self.plot_real.showGrid(x=True, y=True, alpha=0.3)
        self.plot_imag.showGrid(x=True, y=True, alpha=0.3)
        self.plot_mag.showGrid(x=True, y=True, alpha=0.3)
        self.plot_sig.showGrid(x=True, y=True, alpha=0.3)

        # Fijar rangos del eje X e Y
        self.plot_real.setXRange(0, 8000, padding=0)
        self.plot_imag.setXRange(0, 8000, padding=0)
        self.plot_mag.setXRange(0, 8000, padding=0)
        self.plot_sig.setXRange(0, 511, padding=0)

        # Frecuencias para el eje X (16000 Hz sample rate, 512 N)
        self.freqs = np.arange(256) * (16000 / 512)

        self.plot_real.setYRange(-10, 10, padding=0)
        self.plot_imag.setYRange(-10, 10, padding=0)
        self.plot_mag.setYRange(0, 10, padding=0)
        self.plot_sig.setYRange(-1, 1, padding=0)

        # Deshabilitar auto-rango para asegurar que queden fijos
        for plot in [self.plot_real, self.plot_imag, self.plot_mag, self.plot_sig]:
            plot.disableAutoRange()

        layout.addWidget(self.plot_real, 0, 0)
        layout.addWidget(self.plot_imag, 0, 1)
        layout.addWidget(self.plot_mag, 1, 0)
        layout.addWidget(self.plot_sig, 1, 1)

        # Crear curvas
        self.curve_real = self.plot_real.plot(pen=pg.mkPen(color='#00d9ff', width=1.5))
        self.curve_imag = self.plot_imag.plot(pen=pg.mkPen(color='#ff00ff', width=1.5))
        self.curve_mag = self.plot_mag.plot(pen=pg.mkPen(color='#00ff88', width=1.5))
        self.curve_sig = self.plot_sig.plot(pen=pg.mkPen(color='#f0e68c', width=1.5))

        # Iniciar hilo de lectura serial
        self.thread = SerialReaderThread(port, baudrate, play_audio)
        self.thread.data_ready.connect(self.update_plots)
        self.thread.start()

    def update_plots(self, real, imag, mag, sig):
        self.curve_real.setData(self.freqs, real)
        self.curve_imag.setData(self.freqs, imag)
        self.curve_mag.setData(self.freqs, mag)
        self.curve_sig.setData(sig)

    def closeEvent(self, event):
        print("Closing application...")
        if self.thread:
            self.thread.stop()
        event.accept()

def main():
    parser = argparse.ArgumentParser(description="FFT Serial Plotter")
    parser.add_argument("port", type=str, help="Serial port (e.g., COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--play-audio", action="store_true", help="Escuchar el audio reconstruido por IFFT")
    parser.add_argument("--gain", type=float, default=1.0, help="Multiplicador de ganancia de audio (ej. 2.0, 10.0)")
    args = parser.parse_args()

    global GLOBAL_GAIN
    GLOBAL_GAIN = args.gain

    app = QApplication(sys.argv)
    window = MainWindow(args.port, args.baudrate, args.play_audio)
    window.show()
    sys.exit(app.exec())

if __name__ == '__main__':
    main()
