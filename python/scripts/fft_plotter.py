import sys
import time
import struct
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

def audio_callback(outdata, frames, time_info, status):
    if status:
        print(f"Audio status: {status}")
    try:
        data = audio_queue.get_nowait()
        scaled = np.clip(data * GLOBAL_GAIN, -1.0, 1.0)
        outdata[:] = scaled.reshape(-1, 1)
    except queue.Empty:
        outdata[:] = np.zeros((frames, 1), dtype=np.float32)

class SerialReaderThread(QThread):
    data_ready = pyqtSignal(np.ndarray, np.ndarray, np.ndarray, np.ndarray)
    status_message = pyqtSignal(str)
    
    def __init__(self, port, baudrate, play_audio, auto_threshold_frames, cutoff_freq, target_fs=16000):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.play_audio = play_audio
        self.auto_threshold_frames = auto_threshold_frames
        self.target_fs = target_fs
        self.running = True
        self.ser = None
        self.ola_buffer = np.zeros(256, dtype=np.float32)
        
        self.cutoff_freq = cutoff_freq
        nyq = self.target_fs / 2.0
        if self.cutoff_freq > 0 and self.cutoff_freq < nyq:
            self.b, self.a = scipy_signal.butter(4, self.cutoff_freq / nyq, btype='low')
            self.zi = scipy_signal.lfilter_zi(self.b, self.a)
        else:
            self.b, self.a, self.zi = None, None, None

        self.noise_gate_enabled = (auto_threshold_frames > 0)
        self.threshold_value = 0.0
        self.calibration_frames_count = 0
        self.sum_rms = 0.0

    def apply_noise_gate(self, signal):
        if not self.noise_gate_enabled:
            return signal

        rms = np.sqrt(np.mean(signal**2))
        frames_to_calibrate = self.auto_threshold_frames if self.auto_threshold_frames > 0 else 62
        
        if self.calibration_frames_count < frames_to_calibrate:
            self.sum_rms += rms
            self.calibration_frames_count += 1
            if self.calibration_frames_count == frames_to_calibrate:
                self.threshold_value = (self.sum_rms / frames_to_calibrate) * 1.5
                dbfs = 20 * np.log10(max(self.threshold_value, 1e-6) / 1.0)
                rms_str = f"{self.threshold_value:.2e}" if self.threshold_value < 0.01 else f"{self.threshold_value:.3f}"
                msg = f"Umbral RMS: {rms_str}  ({dbfs:.1f} dBFS)"
                print(f"\n[Noise Gate] Calibración terminada. {msg}\n")
                self.status_message.emit(msg)
            return np.zeros_like(signal)

        if rms < self.threshold_value:
            return np.zeros_like(signal)
        
        return signal

    def run(self):
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"Connected to {self.port} at {self.baudrate} baud.")
            if self.target_fs:
                time.sleep(0.2)
                cmd = f"SET_FS:{self.target_fs}\n"
                self.ser.write(cmd.encode('ascii'))
                self.ser.flush()
                print(f"Sent command to Pico 2: {cmd.strip()}")
        except Exception as e:
            print(f"Error opening serial port: {e}")
            return

        buffer = bytearray()
        packet_size = 2072  # 2056 + 16
        sync_header = b'\xAA\x55'
        frame_count = 0
        stream = None
        current_active_fs = None
        
        if self.play_audio:
            try:
                stream = sd.OutputStream(
                    samplerate=self.target_fs, 
                    channels=1, 
                    blocksize=256, 
                    callback=audio_callback
                )
                stream.start()
                print(f"Audio playback started at {self.target_fs} Hz.")
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
                    
                    pkt_fs = struct.unpack('<I', packet[4:8])[0]
                    if pkt_fs != current_active_fs and pkt_fs > 0:
                        current_active_fs = pkt_fs
                        print(f"[Pico 2 CDC] Frecuencia activa confirmada por hardware: {current_active_fs} Hz")

                    floats_data = packet[8:2056]
                    all_floats = np.frombuffer(floats_data, dtype=np.float32)
                    
                    if len(all_floats) == 512:
                        real_part = all_floats[:256]
                        imag_part = all_floats[256:]
                        
                        complex_spec = real_part + 1j * imag_part
                        signal = np.fft.irfft(complex_spec, n=512)
                        
                        signal = self.apply_noise_gate(signal)
                        
                        if self.play_audio:
                            audio_out = signal[:256] + self.ola_buffer
                            self.ola_buffer = signal[256:]
                            
                            if self.b is not None:
                                audio_out, self.zi = scipy_signal.lfilter(self.b, self.a, audio_out, zi=self.zi)
                                
                            try:
                                audio_queue.put_nowait(audio_out.astype(np.float32))
                            except queue.Empty:
                                pass
                                
                        frame_count += 1
                        
                        if frame_count % 4 == 0:
                            magnitude = np.sqrt(real_part**2 + imag_part**2)
                            self.data_ready.emit(real_part, imag_part, magnitude, signal)
                else:
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
    def __init__(self, port, baudrate, play_audio, auto_threshold_frames, cutoff_freq, target_fs=16000):
        super().__init__()
        self.target_fs = target_fs
        self.setWindowTitle(f"FFT Serial Plotter - {port} ({self.target_fs} Hz)")
        self.resize(1000, 800)

        pg.setConfigOptions(antialias=False, background='#0d1117', foreground='#c9d1d9')

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QGridLayout(central_widget)

        self.plot_real = pg.PlotWidget(title="Parte Real")
        self.plot_real.setLabel('bottom', 'Frecuencia (Hz)')
        self.plot_imag = pg.PlotWidget(title="Parte Imaginaria")
        self.plot_imag.setLabel('bottom', 'Frecuencia (Hz)')
        self.plot_mag = pg.PlotWidget(title="Magnitud")
        self.plot_mag.setLabel('bottom', 'Frecuencia (Hz)')
        self.plot_sig = pg.PlotWidget(title="Señal Temporal (IFFT)")
        self.plot_sig.setLabel('bottom', 'Muestras')

        self.plot_real.showGrid(x=True, y=True, alpha=0.3)
        self.plot_imag.showGrid(x=True, y=True, alpha=0.3)
        self.plot_mag.showGrid(x=True, y=True, alpha=0.3)
        self.plot_sig.showGrid(x=True, y=True, alpha=0.3)

        nyq = self.target_fs / 2.0
        self.plot_real.setXRange(0, nyq, padding=0)
        self.plot_imag.setXRange(0, nyq, padding=0)
        self.plot_mag.setXRange(0, nyq, padding=0)
        self.plot_sig.setXRange(0, 511, padding=0)

        self.freqs = np.arange(256) * (self.target_fs / 512)

        self.plot_real.setYRange(-10, 10, padding=0)
        self.plot_imag.setYRange(-10, 10, padding=0)
        self.plot_mag.setYRange(0, 10, padding=0)
        self.plot_sig.setYRange(-1, 1, padding=0)

        for plot in [self.plot_real, self.plot_imag, self.plot_mag, self.plot_sig]:
            plot.disableAutoRange()

        layout.addWidget(self.plot_real, 0, 0)
        layout.addWidget(self.plot_imag, 0, 1)
        layout.addWidget(self.plot_mag, 1, 0)
        layout.addWidget(self.plot_sig, 1, 1)

        self.checkbox_gate = QCheckBox("Activar Supresor de Ruido (Auto-calibrable)")
        self.checkbox_gate.setChecked(auto_threshold_frames > 0)
        self.checkbox_gate.toggled.connect(self.toggle_noise_gate)
        layout.addWidget(self.checkbox_gate, 2, 0)

        self.label_rms = QLabel("Umbral RMS: Calibrando..." if auto_threshold_frames > 0 else "Umbral RMS: Inactivo")
        self.label_rms.setStyleSheet("color: #00ff88; font-weight: bold; font-size: 14px;")
        layout.addWidget(self.label_rms, 2, 1)

        self.curve_real = self.plot_real.plot(pen=pg.mkPen('#58a6ff', width=1.5))
        self.curve_imag = self.plot_imag.plot(pen=pg.mkPen('#bc8cff', width=1.5))
        self.curve_mag  = self.plot_mag.plot(pen=pg.mkPen('#3fb950', width=1.5))
        self.curve_sig  = self.plot_sig.plot(pen=pg.mkPen('#d29922', width=1.5))

        self.thread = SerialReaderThread(port, baudrate, play_audio, auto_threshold_frames, cutoff_freq, target_fs=self.target_fs)
        self.thread.data_ready.connect(self.update_plots)
        self.thread.status_message.connect(self.update_status_label)
        self.thread.start()

    def update_status_label(self, msg):
        self.label_rms.setText(f"Umbral RMS: {msg}")

    def toggle_noise_gate(self, checked):
        if self.thread:
            self.thread.noise_gate_enabled = checked
            if not checked:
                self.label_rms.setText("Umbral RMS: Inactivo")
            else:
                self.label_rms.setText("Umbral RMS: Activado")

    def update_plots(self, real_part, imag_part, magnitude, signal):
        self.curve_real.setData(self.freqs, real_part)
        self.curve_imag.setData(self.freqs, imag_part)
        self.curve_mag.setData(self.freqs, magnitude)
        self.curve_sig.setData(signal)

    def closeEvent(self, event):
        print("Closing application...")
        if self.thread:
            self.thread.stop()
        event.accept()

def main():
    parser = argparse.ArgumentParser(description="FFT Serial Plotter")
    parser.add_argument("port", type=str, help="Serial port (e.g., COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--fs", type=int, default=16000, help="Frecuencia de muestreo ADC deseada en Hz (8000..64000)")
    parser.add_argument("--play-audio", action="store_true", help="Escuchar el audio reconstruido por IFFT")
    parser.add_argument("--gain", type=float, default=1.0, help="Multiplicador de ganancia de audio (ej. 2.0, 10.0)")
    parser.add_argument("--auto-threshold", type=int, default=0, help="N frames para calibrar ruido de fondo (0=desactivado)")
    parser.add_argument("--cutoff", type=float, default=7000.0, help="Frecuencia de corte del filtro pasa bajos en Hz (Nyquist max = 8000)")
    args = parser.parse_args()

    global GLOBAL_GAIN
    GLOBAL_GAIN = args.gain

    app = QApplication(sys.argv)
    window = MainWindow(args.port, args.baudrate, args.play_audio, args.auto_threshold, args.cutoff, target_fs=args.fs)
    window.show()
    sys.exit(app.exec())

if __name__ == '__main__':
    main()
