import sys
import subprocess
import os
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QPushButton, 
    QLabel, QFrame, QHBoxLayout, QMessageBox, QLineEdit, QScrollArea,
    QCheckBox, QDoubleSpinBox, QSpinBox, QFormLayout, QGroupBox
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QFont

class ScriptLauncherMenu(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Menú de Scripts - Implante Coclear")
        self.setMinimumSize(550, 500)
        
        self.base_dir = os.path.dirname(os.path.abspath(__file__))
        self.scripts_dir = os.path.join(self.base_dir, "scripts")
        
        # Central widget and layout
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(20, 20, 20, 20)
        main_layout.setSpacing(15)
        
        # Title
        title_label = QLabel("Lanzador de Scripts")
        title_font = QFont("Arial", 18, QFont.Bold)
        title_label.setFont(title_font)
        title_label.setAlignment(Qt.AlignCenter)
        main_layout.addWidget(title_label)
        
        # Subtitle
        subtitle_label = QLabel(f"Directorio: {self.scripts_dir}")
        subtitle_label.setAlignment(Qt.AlignCenter)
        subtitle_label.setStyleSheet("color: gray;")
        main_layout.addWidget(subtitle_label)
        
        # Separator
        line = QFrame()
        line.setFrameShape(QFrame.HLine)
        line.setFrameShadow(QFrame.Sunken)
        main_layout.addWidget(line)

        # Scroll Area for scripts
        scroll_area = QScrollArea()
        scroll_area.setWidgetResizable(True)
        scroll_area.setFrameShape(QFrame.NoFrame)
        
        scroll_widget = QWidget()
        self.scripts_layout = QVBoxLayout(scroll_widget)
        self.scripts_layout.setSpacing(20)

        self.load_scripts()
        
        self.scripts_layout.addStretch()
        scroll_area.setWidget(scroll_widget)
        main_layout.addWidget(scroll_area)

    def load_scripts(self):
        if not os.path.exists(self.scripts_dir):
            error_label = QLabel("No se encontró la carpeta 'scripts'")
            error_label.setStyleSheet("color: red;")
            self.scripts_layout.addWidget(error_label)
            return

        scripts = [f for f in os.listdir(self.scripts_dir) if f.endswith('.py') and f != '__init__.py']
        
        if not scripts:
            empty_label = QLabel("No hay scripts disponibles en la carpeta.")
            self.scripts_layout.addWidget(empty_label)
            return

        for script_name in sorted(scripts):
            self.create_script_card(script_name)

    def create_script_card(self, script_name):
        group_box = QGroupBox(script_name)
        group_font = QFont("Arial", 11, QFont.Bold)
        group_box.setFont(group_font)
        group_box.setStyleSheet("""
            QGroupBox {
                border: 1px solid #34495e;
                border-radius: 6px;
                margin-top: 10px;
                padding-top: 15px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 3px 0 3px;
                color: #2980b9;
            }
        """)
        
        layout = QHBoxLayout(group_box)
        
        form_layout = QFormLayout()
        form_layout.setFieldGrowthPolicy(QFormLayout.ExpandingFieldsGrow)
        
        # Diccionario para almacenar los widgets de este script
        widgets = {}
        
        # Configuraciones específicas según el script
        if script_name in ["cochlear_plotter.py", "fft_plotter.py", "fft_audio_player.py", "fft_audio_recorder.py", "dual_audio_recorder.py"]:
            # Puerto
            port_input = QLineEdit("COM3")
            form_layout.addRow("Puerto Serial:", port_input)
            widgets['port'] = port_input
            
            # Baudrate
            baud_input = QSpinBox()
            baud_input.setRange(9600, 2000000)
            baud_input.setValue(115200)
            baud_input.setSingleStep(9600)
            form_layout.addRow("Baudrate:", baud_input)
            widgets['baudrate'] = baud_input

            if script_name in ["cochlear_plotter.py", "fft_plotter.py", "fft_audio_player.py", "dual_audio_recorder.py"]:
                # Ganancia
                gain_input = QDoubleSpinBox()
                gain_input.setRange(0.1, 100.0)
                gain_input.setValue(1.0)
                gain_input.setSingleStep(0.5)
                form_layout.addRow("Ganancia de Audio:", gain_input)
                widgets['gain'] = gain_input

            if script_name in ["cochlear_plotter.py", "fft_plotter.py"]:
                # Play Audio
                play_audio_input = QCheckBox("Escuchar salida de audio")
                form_layout.addRow("", play_audio_input)
                widgets['play_audio'] = play_audio_input
                
            if script_name in ["fft_plotter.py", "cochlear_plotter.py"]:
                # Auto threshold
                auto_thresh_input = QSpinBox()
                auto_thresh_input.setRange(0, 1000)
                auto_thresh_input.setValue(0)
                form_layout.addRow("Auto-Threshold (frames):", auto_thresh_input)
                widgets['auto_threshold'] = auto_thresh_input

            if script_name == "fft_audio_recorder.py":
                output_input = QLineEdit("grabacion.wav")
                form_layout.addRow("Archivo de salida:", output_input)
                widgets['output'] = output_input
                
            if script_name == "dual_audio_recorder.py":
                out_fft_input = QLineEdit("grabacion_fft.wav")
                form_layout.addRow("Salida FFT:", out_fft_input)
                widgets['out_fft'] = out_fft_input
                
                out_voc_input = QLineEdit("grabacion_vocoder.wav")
                form_layout.addRow("Salida Vocoder:", out_voc_input)
                widgets['out_vocoder'] = out_voc_input
                
            if script_name in ["fft_plotter.py", "fft_audio_recorder.py", "dual_audio_recorder.py"]:
                cutoff_input = QDoubleSpinBox()
                cutoff_input.setRange(0.0, 8000.0)
                cutoff_input.setValue(7000.0)
                cutoff_input.setSingleStep(500.0)
                form_layout.addRow("Corte Filtro Pasa Bajos (Hz):", cutoff_input)
                widgets['cutoff'] = cutoff_input

        elif script_name == "wav_to_mp3.py":
            input_wav = QLineEdit("grabacion.wav")
            form_layout.addRow("WAV entrada:", input_wav)
            widgets['input'] = input_wav
            
            output_mp3 = QLineEdit("")
            output_mp3.setPlaceholderText("Opcional (auto-generado)")
            form_layout.addRow("MP3 salida:", output_mp3)
            widgets['output'] = output_mp3
            
            bitrate = QLineEdit("192k")
            form_layout.addRow("Bitrate:", bitrate)
            widgets['bitrate'] = bitrate
            
        else:
            # Script genérico sin argumentos definidos visualmente, campo libre
            args_input = QLineEdit()
            args_input.setPlaceholderText("Argumentos opcionales...")
            form_layout.addRow("Argumentos:", args_input)
            widgets['generic_args'] = args_input

        layout.addLayout(form_layout)
        
        # Botón Lanzar
        btn = QPushButton("Lanzar")
        btn.setMinimumHeight(40)
        btn.setMinimumWidth(100)
        btn.setStyleSheet("""
            QPushButton {
                background-color: #27ae60;
                color: white;
                font-weight: bold;
                border-radius: 4px;
            }
            QPushButton:hover {
                background-color: #2ecc71;
            }
        """)
        
        script_path = os.path.join(self.scripts_dir, script_name)
        btn.clicked.connect(lambda checked=False, path=script_path, w=widgets, s=script_name: self.run_script(path, w, s))
        
        btn_layout = QVBoxLayout()
        btn_layout.addStretch()
        btn_layout.addWidget(btn)
        btn_layout.addStretch()
        
        layout.addLayout(btn_layout)
        
        self.scripts_layout.addWidget(group_box)

    def run_script(self, script_path, widgets, script_name):
        if not os.path.exists(script_path):
            QMessageBox.critical(self, "Error", f"No se encontró el script:\n{script_path}")
            return
            
        command = [sys.executable, script_path]
        
        if script_name in ["cochlear_plotter.py", "fft_plotter.py", "fft_audio_player.py", "fft_audio_recorder.py", "dual_audio_recorder.py"]:
            port = widgets['port'].text().strip()
            if not port:
                QMessageBox.warning(self, "Falta dato", "El puerto serial es obligatorio.")
                return
            command.append(port)
            command.append("--baudrate")
            command.append(str(widgets['baudrate'].value()))
            
            if 'gain' in widgets:
                command.append("--gain")
                command.append(str(widgets['gain'].value()))
                
            if 'play_audio' in widgets and widgets['play_audio'].isChecked():
                command.append("--play-audio")
                
            if 'auto_threshold' in widgets and widgets['auto_threshold'].value() > 0:
                command.append("--auto-threshold")
                command.append(str(widgets['auto_threshold'].value()))
                
            if 'output' in widgets:
                out_val = widgets['output'].text().strip()
                if out_val:
                    command.append("--output")
                    command.append(out_val)
                    
            if 'out_fft' in widgets:
                out_val = widgets['out_fft'].text().strip()
                if out_val:
                    command.append("--out-fft")
                    command.append(out_val)
                    
            if 'out_vocoder' in widgets:
                out_val = widgets['out_vocoder'].text().strip()
                if out_val:
                    command.append("--out-vocoder")
                    command.append(out_val)

            if 'cutoff' in widgets:
                command.append("--cutoff")
                command.append(str(widgets['cutoff'].value()))

        elif script_name == "wav_to_mp3.py":
            in_val = widgets['input'].text().strip()
            if not in_val:
                QMessageBox.warning(self, "Falta dato", "El archivo de entrada es obligatorio.")
                return
            command.append(in_val)
            
            out_val = widgets['output'].text().strip()
            if out_val:
                command.append("--output")
                command.append(out_val)
                
            br_val = widgets['bitrate'].text().strip()
            if br_val:
                command.append("--bitrate")
                command.append(br_val)
                
        else:
            import shlex
            args_text = widgets['generic_args'].text().strip()
            if args_text:
                command.extend(shlex.split(args_text))
            
        try:
            subprocess.Popen(command, cwd=os.path.dirname(script_path))
        except Exception as e:
            QMessageBox.critical(self, "Error al ejecutar", f"Error:\n{str(e)}")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    
    window = ScriptLauncherMenu()
    window.show()
    
    sys.exit(app.exec())
