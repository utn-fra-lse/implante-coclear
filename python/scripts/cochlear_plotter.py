import sys
import argparse
import serial
import queue
import numpy as np
import pyqtgraph as pg
import sounddevice as sd
from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QCheckBox, QLabel
from PyQt6.QtCore import QThread, pyqtSignal

# Cola para reproducir el vocoder en tiempo real
audio_queue = queue.Queue(maxsize=10)
GLOBAL_GAIN = 1.0

def audio_callback(outdata, frames, time, status):
    if status:
        print(f"Audio status: {status}")
    try:
        data = audio_queue.get_nowait()
        # Escalar con la ganancia multiplicadora
        scaled = np.clip(data * GLOBAL_GAIN, -1.0, 1.0)
        outdata[:] = scaled.reshape(-1, 1)
    except queue.Empty:
        outdata[:] = np.zeros((frames, 1), dtype=np.float32)

class CochlearSimulatorThread(QThread):
    # Emite un array de 8 floats (las energías de cada banda)
    data_ready = pyqtSignal(np.ndarray)
    status_message = pyqtSignal(str)
    
    def __init__(self, port, baudrate, play_audio, auto_threshold_frames):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.play_audio = play_audio
        self.auto_threshold_frames = auto_threshold_frames
        self.running = True
        self.ser = None
        self.ola_buffer = np.zeros(256, dtype=np.float32)

        # Estado del Noise Gate
        self.noise_gate_enabled = (auto_threshold_frames > 0)
        self.threshold_value = 0.0
        self.calibration_frames_count = 0
        self.sum_rms = 0.0

    def apply_noise_gate(self, mag):
        if not self.noise_gate_enabled:
            return mag

        # Por el teorema de Parseval, el RMS calculado desde los bins crudos de la FFT
        # viene escalado. Para que el valor sea equivalente al RMS temporal (y los dBFS 
        # coincidan con la realidad), tenemos que dividir por sqrt(N), donde N=512.
        rms = np.sqrt(np.mean(mag**2)) / np.sqrt(512)
        
        frames_to_calibrate = self.auto_threshold_frames if self.auto_threshold_frames > 0 else 62
        
        if self.calibration_frames_count < frames_to_calibrate:
            self.sum_rms += rms
            self.calibration_frames_count += 1
            if self.calibration_frames_count == frames_to_calibrate:
                self.threshold_value = (self.sum_rms / frames_to_calibrate) * 1.5
                dbfs = 20 * np.log10(max(self.threshold_value, 1e-6) / 1.0)
                if self.threshold_value < 0.01:
                    rms_str = f"{self.threshold_value:.2e}"
                else:
                    rms_str = f"{self.threshold_value:.3f}"
                msg = f"Umbral RMS: {rms_str}  ({dbfs:.1f} dBFS)"
                print(f"\n[Noise Gate] Calibración terminada. {msg}\n")
                self.status_message.emit(msg)
            return np.zeros_like(mag)

        if rms < self.threshold_value:
            return np.zeros_like(mag)
        
        return mag

    def run(self):
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"Conectado a {self.port} para simulador coclear.")
        except Exception as e:
            print(f"Error abriendo puerto serial: {e}")
            return

        buffer = bytearray()
        packet_size = 2056
        sync_header = b'\xAA\x55'
        frame_count = 0
        stream = None
        
        if self.play_audio:
            try:
                stream = sd.OutputStream(
                    samplerate=16000, 
                    channels=1, 
                    blocksize=256, 
                    callback=audio_callback
                )
                stream.start()
                print("Simulador de ruido blanco iniciado.")
            except Exception as e:
                print(f"Error de audio: {e}")
                self.play_audio = False

        # Definir los límites de las 8 bandas para la cóclea (espaciado logarítmico simulado)
        # fs = 16000 Hz, Nyquist = 8000 Hz. 256 bins -> resolución = 31.25 Hz por bin.
        # Rango útil simulado: 200 Hz a 8000 Hz.
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

        while self.running:
            try:
                if self.ser.in_waiting > 0:
                    buffer.extend(self.ser.read(self.ser.in_waiting or 1))
            except serial.SerialException:
                break
                
            while self.running:
                idx = buffer.find(sync_header)
                if idx == -1:
                    if len(buffer) > 0 and buffer[-1] == 0xAA:
                        buffer = buffer[-1:]
                    else:
                        buffer.clear()
                    break
                
                if len(buffer) >= idx + packet_size:
                    packet = buffer[idx : idx + packet_size]
                    buffer = buffer[idx + packet_size:]
                    
                    floats_data = packet[8:]
                    all_floats = np.frombuffer(floats_data, dtype=np.float32)
                    
                    if len(all_floats) == 512:
                        real_part = all_floats[:256]
                        imag_part = all_floats[256:]
                        mag = np.sqrt(real_part**2 + imag_part**2)
                        
                        # Aplicar Noise Gate
                        mag = self.apply_noise_gate(mag)
                        
                        # 1. Calcular energía (promedio de magnitud) por cada una de las 8 bandas
                        band_energies = np.zeros(8, dtype=np.float32)
                        for i, (start, end) in enumerate(bands):
                            # Promediamos la energía en este bloque de frecuencias
                            band_energies[i] = np.mean(mag[start:end])
                            
                        # 2. Simulador Vocoder (Excitación de Ruido Blanco)
                        if self.play_audio:
                            # Creamos un espectro vacío
                            vocoder_complex = np.zeros(256, dtype=np.complex64)
                            
                            for i, (start, end) in enumerate(bands):
                                # Generamos ruido blanco inyectando FASES ALEATORIAS (-pi a pi)
                                phases = np.random.uniform(-np.pi, np.pi, end - start)
                                
                                # Le asignamos a ese rango de ruido la MAGNITUD de nuestra banda
                                vocoder_complex[start:end] = band_energies[i] * np.exp(1j * phases)
                                
                            # Al hacer la IFFT de este espectro modulado, obtenemos en el tiempo 
                            # ruido blanco filtrado exactamente por las 8 bandas de energía.
                            vocoder_signal = np.fft.irfft(vocoder_complex, n=512)
                            
                            audio_out = vocoder_signal[:256] + self.ola_buffer
                            self.ola_buffer = vocoder_signal[256:]
                            
                            try:
                                audio_queue.put_nowait(audio_out.astype(np.float32))
                            except queue.Full:
                                pass
                                
                        frame_count += 1
                        
                        # Decimación UI (~3 FPS)
                        if frame_count % 1 == 0:
                            self.data_ready.emit(band_energies)
                else:
                    if idx > 0:
                        buffer = buffer[idx:]
                    break

        if stream:
            stream.stop()
            stream.close()

        if self.ser and self.ser.is_open:
            self.ser.close()

    def stop(self):
        self.running = False
        self.wait()


class MainWindow(QMainWindow):
    def __init__(self, port, baudrate, play_audio, auto_threshold_frames):
        super().__init__()
        self.setWindowTitle("Simulador de Implante Coclear - 8 Bandas Vocoder")
        self.resize(900, 500)

        pg.setConfigOptions(antialias=True, background='#0d1117', foreground='#c9d1d9')
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)

        # Controles del Noise Gate
        self.checkbox_gate = QCheckBox("Activar Supresor de Ruido (Auto-calibrable)")
        self.checkbox_gate.setChecked(auto_threshold_frames > 0)
        self.checkbox_gate.toggled.connect(self.toggle_noise_gate)
        layout.addWidget(self.checkbox_gate)

        self.label_rms = QLabel("Umbral RMS: Calibrando..." if auto_threshold_frames > 0 else "Umbral RMS: Inactivo")
        self.label_rms.setStyleSheet("color: #00ff88; font-weight: bold; font-size: 14px;")
        layout.addWidget(self.label_rms)

        # Gráfico de Barras para las 8 Bandas
        self.plot_bands = pg.PlotWidget(title="Excitación de Electrodos (8 Bandas Logarítmicas)")
        self.plot_bands.showGrid(x=True, y=True, alpha=0.3)
        self.plot_bands.setYRange(0, 10, padding=0)
        self.plot_bands.setXRange(0.5, 8.5, padding=0)
        self.plot_bands.disableAutoRange()
        
        # Customizar etiquetas del eje X para que muestren las frecuencias
        band_labels = [
            (1, "200-300 Hz"), (2, "300-500 Hz"), (3, "500-780 Hz"), (4, "780-1.2 kHz"), 
            (5, "1.2-2.0 kHz"), (6, "2.0-3.1 kHz"), (7, "3.1-5.0 kHz"), (8, "5.0-8.0 kHz")
        ]
        self.plot_bands.getAxis('bottom').setTicks([band_labels])
        layout.addWidget(self.plot_bands)

        # Crear el BarGraphItem (Barras de neón)
        self.bar_chart = pg.BarGraphItem(
            x=np.arange(1, 9), 
            height=np.zeros(8), 
            width=0.6, 
            brush='#00ff88',  # Verde neón
            pen='#0d1117'
        )
        self.plot_bands.addItem(self.bar_chart)

        # Iniciar Hilo
        self.thread = CochlearSimulatorThread(port, baudrate, play_audio, auto_threshold_frames)
        self.thread.data_ready.connect(self.update_bars)
        self.thread.status_message.connect(self.label_rms.setText)
        self.thread.start()

    def update_bars(self, energies):
        self.bar_chart.setOpts(height=energies)

    def toggle_noise_gate(self, checked):
        if self.thread:
            self.thread.noise_gate_enabled = checked
            if checked:
                print("\n[Noise Gate] Reiniciando calibración...\n")
                self.thread.calibration_frames_count = 0
                self.thread.sum_rms = 0.0
                self.label_rms.setText("Umbral RMS: Calibrando...")
            else:
                self.label_rms.setText("Umbral RMS: Inactivo")

    def closeEvent(self, event):
        print("Cerrando simulador...")
        if self.thread:
            self.thread.stop()
        event.accept()

def main():
    parser = argparse.ArgumentParser(description="Cochlear 8-Band Vocoder Simulator")
    parser.add_argument("port", type=str, help="Serial port (e.g., COM3)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate (ignored on USB CDC)")
    parser.add_argument("--play-audio", action="store_true", help="Escuchar la salida del Vocoder (Ruido Blanco Modulado)")
    parser.add_argument("--gain", type=float, default=1.0, help="Multiplicador de ganancia de audio (ej. 2.0, 10.0)")
    parser.add_argument("--auto-threshold", type=int, default=0, help="N frames para calibrar ruido de fondo (0=desactivado)")
    args = parser.parse_args()

    global GLOBAL_GAIN
    GLOBAL_GAIN = args.gain

    app = QApplication(sys.argv)
    window = MainWindow(args.port, args.baudrate, args.play_audio, args.auto_threshold)
    window.show()
    sys.exit(app.exec())

if __name__ == '__main__':
    main()
