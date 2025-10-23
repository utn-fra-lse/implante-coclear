import serial
import serial.tools.list_ports

from PyQt6.QtCore import QThread, pyqtSignal, pyqtSlot

def list_serial_ports():
    ports = serial.tools.list_ports.comports()
    return [port.device for port in ports]

class SerialReaderThread(QThread):

    connection = pyqtSignal(str, name='connection')
    new_data = pyqtSignal(int, name='new_data')
    new_serial_data = pyqtSignal(dict, name='new serial data')

    def __init__(self):
        super().__init__()
        self.ser = None
        self.baud_rate = 9600
        self.port = 'COM6'
        # self.haveData = False
        self.x = 0
        self.y = 0
        self._running = False
    
    def set_port(self, port):
        self.port = port
    
    def set_baud_rate(self, baud_rate):
        self.baud_rate = baud_rate

    def init_com(self):
        print(f"Inicializando conexión en {self.port} a {self.baud_rate} baudios...")
        try:
            # self.connection.emit("Connecting")
            self.ser = serial.Serial(self.port, self.baud_rate, timeout=1)

            # self.connection.emit("Connected")

            return True
        except:
            print("ERROR")
            # self.connection.emit("Fail")
            return False

    def run(self):
        self._running = True
        raw_data = []
        label_data = []
        value = []
        data = {}

        if self.init_com():
            while self._running:
                if self.ser is not None and self.ser.is_open:
                    try:
                        if self.ser.in_waiting > 0:
                            string_serial = self.ser.readline().decode('utf-8').strip()
                            if "[" in string_serial or "]" in string_serial:
                                continue
                            # self.haveData = True
                            # raw_data = string_serial.split(',')
                            # k_v = [e.split(":") for e in raw_data]

                            # for key, value in k_v:
                            #     data[key.replace(" ","")]=float(value)
                            key, value = string_serial.split(":")
                            self.new_serial_data.emit({
                                key.strip(): float(value.strip())
                            })

                            # if data[0] == '+':
                            #     print("Esperando instrucciones")

                    except Exception as e:
                        break
        
    def close(self):
        print("Cerrando hilo de lectura serial...")
        self._running = False
        if self.ser:
            if self.ser.is_open:
                print("Cerrando puerto serial...")
                self.ser.close()
            self.ser = None

if __name__ == "__main__":
    ser = serial.Serial("COM8", 9600)

    ds = {}
    data = ser.readline().decode('utf-8').strip()
    raw_data = data.split(',')
    print(data)
    print(raw_data)
    k_v = [e.split(":") for e in raw_data]
    print(k_v)

    for key, value in k_v:
        ds[key.replace(" ","")]=float(value)
    
    print(ds)
    
    ser.close()
