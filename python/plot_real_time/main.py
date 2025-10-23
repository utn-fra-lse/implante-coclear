import sys
import numpy as np
from pyqtgraph.Qt import QtGui, QtCore
from PySide6.QtWidgets import QMainWindow, QApplication, QPushButton
from PySide6.QtCore import QRect
from PySide6.QtWidgets import QGridLayout, QWidget
from collections import deque
from PyQt6.QtCore import QThread, pyqtSignal, pyqtSlot

from SerialThread import SerialReaderThread, list_serial_ports

import pyqtgraph as pg

# --- Ventana de gráfico ---
class PlotWindow:
    def __init__(self):
        
        self.win = pg.GraphicsLayoutWidget(show=True)
        self.win.setWindowTitle('PyQtGraph: Gráfico en tiempo real')
        self.win.resize(800, 600)
        self.plot = self.win.addPlot(title="Datos en tiempo real")
        self.curve = self.plot.plot(pen='y')

        # Añadir leyenda
        self.legend = self.plot.addLegend()

        # Inicializar curvas como dict
        self.curves = {}
        self.data_buffers = {}
        self.max_len = 256
        self.data_x = []
        self.data_y = []

        self.plot.setLabel('left', 'Valor', units='V')
        self.plot.setLabel('bottom', 'Tiempo', units='hz')
        self.plot.setYRange(0, 100, padding=0)

        self.is_stop = False
        self.ptr = 0

        self.count = 0
        self.keys_cant = 0

    def update(self, data: dict):
        if self.is_stop:
            return

        for key, value in data.items():
            self.data_x.append(int(key))
            self.data_y.append(value)

            self.ptr += 1

        if self.ptr >= self.max_len:
            y = np.array(self.data_y)
            self.curve.setData(np.array(self.data_x), y)
            self.plot.setYRange(0, max(100, np.max(y)), padding=0)
            self.ptr = 0
            self.data_x = []
            self.data_y = []

    def stop(self):
        self.is_stop = not self.is_stop

# --- Ventana de comandos ---
from PySide6.QtWidgets import QComboBox, QLabel
class CommandWindow:
    new_serial_data = pyqtSignal(list, name='new serial data')

    def __init__(self):
        self.window = QMainWindow()
        self.widget = QWidget()
        self.layout = QGridLayout(self.widget)
        self.label_port = QLabel('Puerto:')
        self.combo_port = QComboBox()
        self.combo_port.addItems(list_serial_ports())
        self.label_baud = QLabel('Baudrate:')
        self.combo_baud = QComboBox()
        self.combo_baud.addItems(['9600', '115200'])
        self.button_connect = QPushButton('Conectar')
        self.layout.addWidget(self.label_port, 0, 0)
        self.layout.addWidget(self.combo_port, 0, 1)
        self.layout.addWidget(self.label_baud, 1, 0)
        self.layout.addWidget(self.combo_baud, 1, 1)
        self.layout.addWidget(self.button_connect, 2, 0, 1, 2)
        self.window.setCentralWidget(self.widget)
        self.window.setWindowTitle('Comandos')
        self.window.resize(300, 150)
        self.window.show()

        self.serial_thread = SerialReaderThread()

        self.is_connected = False

        self.button_connect.clicked.connect(self.connect_serial)
    
    def close(self):
        self.serial_thread.quit()
        self.window.close()
    
    def connect_serial(self):
        if self.is_connected:
            # self.serial_thread.quit()
            self.button_connect.setText('Connect')
            self.serial_thread.close()
        else:   
            port = self.combo_port.currentText()
            baud_rate = int(self.combo_baud.currentText())
            self.serial_thread.set_port(port)
            self.serial_thread.set_baud_rate(baud_rate)
            self.serial_thread.start()

        self.button_connect.setText('Disconect' if not self.is_connected else 'Connect')
        self.is_connected = not self.is_connected

# --- Clase principal ---
class MainWindow:
    def __init__(self):
        self.app = QApplication(sys.argv)
        self.plot_window = PlotWindow()
        self.command_window = CommandWindow()
        self.command_window.serial_thread.new_serial_data.connect(self.plot_window.update)
        # self.command_window.button_connect.clicked.connect(self.plot_window.stop)

    def run(self):
        if (sys.flags.interactive != 1):
            QApplication.instance().exec()
    
    def close(self):
        self.plot_window.stop()
        self.command_window.serial_thread.close()
        self.app.quit()


if __name__ == '__main__':
    main = MainWindow()
    main.run()