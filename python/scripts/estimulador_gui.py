import sys
import struct
import time
import serial

try:
    from PySide6.QtWidgets import (
        QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
        QGridLayout, QGroupBox, QSpinBox, QSlider, QLabel, QPushButton,
        QLineEdit, QTextEdit, QScrollArea, QFrame, QMessageBox
    )
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QFont
except ImportError:
    try:
        from PyQt6.QtWidgets import (
            QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
            QGridLayout, QGroupBox, QSpinBox, QSlider, QLabel, QPushButton,
            QLineEdit, QTextEdit, QScrollArea, QFrame, QMessageBox
        )
        from PyQt6.QtCore import Qt
        from PyQt6.QtGui import QFont
    except ImportError:
        pass

if 'QMainWindow' not in globals():
    QMainWindow = object

NUM_ELECTRODES = 16
SNAP_THRESHOLD = 15  # Rango de atracción magnética hacia 0 para el slider

def compute_parity_bit(frame_15bits: int) -> int:
    """Calcula el bit de paridad par sobre los 15 bits superiores (bits 15 a 1)."""
    parity = 0
    val = frame_15bits & 0xFFFE
    while val:
        parity ^= (val & 1)
        val >>= 1
    return parity & 1

def build_stimulus_frame(electrode_idx: int, amplitude_signed: int) -> int:
    """
    Construye la trama de 16 bits para un estímulo:
    - Bit 15: Modo (1)
    - Bits 14-11: N° de electrodo (0 a 15)
    - Bits 10-1: Amplitud signada (-512 a +511)
    - Bit 0: Bit de paridad
    """
    amp_10bit = amplitude_signed & 0x3FF
    frame_15bits = (1 << 15) | ((electrode_idx & 0x0F) << 11) | (amp_10bit << 1)
    parity = compute_parity_bit(frame_15bits)
    return frame_15bits | parity

class StimulatorGUIMainWindow(QMainWindow):
    def __init__(self, port="COM3", baudrate=115200):
        super().__init__()
        self.default_port = port
        self.default_baudrate = baudrate
        self.ser = None

        self.setWindowTitle("Probador de Placa Estimuladora (pico_w_estimulador)")
        self.resize(1150, 780)

        self.setup_ui()

    def setup_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(15, 15, 15, 15)
        main_layout.setSpacing(12)

        # 1. Configuración Conexión Serial
        conn_group = QGroupBox("Conexión Serial")
        conn_group.setFont(QFont("Arial", 10, QFont.Weight.Bold))
        conn_layout = QHBoxLayout(conn_group)

        conn_layout.addWidget(QLabel("Puerto:"))
        self.port_input = QLineEdit(self.default_port)
        self.port_input.setFixedWidth(100)
        conn_layout.addWidget(self.port_input)

        conn_layout.addWidget(QLabel("Baudrate:"))
        self.baud_input = QSpinBox()
        self.baud_input.setRange(9600, 2000000)
        self.baud_input.setValue(self.default_baudrate)
        self.baud_input.setSingleStep(9600)
        self.baud_input.setFixedWidth(110)
        conn_layout.addWidget(self.baud_input)

        self.btn_connect = QPushButton("Conectar")
        self.btn_connect.setStyleSheet("background-color: #27ae60; color: white; font-weight: bold;")
        self.btn_connect.clicked.connect(self.toggle_connection)
        conn_layout.addWidget(self.btn_connect)

        self.label_conn_status = QLabel("Estado: Desconectado")
        self.label_conn_status.setStyleSheet("color: #e74c3c; font-weight: bold;")
        conn_layout.addWidget(self.label_conn_status)

        conn_layout.addStretch()
        main_layout.addWidget(conn_group)

        # 2. Área Principal Dividida: Grilla de Electrodos y Consola
        body_layout = QHBoxLayout()

        # Izquierda: Controles de Electrodos
        electrodes_group = QGroupBox("Configuración de Electrodos (0 - 15)")
        electrodes_group.setFont(QFont("Arial", 10, QFont.Weight.Bold))
        electrodes_vlayout = QVBoxLayout(electrodes_group)

        # Barra de acciones rápidas
        quick_bar = QHBoxLayout()
        btn_select_all = QPushButton("Seleccionar Todos")
        btn_select_all.clicked.connect(lambda: self.set_all_checks(True))
        quick_bar.addWidget(btn_select_all)

        btn_deselect_all = QPushButton("Desmarcar Todos")
        btn_deselect_all.clicked.connect(lambda: self.set_all_checks(False))
        quick_bar.addWidget(btn_deselect_all)

        quick_bar.addWidget(QLabel("Amplitud masiva:"))
        self.spin_bulk_amp = QSpinBox()
        self.spin_bulk_amp.setRange(-512, 511)
        self.spin_bulk_amp.setValue(0)
        quick_bar.addWidget(self.spin_bulk_amp)

        btn_apply_bulk = QPushButton("Aplicar a Todos")
        btn_apply_bulk.clicked.connect(self.apply_bulk_amplitude)
        quick_bar.addWidget(btn_apply_bulk)

        electrodes_vlayout.addLayout(quick_bar)

        # Scroll Area para los 16 electrodos
        scroll_area = QScrollArea()
        scroll_area.setWidgetResizable(True)
        scroll_widget = QWidget()
        self.grid_layout = QGridLayout(scroll_widget)
        self.grid_layout.setSpacing(10)

        self.channel_controls = []

        # Estilo para los botones de alternancia de electrodo
        self.toggle_btn_style = """
            QPushButton {
                font-weight: bold;
                font-size: 13px;
                border-radius: 5px;
                padding: 6px 12px;
                background-color: #34495e;
                color: #bdc3c7;
                border: 1px solid #2c3e50;
            }
            QPushButton:checked {
                background-color: #27ae60;
                color: white;
                border: 1px solid #2ecc71;
            }
            QPushButton:hover {
                border: 1px solid #3498db;
            }
        """

        # Encabezados de la grilla condensada
        headers = ["Electrodo", "Amplitud (-512 a +511)", "Previsualización Trama (Hex / Binario)"]
        for col, h in enumerate(headers):
            lbl = QLabel(h)
            lbl.setFont(QFont("Arial", 9, QFont.Weight.Bold))
            lbl.setStyleSheet("color: #2980b9;")
            self.grid_layout.addWidget(lbl, 0, col)

        for i in range(NUM_ELECTRODES):
            row = i + 1

            # Botón de alternancia que incluye el número de electrodo (E. 00, E. 01, ...)
            btn_toggle = QPushButton(f"E. {i:02d}")
            btn_toggle.setCheckable(True)
            btn_toggle.setChecked(True)
            btn_toggle.setStyleSheet(self.toggle_btn_style)
            btn_toggle.toggled.connect(self.update_previews)

            # Layout horizontal para el Slider de Amplitud + SpinBox + Botón Reset a 0
            amp_widget = QWidget()
            amp_layout = QHBoxLayout(amp_widget)
            amp_layout.setContentsMargins(0, 0, 0, 0)
            amp_layout.setSpacing(6)

            slider_amp = QSlider(Qt.Orientation.Horizontal)
            slider_amp.setRange(-512, 511)
            slider_amp.setValue(0)

            spin_amp = QSpinBox()
            spin_amp.setRange(-512, 511)
            spin_amp.setValue(0)
            spin_amp.setFixedWidth(70)

            btn_reset_zero = QPushButton("0")
            btn_reset_zero.setToolTip("Restablecer a 0")
            btn_reset_zero.setFixedWidth(26)
            btn_reset_zero.setStyleSheet("font-weight: bold; background-color: #7f8c8d; color: white; border-radius: 3px;")
            btn_reset_zero.clicked.connect(lambda checked=False, s=spin_amp: s.setValue(0))

            # Conectar Sincronización entre Slider y SpinBox con Snap a 0
            def make_slider_handler(s_amp=spin_amp, s_slider=slider_amp):
                def on_slider_change(val):
                    # Snap magnético a 0 cerca del centro
                    if abs(val) <= SNAP_THRESHOLD and val != 0:
                        s_slider.blockSignals(True)
                        s_slider.setValue(0)
                        s_slider.blockSignals(False)
                        val = 0
                    s_amp.blockSignals(True)
                    s_amp.setValue(val)
                    s_amp.blockSignals(False)
                    self.update_previews()
                return on_slider_change

            def make_spin_handler(s_slider=slider_amp):
                def on_spin_change(val):
                    s_slider.blockSignals(True)
                    s_slider.setValue(val)
                    s_slider.blockSignals(False)
                    self.update_previews()
                return on_spin_change

            slider_amp.valueChanged.connect(make_slider_handler(spin_amp, slider_amp))
            spin_amp.valueChanged.connect(make_spin_handler(slider_amp))

            amp_layout.addWidget(slider_amp, stretch=1)
            amp_layout.addWidget(spin_amp)
            amp_layout.addWidget(btn_reset_zero)

            lbl_preview = QLabel()
            lbl_preview.setFont(QFont("Consolas", 9))
            lbl_preview.setStyleSheet("color: #27ae60;")

            self.grid_layout.addWidget(btn_toggle, row, 0)
            self.grid_layout.addWidget(amp_widget, row, 1)
            self.grid_layout.addWidget(lbl_preview, row, 2)

            self.channel_controls.append({
                'toggle': btn_toggle,
                'spin': spin_amp,
                'slider': slider_amp,
                'preview': lbl_preview
            })

        scroll_area.setWidget(scroll_widget)
        electrodes_vlayout.addWidget(scroll_area)
        body_layout.addWidget(electrodes_group, stretch=3)

        # Derecha: Panel de Transmisión y Log
        right_panel = QVBoxLayout()

        # Botón de Disparo Único (Single Shot)
        self.btn_send_single = QPushButton("Enviar Tramas Seleccionadas (Single Shot)")
        self.btn_send_single.setMinimumHeight(45)
        self.btn_send_single.setFont(QFont("Arial", 11, QFont.Weight.Bold))
        self.btn_send_single.setStyleSheet("""
            QPushButton {
                background-color: #2980b9;
                color: white;
                border-radius: 5px;
            }
            QPushButton:hover {
                background-color: #3498db;
            }
        """)
        self.btn_send_single.clicked.connect(self.send_single_shot)
        right_panel.addWidget(self.btn_send_single)

        # Consola de Log
        log_group = QGroupBox("Monitor de Transmisión")
        log_group.setFont(QFont("Arial", 10, QFont.Weight.Bold))
        log_layout = QVBoxLayout(log_group)

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFont(QFont("Consolas", 9))
        self.log_text.setStyleSheet("background-color: #1e1e1e; color: #00ff88;")
        log_layout.addWidget(self.log_text)

        btn_clear_log = QPushButton("Limpiar Consola")
        btn_clear_log.clicked.connect(self.log_text.clear)
        log_layout.addWidget(btn_clear_log)

        right_panel.addWidget(log_group, stretch=1)
        body_layout.addLayout(right_panel, stretch=2)

        main_layout.addLayout(body_layout)

        self.update_previews()

    def set_all_checks(self, checked: bool):
        for ctrl in self.channel_controls:
            ctrl['toggle'].setChecked(checked)

    def apply_bulk_amplitude(self):
        val = self.spin_bulk_amp.value()
        for ctrl in self.channel_controls:
            ctrl['spin'].setValue(val)

    def update_previews(self):
        for i, ctrl in enumerate(self.channel_controls):
            if ctrl['toggle'].isChecked():
                amp = ctrl['spin'].value()
                frame = build_stimulus_frame(i, amp)
                parity = frame & 1
                bin_str = f"{frame:016b}"
                formatted_bin = f"{bin_str[0]} {bin_str[1:5]} {bin_str[5:15]} {bin_str[15]}"
                ctrl['preview'].setText(f"0x{frame:04X}  [{formatted_bin}] P={parity}")
                ctrl['preview'].setStyleSheet("color: #27ae60;")
            else:
                ctrl['preview'].setText("--- (Desactivado) ---")
                ctrl['preview'].setStyleSheet("color: gray;")

    def force_disconnect(self, reason: str = "Desconectado por Error USB"):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.btn_connect.setText("Conectar")
        self.btn_connect.setStyleSheet("background-color: #27ae60; color: white; font-weight: bold;")
        self.label_conn_status.setText(f"Estado: {reason}")
        self.label_conn_status.setStyleSheet("color: #e74c3c; font-weight: bold;")
        self.log_info(f"[USB Error] {reason}")

    def toggle_connection(self):
        if self.ser and self.ser.is_open:
            self.force_disconnect("Desconectado manualmente")
        else:
            port = self.port_input.text().strip()
            baud = self.baud_input.value()
            try:
                self.ser = serial.Serial(port, baud, timeout=1, write_timeout=1.0)
                self.btn_connect.setText("Desconectar")
                self.btn_connect.setStyleSheet("background-color: #e74c3c; color: white; font-weight: bold;")
                self.label_conn_status.setText(f"Estado: Conectado ({port})")
                self.label_conn_status.setStyleSheet("color: #27ae60; font-weight: bold;")
                self.log_info(f"Conectado a {port} a {baud} baudios.")
            except (serial.SerialException, OSError, Exception) as e:
                self.ser = None
                QMessageBox.critical(self, "Error de Conexión USB", f"No se pudo abrir el puerto serie {port}:\n{str(e)}")
                self.log_info(f"Error abriendo {port}: {e}")

    def send_single_shot(self):
        selected_frames = []
        for i, ctrl in enumerate(self.channel_controls):
            if ctrl['toggle'].isChecked():
                amp = ctrl['spin'].value()
                frame = build_stimulus_frame(i, amp)
                selected_frames.append((i, amp, frame))

        if not selected_frames:
            QMessageBox.information(self, "Sin Selección", "No hay ningún electrodo tildado para enviar.")
            return

        self.log_info(f"--- Iniciando Disparo Único ({len(selected_frames)} tramas) ---")

        for idx, amp, frame in selected_frames:
            # Enviar binario directo MSB First (Big-Endian >H)
            raw_bytes = struct.pack('>H', frame)
            sent_status = "SIMULADO (No conectado)"
            
            if self.ser and self.ser.is_open:
                try:
                    self.ser.write(raw_bytes)
                    self.ser.flush()
                    sent_status = "ENVIADO SERIE"
                except (serial.SerialException, serial.SerialTimeoutException, OSError, Exception) as e:
                    sent_status = f"ERROR SERIE: {e}"
                    parity = frame & 1
                    bin_str = f"{frame:016b}"
                    formatted_bin = f"{bin_str[0]} {bin_str[1:5]} {bin_str[5:15]} {bin_str[15]}"
                    bytes_hex = " ".join(f"{b:02X}" for b in raw_bytes)
                    msg = (f"[E. {idx:02d}] Amp: {amp:+4d} -> Trama: 0x{frame:04X} "
                           f"({bytes_hex}) [{formatted_bin}] | Paridad={parity} | {sent_status}")
                    self.log_info(msg)
                    self.force_disconnect(f"Error de transmisión USB: {e}")
                    QMessageBox.warning(self, "Desconexión USB", f"Se perdió la conexión serie durante el envío:\n{str(e)}")
                    break

            parity = frame & 1
            bin_str = f"{frame:016b}"
            formatted_bin = f"{bin_str[0]} {bin_str[1:5]} {bin_str[5:15]} {bin_str[15]}"
            bytes_hex = " ".join(f"{b:02X}" for b in raw_bytes)

            msg = (f"[E. {idx:02d}] Amp: {amp:+4d} -> Trama: 0x{frame:04X} "
                   f"({bytes_hex}) [{formatted_bin}] | Paridad={parity} | {sent_status}")
            self.log_info(msg)

        self.log_info("--- Fin del Disparo Único ---\n")

    def log_info(self, text: str):
        t_stamp = time.strftime("%H:%M:%S")
        self.log_text.append(f"[{t_stamp}] {text}")

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Probador GUI de Placa Estimuladora (pico_w_estimulador)")
    parser.add_argument("port", nargs="?", type=str, default="COM3", help="Puerto serial (ej. COM3 o /dev/ttyACM0)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate (default: 115200)")
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    window = StimulatorGUIMainWindow(port=args.port, baudrate=args.baudrate)
    window.show()

    sys.exit(app.exec())

if __name__ == "__main__":
    main()
