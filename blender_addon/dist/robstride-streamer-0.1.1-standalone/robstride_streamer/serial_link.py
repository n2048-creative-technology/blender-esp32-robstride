import time
import threading
import sys
import os
from pathlib import Path

# Try to add pyserial to path if not already imported
if 'serial' not in sys.modules:
    try:
        # Check common locations for pyserial
        _home = Path.home()
        for _version in ["5.0", "6.0", "4.2", "3.6", "3.5", "3.4", "3.3", "3.2", "3.1", "3.0"]:
            _site_packages = _home / ".config" / "blender" / _version / "scripts" / "addons" / "site-packages"
            if _site_packages.exists() and str(_site_packages) not in sys.path:
                sys.path.insert(0, str(_site_packages))
    except Exception:
        pass

try:
    import serial
    from serial.tools import list_ports
    _pyserial_available = True
except Exception as e:  # pragma: no cover - Blender env may not have pyserial
    serial = None
    list_ports = None
    _pyserial_available = False
    _pyserial_error = str(e)

from .protocol import pack_setpoints, pack_command, Parser, MSG_TELEMETRY, MSG_ERROR


class SerialLink:
    def __init__(self, loopback=False):
        self.loopback = bool(loopback)
        self._ser = None
        self._lock = threading.Lock()
        self.sequence = 1
        self._rx_thread = None
        self._rx_stop = False
        self._parser = Parser()
        self._telem_cb = None
        self.last_telem = {}
        self.last_error = {}

    @staticmethod
    def list_ports():
        if list_ports is None:
            return [("loopback", "Loopback (pyserial not installed)", "")] 
        ports = [(p.device, f"{p.device} {p.description}", p.hwid) for p in list_ports.comports()]
        if not ports:
            ports.append(("loopback", "Loopback (no ports detected)", ""))
        return ports

    def open(self, port: str, baud: int):
        if self.loopback or port == "loopback":
            self.loopback = True
            return True
        if serial is None:
            raise RuntimeError("pyserial not available. Enable Loopback or install pyserial in Blender Python.")
        self._ser = serial.Serial(port=port, baudrate=baud, timeout=0)
        self._start_reader()
        return True

    def is_open(self) -> bool:
        if self.loopback:
            return True
        if self._ser is None:
            return False
        return self._check_port()

    def _check_port(self) -> bool:
        if self._ser is None:
            return False
        try:
            if not bool(getattr(self._ser, "is_open", True)):
                return False
            port = getattr(self._ser, "port", None)
            if isinstance(port, str) and port and not os.path.exists(port):
                return False
            # Accessing properties triggers exceptions on disconnect in pyserial
            _ = self._ser.in_waiting
            _ = self._ser.out_waiting
            return True
        except Exception:
            return False

    def close(self):
        with self._lock:
            if self._ser:
                try:
                    self._ser.close()
                except Exception:
                    pass
                self._ser = None
        self._stop_reader()

    def send_setpoints(self, timestamp_us: int, items: list):
        frame = pack_setpoints(self.sequence, timestamp_us, items)
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        with self._lock:
            if self.loopback:
                hdr = f"[LOOPBACK] SETPOINTS {len(items)} @ {timestamp_us} us"
                if items:
                    fr = items[0].get('frame') if isinstance(items[0], dict) else None
                    frf = items[0].get('frame_f') if isinstance(items[0], dict) else None
                    if fr is not None:
                        if frf is not None:
                            hdr += f" frame={int(fr)} ({float(frf):.3f})"
                        else:
                            hdr += f" frame={int(fr)}"
                print(hdr)
                for it in items:
                    try:
                        mid = int(it.get('motor_id', 0))
                        pos = float(it.get('pos', 0.0))
                        vel = float(it.get('vel', 0.0))
                        acc = float(it.get('acc', 0.0))
                        kp = float(it.get('kp', 0.0))
                        kd = float(it.get('kd', 0.0))
                        tff = float(it.get('t_ff', 0.0))
                        flags = int(it.get('flags', 0))
                        # Print with higher precision to avoid rounding to zero visually
                        print(f"  id={mid} pos={pos:.9f} vel={vel:.9f} acc={acc:.9f} kp={kp:.3f} kd={kd:.3f} t_ff={tff:.3f} flags=0x{flags:04X}")
                    except Exception:
                        print(f"  item={it}")
                return True
            if self._ser and self._ser.writable():
                self._ser.write(frame)
                return True
        return False

    def send_command(self, cmd: int, motor_id: int = 0, timestamp_us: int = 0):
        frame = pack_command(self.sequence, cmd, motor_id, timestamp_us)
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        with self._lock:
            if self.loopback:
                cmd_names = {1: 'enable', 2: 'disable', 3: 'stop', 4: 'zero', 5: 'ping', 6: 'home', 7: 'calibrate'}
                cname = cmd_names.get(int(cmd), 'unknown')
                print(f"[LOOPBACK] COMMAND {cmd} ({cname}) motor {motor_id}")
                return True
            if self._ser and self._ser.writable():
                self._ser.write(frame)
                return True
        return False

    def set_telem_cb(self, cb):
        self._telem_cb = cb

    def _start_reader(self):
        if self._rx_thread is not None:
            return
        self._rx_stop = False
        self._rx_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._rx_thread.start()

    def _stop_reader(self):
        if self._rx_thread is None:
            return
        self._rx_stop = True
        self._rx_thread.join(timeout=0.2)
        self._rx_thread = None

    def _reader_loop(self):
        while not self._rx_stop:
            try:
                with self._lock:
                    data = self._ser.read(512) if self._ser else b""
                if not data:
                    if self._ser and not self.is_open():
                        with self._lock:
                            self._ser = None
                        break
                    time.sleep(0.01)
                    continue
                frames = self._parser.feed(data)
                for fr in frames:
                    if fr.get('type') == MSG_TELEMETRY:
                        for it in fr.get('items', []):
                            self.last_telem[it['motor_id']] = it
                            if self._telem_cb:
                                try:
                                    self._telem_cb(it)
                                except Exception:
                                    pass
                    elif fr.get('type') == MSG_ERROR:
                        for it in fr.get('items', []):
                            mid = it.get('motor_id')
                            code = int(it.get('error_code', 0))
                            if code == 0:
                                if mid in self.last_error:
                                    del self.last_error[mid]
                            else:
                                self.last_error[mid] = it
            except Exception:
                with self._lock:
                    self._ser = None
                time.sleep(0.05)
