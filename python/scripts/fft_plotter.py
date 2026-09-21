import sys
import argparse
import serial
import queue
import numpy as np
import scipy.signal as scipy_signal
import pyqtgraph as pg
import sounddevice as sd
from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget, QGridLayout, QCheckBox, QLabel
from PyQt6.QtCore import QThread, pyqtSignal, Qt

# Queue for audio playback (ampliada para absorber jitter)
audio_queue = queue.Queue(maxsize=50)
GLOBAL_GAIN = 1.0

def audio_callback(outdata, frames, time, status):
    if status:
        print(f"Audio status: {status}")
    try:
        # Ahora esperamos frames=256 gracias al Overlap-Add
        data = audio_queue.get_nowait()
        
        # Audio expects float32 between -1.0 and 1.0. 
        # La señal ya se encuentra en el rango [-1.0, 1.0] gracias a dsp_normalize_buffer.
        # Si escuchás ruido saturado, disminuí la ganancia.
        scaled = np.clip(data * GLOBAL_GAIN, -1.0, 1.0)
        outdata[:] = scaled.reshape(-1, 1)
    except queue.Empty:
        outdata[:] = np.zeros((frames, 1), dtype=np.float32)

class SerialReaderThread(QThread):
    # Signals: real, imag, magnitude, signal
    data_ready = pyqtSignal(np.ndarray, np.ndarray, np.ndarray, np.ndarray)
    status_message = pyqtSignal(str)
    
    def __init__(self, port, baudrate, play_audio, auto_threshold_frames, cutoff_freq):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.play_audio = play_audio
        self.auto_threshold_frames = auto_threshold_frames
        self.running = True
        self.ser = None
        self.ola_buffer = np.zeros(256, dtype=np.float32)
        
        # Inicialización del filtro pasa bajos (Butterworth)
        self.cutoff_freq = cutoff_freq
        nyq = 16000 / 2.0
        if self.cutoff_freq > 0 and self.cutoff_freq < nyq:
            self.b, self.a = scipy_signal.butter(4, self.cutoff_freq / nyq, btype='low')
            self.zi = scipy_signal.lfilter_zi(self.b, self.a)
        else:
            self.b, self.a, self.zi = None, None, None

        # Estado del Noise Gate
        self.noise_gate_enabled = (auto_threshold_frames > 0)
        self.threshold_value = 0.0
        self.calibration_frames_count = 0
        self.sum_rms = 0.0

    def apply_noise_gate(self, signal):
        if not self.noise_gate_enabled:
            return signal

        rms = np.sqrt(np.mean(signal**2))
        
        # Usamos el argumento del usuario o un default de ~1 segundo (62 frames)
        frames_to_calibrate = self.auto_threshold_frames if self.auto_threshold_frames > 0 else 62
        
        # Fase de calibración
        if self.calibration_frames_count < frames_to_calibrate:
            self.sum_rms += rms
            self.calibration_frames_count += 1
            if self.calibration_frames_count == frames_to_calibrate:
                # El umbral es el promedio del ruido + un 50% de margen
                self.threshold_value = (self.sum_rms / frames_to_calibrate) * 1.5
                
                # Calculamos el dBFS usando 1.0 como referencia de Full Scale
                dbfs = 20 * np.log10(max(self.threshold_value, 1e-6) / 1.0)
                
                # Formatear el RMS en notación científica si es muy chico
                if self.threshold_value < 0.01:
                    rms_str = f"{self.threshold_value:.2e}"
                else:
                    rms_str = f"{self.threshold_value:.3f}"
                
                msg = f"Umbral RMS: {rms_str}  ({dbfs:.1f} dBFS)"
                print(f"\n[Noise Gate] Calibración terminada. {msg}\n")
                self.status_message.emit(msg)
            return np.zeros_like(signal) # Silencio mientras calibra

        # Aplicación del umbral
        if rms < self.threshold_value:
            return np.zeros_like(signal)
        
        return signal

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
                        
                        # Aplicar Threshold / Noise Gate
                        signal = self.apply_noise_gate(signal)
                        
                        if self.play_audio:
                            # --- OVERLAP-ADD (OLA) ---
                            # Sumamos la primera mitad de la IFFT con la cola del frame anterior
                            audio_out = signal[:256] + self.ola_buffer
                            # Guardamos la segunda mitad para el próximo frame
                            self.ola_buffer = signal[256:]
                            
                            # Aplicamos el filtro pasa bajos al audio ensamblado
                            if self.b is not None:
                                audio_out, self.zi = scipy_signal.lfilter(self.b, self.a, audio_out, zi=self.zi)
                                
                            try:
                                audio_queue.put_nowait(audio_out.astype(np.float32))
                            except queue.Full:
                                pass # buffer lleno, descartamos este frame de audio
                                
                        frame_count += 1
                        
                        # Decimación UI: Graficar solo 1 de cada 4 paquetes para no tildar la app
                        # Esto asegura que la matemática y el audio sigan a 100% velocidad real,
                        # pero la UI solo dibuja a ~15 FPS (mucho más liviano).
                        if frame_count % 4 == 0:
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
    def __init__(self, port, baudrate, play_audio, auto_threshold_frames, cutoff_freq):
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

        # Crear y añadir Checkbox y Label para el Noise Gate
        self.checkbox_gate = QCheckBox("Activar Supresor de Ruido (Auto-calibrable)")
        self.checkbox_gate.setChecked(auto_threshold_frames > 0)
        self.checkbox_gate.toggled.connect(self.toggle_noise_gate)
        layout.addWidget(self.checkbox_gate, 2, 0)

        self.label_rms = QLabel("Umbral RMS: Calibrando..." if auto_threshold_frames > 0 else "Umbral RMS: Inactivo")
        self.label_rms.setStyleSheet("color: #00ff88; font-weight: bold; font-size: 14px;")
        layout.addWidget(self.label_rms, 2, 1)

        # Checkbox para alternar entre líneas (interpolación) y puntos (discreto)
        self.checkbox_interp = QCheckBox("Sin interpolación (muestras crudas)")
        self.checkbox_interp.toggled.connect(self.toggle_interpolation)
        layout.addWidget(self.checkbox_interp, 3, 0, 1, 2)

        # Crear curvas
        self.curve_real = self.plot_real.plot(pen=pg.mkPen(color='#00d9ff', width=1.5))
        self.curve_imag = self.plot_imag.plot(pen=pg.mkPen(color='#ff00ff', width=1.5))
        self.curve_mag = self.plot_mag.plot(pen=pg.mkPen(color='#00ff88', width=1.5))
        self.curve_sig = self.plot_sig.plot(pen=pg.mkPen(color='#f0e68c', width=1.5))

        # Indicadores gráficos de Full Scale (FS)
        line_fs_pos = pg.InfiniteLine(pos=1.0, angle=0, pen=pg.mkPen('r', width=1.5, style=Qt.PenStyle.DashLine))
        line_fs_neg = pg.InfiniteLine(pos=-1.0, angle=0, pen=pg.mkPen('r', width=1.5, style=Qt.PenStyle.DashLine))
        self.plot_sig.addItem(line_fs_pos)
        self.plot_sig.addItem(line_fs_neg)

        # Iniciar hilo de lectura serial
        self.thread = SerialReaderThread(port, baudrate, play_audio, auto_threshold_frames, cutoff_freq)
        self.thread.data_ready.connect(self.update_plots)
        self.thread.status_message.connect(self.label_rms.setText)
        self.thread.start()

    def update_plots(self, real, imag, mag, sig):
        self.curve_real.setData(self.freqs, real)
        self.curve_imag.setData(self.freqs, imag)
        self.curve_mag.setData(self.freqs, mag)
        self.curve_sig.setData(sig)

    def toggle_noise_gate(self, checked):
        if self.thread:
            self.thread.noise_gate_enabled = checked
            # Reiniciar calibración cada vez que se enciende para adaptarse al ruido actual
            if checked:
                print("\n[Noise Gate] Reiniciando calibración...\n")
                self.thread.calibration_frames_count = 0
                self.thread.sum_rms = 0.0
                self.label_rms.setText("Umbral RMS: Calibrando...")
            else:
                self.label_rms.setText("Umbral RMS: Inactivo")

    def toggle_interpolation(self, checked):
        curves = [
            (self.curve_real, '#00d9ff'),
            (self.curve_imag, '#ff00ff'),
            (self.curve_mag, '#00ff88'),
            (self.curve_sig, '#f0e68c')
        ]
        for curve, color in curves:
            if checked:
                # Quita la línea que conecta los puntos y dibuja circulitos
                curve.setPen(None)
                curve.setSymbol('o')
                curve.setSymbolSize(4)
                curve.setSymbolBrush(color)
            else:
                # Vuelve a poner la línea estándar y quita los circulitos
                curve.setPen(pg.mkPen(color=color, width=1.5))
                curve.setSymbol(None)

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
    parser.add_argument("--auto-threshold", type=int, default=0, help="N frames para calibrar ruido de fondo (0=desactivado)")
    parser.add_argument("--cutoff", type=float, default=7000.0, help="Frecuencia de corte del filtro pasa bajos en Hz (Nyquist max = 8000)")
    args = parser.parse_args()

    global GLOBAL_GAIN
    GLOBAL_GAIN = args.gain

    app = QApplication(sys.argv)
    window = MainWindow(args.port, args.baudrate, args.play_audio, args.auto_threshold, args.cutoff)
    window.show()
    sys.exit(app.exec())

if __name__ == '__main__':
    main()
