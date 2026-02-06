bl_info = {
    "name": "ESP32 Single-Axis Sender",
    "author": "Mauricio van der Maesen de Sombreff",
    "version": (0, 1, 0),
    "blender": (3, 0, 0),
    "location": "View3D > Sidebar > ESP32",
    "description": "Send X/Y/Z location of an object over serial to ESP32 single-axis firmware.",
    "warning": "Requires pyserial",
    "category": "3D View",
}

import bpy
import struct
import time
import sys
from bpy.props import (
    BoolProperty,
    EnumProperty,
    FloatProperty,
    PointerProperty,
    StringProperty,
    IntProperty,
)

# Optional: try to import pyserial and list_ports
_serial_available = True
try:
    import serial  # type: ignore
    from serial.tools import list_ports  # type: ignore
except Exception:
    _serial_available = False
    serial = None
    list_ports = None


# Items callback for dropdown listing serial ports
def _port_items(self, context):
    items = []
    if _serial_available and list_ports is not None:
        try:
            plat = sys.platform
            all_ports = list(list_ports.comports())
            # First pass: platform-specific preferred matches
            for p in all_ports:
                dev = getattr(p, 'device', '') or ''
                ok = False
                if plat.startswith('linux'):
                    ok = dev.startswith('/dev/ttyUSB') or dev.startswith('/dev/ttyACM')
                elif plat == 'darwin':
                    ok = dev.startswith('/dev/tty.usb') or dev.startswith('/dev/cu.usb')
                elif plat.startswith('win'):
                    ok = dev.upper().startswith('COM')
                if ok:
                    label = f"{dev} ({p.description})" if getattr(p, 'description', None) else dev
                    items.append((dev, label, dev))
            # Fallback: if nothing matched, list everything we found
            if not items:
                for p in all_ports:
                    dev = getattr(p, 'device', '') or ''
                    if not dev:
                        continue
                    label = f"{dev} ({p.description})" if getattr(p, 'description', None) else dev
                    items.append((dev, label, dev))
        except Exception:
            pass
    if not items:
        # Fallback: show current stored string to keep Enum valid
        scene = getattr(context, "scene", None)
        s = getattr(scene, "esp32_sa", None) if scene else None
        current = s.port if s and hasattr(s, 'port') else ""
        items = [(current, current or "<no ports>", current)]
    return items


def _port_enum_update(self, context):
    # Sync dropdown selection to the string property used for connecting
    try:
        self.port = self.port_enum
    except Exception:
        pass


def _on_update_rate(self, context):
    # Mark timer for reconfiguration; modal loop will pick this up
    try:
        self.update_rate_dirty = True
    except Exception:
        pass


class ESP32SA_AddonPreferences(bpy.types.AddonPreferences):
    bl_idname = __name__

    default_baud: IntProperty(name="Default Baud", default=921600, min=1)

    def draw(self, context):
        layout = self.layout
        col = layout.column()
        col.label(text=f"PySerial available: {'Yes' if _serial_available else 'No'}")
        col.prop(self, "default_baud")
        if not _serial_available:
            col.label(text="Install pyserial in Blender's Python:")
            col.label(text="Windows: blender_python -m pip install pyserial")
            col.label(text="macOS/Linux: ./blender --python-expr 'import pip,sys; pip.main([\"install\",\"pyserial\"])'")


class ESP32SA_SceneSettings(bpy.types.PropertyGroup):
    target_object: PointerProperty(
        name="Object",
        type=bpy.types.Object,
        description="Object to track and send its location",
    )

    axis: EnumProperty(
        name="Axis",
        items=[
            ("X", "X", "Use X location"),
            ("Y", "Y", "Use Y location"),
            ("Z", "Z", "Use Z location"),
        ],
        default="X",
    )

    space: EnumProperty(
        name="Space",
        items=[
            ("WORLD", "World", "World space location"),
            ("LOCAL", "Local", "Local space location"),
        ],
        default="WORLD",
    )

    # Rotation/Scale options removed: location-only add-on

    port: StringProperty(
        name="Serial Port",
        description="Serial port for ESP32 (e.g. COM3 or /dev/ttyUSB0)",
        default="",
    )

    # Dropdown for selecting port; updates the string above
    port_enum: EnumProperty(
        name="Port",
        description="Select serial interface",
        items=_port_items,
        update=_port_enum_update,
    )

    baud: IntProperty(name="Baud", default=921600, min=1)

    scale: FloatProperty(
        name="Scale",
        description="Multiply value before sending (unit conversion)",
        default=1.0,
    )

    range_mm: FloatProperty(
        name="Range (mm)",
        description="Total motion range in millimeters (centered at 0)",
        default=2000.0,
        min=1.0,
        soft_max=10000.0,
    )

    decimals: IntProperty(name="Decimals", default=3, min=0, max=8)

    streaming: BoolProperty(name="Streaming", default=False)

    last_value: FloatProperty(name="Last Value", default=0.0)

    update_rate: FloatProperty(
        name="Update (s)",
        description="Streaming timer interval in seconds",
        default=0.005,  # 200 Hz
        min=0.001,
        max=0.5,
        update=_on_update_rate,
    )

    buffer_ahead_ms: IntProperty(
        name="Buffer (ms)",
        description="Keep this much future trajectory queued on ESP32 (min 100 ms)",
        default=200,
        min=100,
        max=2000,
    )

    update_rate_dirty: BoolProperty(
        name="Update dirty",
        description="Internal flag to rebuild timer on rate change",
        default=False,
        options={'HIDDEN'},
    )
    # Auto-select removed; users select a port via dropdown


    # Telemetry/status
    rx_count: IntProperty(name="RX Frames", default=0, min=0)
    can_rx_flags: IntProperty(name="CAN RX Flags", default=0, min=0)
    last_can_id: IntProperty(name="Last CAN ID", default=0, min=0)
    status_flags: IntProperty(name="Status Flags", default=0, min=0)
    last_error_code: IntProperty(name="Last Error Code", default=0)
    last_error_msg: StringProperty(name="Last Error", default="")

    auto_stream_on_enable: BoolProperty(
        name="Auto stream on Enable",
        description="Start streaming automatically after sending Enable",
        default=True,
    )


# Simple serial manager kept at module level
class _SerialManager:
    def __init__(self):
        self.ser = None
        self.port = None
        self.baud = None

    def is_open(self):
        return self.ser is not None and getattr(self.ser, "is_open", False)

    def open(self, port: str, baud: int):
        if not _serial_available:
            raise RuntimeError("pyserial not available")
        if self.is_open():
            if self.port == port and self.baud == baud:
                return
            self.close()
        self.ser = serial.Serial(port=port, baudrate=baud, timeout=0)
        self.port = port
        self.baud = baud

    def close(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
            finally:
                self.ser = None
                self.port = None
                self.baud = None

    def write_line(self, data: str):
        if not self.is_open():
            raise RuntimeError("Serial not open")
        self.ser.write(data.encode("utf-8"))

    def write_bytes(self, data: bytes):
        if not self.is_open():
            raise RuntimeError("Serial not open")
        self.ser.write(data)


_SERIAL = _SerialManager()

# ---- Binary protocol (matches firmware SerialProtocol) ----
MSG_SETPOINTS = 1
MSG_COMMAND = 2
MSG_TELEMETRY = 3
MSG_ERROR = 4

_seq = 0
_t0 = None
_stream_state = {
    "t0_us": None,
    "publish_horizon_us": 0,
    "last_mm": None,
    "last_us": None,
    "last_vel": 0.0,
    "sync_sent": False,
}


def _now_us():
    global _t0
    t = time.perf_counter()
    if _t0 is None:
        _t0 = t
        return 0
    return int((t - _t0) * 1_000_000)


def _crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc & 0xFFFF


class _TelemParser:
    def __init__(self):
        self.buf = bytearray()

    def feed(self):
        if not _SERIAL.is_open():
            return
        try:
            data = _SERIAL.ser.read(1024)
        except Exception:
            data = b""
        if data:
            self.buf.extend(data)

    def parse(self):
        out = []
        while True:
            idx = self.buf.find(b"\xA5\x5A")
            if idx < 0:
                if len(self.buf) > 2048:
                    del self.buf[:-4]
                break
            if idx > 0:
                del self.buf[:idx]
            if len(self.buf) < 2 + 11:
                break
            pay_fixed = self.buf[2:2+11]
            typ = pay_fixed[1]
            if typ == MSG_TELEMETRY:
                payload_len = 23
            elif typ == MSG_ERROR:
                payload_len = 13
            else:
                del self.buf[0]
                continue
            total = 2 + payload_len + 2
            if len(self.buf) < total:
                break
            frame = bytes(self.buf[:total])
            pay = frame[2:2+payload_len]
            crc_rx = int.from_bytes(frame[2+payload_len:2+payload_len+2], 'little')
            if _crc16_ccitt(pay) != crc_rx:
                del self.buf[0]
                continue
            if typ == MSG_TELEMETRY:
                rx_count = int.from_bytes(pay[11:15], 'little')
                can_rx_flags = int.from_bytes(pay[15:17], 'little')
                last_can_id = int.from_bytes(pay[17:21], 'little')
                status_flags = int.from_bytes(pay[21:23], 'little')
                out.append({
                    'type': 'telem',
                    'rx_count': rx_count,
                    'can_rx_flags': can_rx_flags,
                    'last_can_id': last_can_id,
                    'status_flags': status_flags,
                })
            elif typ == MSG_ERROR:
                error_code = int.from_bytes(pay[11:13], 'little')
                out.append({'type': 'error', 'error_code': error_code})
            del self.buf[:total]
        return out


_PARSER = _TelemParser()


def _build_setpoints_frame(ts_us: int, items: list) -> bytes:
    # payload: version(1)=1, type(1)=1, seq(4), ts_us(4), count(1), items...
    global _seq
    version = 1
    typ = MSG_SETPOINTS
    seq = _seq & 0xFFFFFFFF
    _seq += 1
    count = len(items)
    payload = bytearray()
    payload += struct.pack('<BB', version, typ)
    payload += struct.pack('<I', seq)
    payload += struct.pack('<I', ts_us & 0xFFFFFFFF)
    payload += struct.pack('<B', count)
    for it in items:
        pos = float(it.get('pos', 0.0))
        vel = float(it.get('vel', 0.0))
        acc = float(it.get('acc', 0.0))
        kp = float(it.get('kp', 0.0))
        kd = float(it.get('kd', 0.0))
        t_ff = float(it.get('t_ff', 0.0))
        flags = int(it.get('flags', 0)) & 0xFFFF
        payload += struct.pack('<ffffffH', pos, vel, acc, kp, kd, t_ff, flags)
    crc = _crc16_ccitt(payload)
    frame = bytearray(b"\xA5\x5A")
    frame += payload
    frame += struct.pack('<H', crc)
    return bytes(frame)


def _send_setpoint_mm(value_mm: float, vel_mm_s: float, ts_us: int, range_mm: float):
    half_range = max(0.5, range_mm * 0.5)
    if value_mm < -half_range:
        value_mm = -half_range
    elif value_mm > half_range:
        value_mm = half_range
    item = {
        'pos': value_mm,
        'vel': vel_mm_s,
        'acc': 0.0,
        'kp': 0.0,
        'kd': 0.0,
        't_ff': 0.0,
        'flags': 0,
    }
    frame = _build_setpoints_frame(ts_us, [item])
    _SERIAL.write_bytes(frame)
    return value_mm


def _build_command_frame(cmd: int, motor_id: int) -> bytes:
    global _seq
    version = 1
    typ = MSG_COMMAND
    seq = _seq & 0xFFFFFFFF
    _seq += 1
    ts_us = _now_us() & 0xFFFFFFFF
    count = 1
    payload = bytearray()
    payload += struct.pack('<BB', version, typ)
    payload += struct.pack('<I', seq)
    payload += struct.pack('<I', ts_us)
    payload += struct.pack('<B', count)
    payload += struct.pack('<B', cmd & 0xFF)
    crc = _crc16_ccitt(payload)
    frame = bytearray(b"\xA5\x5A")
    frame += payload
    frame += struct.pack('<H', crc)
    return bytes(frame)


def _send_command(cmd: int, motor_id: int):
    frame = _build_command_frame(cmd, motor_id)
    _SERIAL.write_bytes(frame)

def _ensure_connected(scene_settings, report=None):
    if _SERIAL.is_open():
        return True
    port = scene_settings.port
    if not port:
        if report:
            report({'ERROR'}, "No serial port selected")
        return False
    try:
        _SERIAL.open(port, scene_settings.baud)
        if report:
            report({'INFO'}, f"Connected to {port} @ {scene_settings.baud}")
        return True
    except Exception as e:
        if report:
            report({'ERROR'}, f"Failed to open {port}: {e}")
        return False


def _get_value(scene_settings: ESP32SA_SceneSettings):
    obj = scene_settings.target_object
    if obj is None:
        return None
    axis_index = {"X": 0, "Y": 1, "Z": 2}[scene_settings.axis]
    if scene_settings.space == "WORLD":
        v = obj.matrix_world.translation
    else:
        v = obj.location
    return float(v[axis_index])


# No ASCII message formatting; protocol uses binary frames.


class ESP32SA_OT_Connect(bpy.types.Operator):
    bl_idname = "esp32_sa.connect"
    bl_label = "Connect"
    bl_description = "Open serial connection"

    def execute(self, context):
        s = context.scene.esp32_sa
        if not _serial_available:
            self.report({'ERROR'}, "pyserial not installed")
            return {'CANCELLED'}
        port = s.port
        if not port:
            self.report({'ERROR'}, "No serial port specified")
            return {'CANCELLED'}
        try:
            _SERIAL.open(port, s.baud)
        except Exception as e:
            self.report({'ERROR'}, f"Failed to open {port}: {e}")
            return {'CANCELLED'}
        self.report({'INFO'}, f"Connected to {port} @ {s.baud}")
        try:
            bpy.ops.esp32_sa.telemetry_monitor('INVOKE_DEFAULT')
        except Exception:
            pass
        return {'FINISHED'}


class ESP32SA_OT_Disconnect(bpy.types.Operator):
    bl_idname = "esp32_sa.disconnect"
    bl_label = "Disconnect"
    bl_description = "Close serial connection"

    def execute(self, context):
        _SERIAL.close()
        self.report({'INFO'}, "Disconnected")
        return {'FINISHED'}


class ESP32SA_OT_TelemetryMonitor(bpy.types.Operator):
    bl_idname = "esp32_sa.telemetry_monitor"
    bl_label = "Telemetry Monitor"
    bl_description = "Background telemetry/error monitor"

    _timer = None
    _prev_status_flags = None
    _prev_error_code = None

    def modal(self, context, event):
        if event.type == 'TIMER':
            if not _SERIAL.is_open():
                self.finish(context)
                return {'CANCELLED'}
            s = context.scene.esp32_sa
            try:
                _PARSER.feed()
                msgs = _PARSER.parse() or []
                for m in msgs:
                    if m['type'] == 'telem':
                        s.rx_count = m['rx_count']
                        s.can_rx_flags = m['can_rx_flags']
                        s.last_can_id = m['last_can_id']
                        s.status_flags = m['status_flags']
                        if self._prev_status_flags is None:
                            self._prev_status_flags = s.status_flags
                        elif s.status_flags != self._prev_status_flags:
                            # Compose a concise change message
                            flags = []
                            if s.status_flags & 0x1: flags.append('ESTOP')
                            if s.status_flags & 0x2: flags.append('CALIB')
                            if s.status_flags & 0x4: flags.append('WD')
                            if s.status_flags & 0x8: flags.append('UNDERRUN')
                            msg = 'Flags: ' + (' | '.join(flags) if flags else 'OK')
                            try:
                                self.report({'INFO'}, msg)
                            except Exception:
                                pass
                            self._prev_status_flags = s.status_flags
                    elif m['type'] == 'error':
                        s.last_error_code = m['error_code']
                        err_map = {1: 'Buffer underrun', 2: 'Watchdog timeout', 3: 'CAN TX failed', 4: 'Interpolator empty'}
                        s.last_error_msg = err_map.get(s.last_error_code, f"Error {s.last_error_code}")
                        if self._prev_error_code != s.last_error_code:
                            try:
                                self.report({'WARNING'}, f"{s.last_error_msg}")
                            except Exception:
                                pass
                            self._prev_error_code = s.last_error_code
            except Exception:
                pass
        return {'PASS_THROUGH'}

    def finish(self, context):
        wm = context.window_manager
        if self._timer is not None:
            wm.event_timer_remove(self._timer)
            self._timer = None

    def execute(self, context):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, window=context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


class ESP32SA_OT_CmdEnable(bpy.types.Operator):
    bl_idname = "esp32_sa.cmd_enable"
    bl_label = "Enable"
    bl_description = "Enable motor (RobStride)"

    def execute(self, context):
        s = context.scene.esp32_sa
        if not _ensure_connected(s, self.report):
            return {'CANCELLED'}
        try:
            _send_command(1, 0)
        except Exception as e:
            self.report({'ERROR'}, f"Enable failed: {e}")
            return {'CANCELLED'}
        self.report({'INFO'}, "Enable sent")
        # Optionally start streaming automatically
        if s.auto_stream_on_enable:
            if not s.target_object:
                try:
                    self.report({'INFO'}, "Select a target object to auto-start streaming")
                except Exception:
                    pass
                return {'FINISHED'}
            try:
                bpy.ops.esp32_sa.start_stream('INVOKE_DEFAULT')
            except Exception:
                pass
        return {'FINISHED'}


class ESP32SA_OT_CmdStop(bpy.types.Operator):
    bl_idname = "esp32_sa.cmd_stop"
    bl_label = "Stop"
    bl_description = "Stop/E-Stop motor"

    def execute(self, context):
        s = context.scene.esp32_sa
        if not _ensure_connected(s, self.report):
            return {'CANCELLED'}
        try:
            _send_command(3, 0)
        except Exception as e:
            self.report({'ERROR'}, f"Stop failed: {e}")
            return {'CANCELLED'}
        self.report({'INFO'}, "Stop sent")
        return {'FINISHED'}


class ESP32SA_OT_CmdHome(bpy.types.Operator):
    bl_idname = "esp32_sa.cmd_home"
    bl_label = "Home (SW)"
    bl_description = "Software home: set current position as zero"

    def execute(self, context):
        s = context.scene.esp32_sa
        if not _ensure_connected(s, self.report):
            return {'CANCELLED'}
        try:
            _send_command(6, 0)
        except Exception as e:
            self.report({'ERROR'}, f"Home failed: {e}")
            return {'CANCELLED'}
        self.report({'INFO'}, "Home sent")
        return {'FINISHED'}


class ESP32SA_OT_CmdCalib(bpy.types.Operator):
    bl_idname = "esp32_sa.cmd_calib"
    bl_label = "Calibrate"
    bl_description = "Start calibration waveform"

    def execute(self, context):
        s = context.scene.esp32_sa
        if not _ensure_connected(s, self.report):
            return {'CANCELLED'}
        try:
            _send_command(7, 0)
        except Exception as e:
            self.report({'ERROR'}, f"Calibrate failed: {e}")
            return {'CANCELLED'}
        self.report({'INFO'}, "Calibrate sent")
        return {'FINISHED'}


class ESP32SA_OT_SendOnce(bpy.types.Operator):
    bl_idname = "esp32_sa.send_once"
    bl_label = "Send Once"
    bl_description = "Send current value once"

    def execute(self, context):
        s = context.scene.esp32_sa
        if not _ensure_connected(s, self.report):
            return {'CANCELLED'}
        val = _get_value(s)
        if val is None:
            self.report({'ERROR'}, "No object selected")
            return {'CANCELLED'}
        # Convert Blender units to millimeters using unit scale (meters per unit)
        try:
            unit_scale = context.scene.unit_settings.scale_length
        except Exception:
            unit_scale = 1.0
        value_mm = float(val) * unit_scale * 1000.0 * s.scale
        try:
            ts_us = _now_us()
            value_mm = _send_setpoint_mm(value_mm, 0.0, ts_us, s.range_mm)
        except Exception as e:
            self.report({'ERROR'}, f"Send failed: {e}")
            return {'CANCELLED'}
        s.last_value = value_mm
        self._prev_mm = value_mm
        self._prev_dt = None
        self.report({'INFO'}, f"Setpoint @ {value_mm:.3f} mm queued")
        return {'FINISHED'}


class ESP32SA_OT_StartStreaming(bpy.types.Operator):
    bl_idname = "esp32_sa.start_stream"
    bl_label = "Start Streaming"
    bl_description = "Start sending value periodically when changed"

    _timer = None
    _last_sent = None

    def modal(self, context, event):
        if event.type == 'TIMER':
            s = context.scene.esp32_sa
            if not s.streaming:
                self.finish(context)
                return {'CANCELLED'}
            try:
                # Reconfigure timer if user changed update rate while streaming
                if getattr(s, 'update_rate_dirty', False):
                    wm = context.window_manager
                    if self._timer is not None:
                        wm.event_timer_remove(self._timer)
                        self._timer = None
                    interval = max(0.001, min(0.5, s.update_rate))
                    self._timer = wm.event_timer_add(interval, window=context.window)
                    s.update_rate_dirty = False
                # Poll telemetry/error frames
                _PARSER.feed()
                msgs = _PARSER.parse() or []
                for m in msgs:
                    if m['type'] == 'telem':
                        s.rx_count = m['rx_count']
                        s.can_rx_flags = m['can_rx_flags']
                        s.last_can_id = m['last_can_id']
                        s.status_flags = m['status_flags']
                    elif m['type'] == 'error':
                        s.last_error_code = m['error_code']
                        err_map = {1: 'Buffer underrun', 2: 'Watchdog timeout', 3: 'CAN TX failed', 4: 'Interpolator empty'}
                        s.last_error_msg = err_map.get(s.last_error_code, f"Error {s.last_error_code}")
                val = _get_value(s)
                if val is None:
                    return {'PASS_THROUGH'}
                try:
                    unit_scale = context.scene.unit_settings.scale_length
                except Exception:
                    unit_scale = 1.0
                value_mm = float(val) * unit_scale * 1000.0 * s.scale
                now_us_raw = _now_us()
                if _stream_state["t0_us"] is None:
                    _stream_state["t0_us"] = now_us_raw
                    _stream_state["publish_horizon_us"] = 0
                    _stream_state["last_mm"] = None
                    _stream_state["last_us"] = None
                    _stream_state["last_vel"] = 0.0
                    _stream_state["sync_sent"] = False
                traj_now_us = max(0, now_us_raw - _stream_state["t0_us"])

                vel_now = _stream_state["last_vel"]
                last_mm = _stream_state["last_mm"]
                last_us = _stream_state["last_us"]
                if last_mm is not None and last_us is not None:
                    dt = (now_us_raw - last_us) * 1e-6
                    if dt > 0:
                        vel_now = (value_mm - last_mm) / dt
                _stream_state["last_mm"] = value_mm
                _stream_state["last_us"] = now_us_raw
                _stream_state["last_vel"] = vel_now

                buffer_us = int(max(100, s.buffer_ahead_ms)) * 1000
                step_us = int(max(1000, s.update_rate * 1_000_000))
                target_horizon_us = traj_now_us + buffer_us
                if _stream_state["publish_horizon_us"] < traj_now_us:
                    _stream_state["publish_horizon_us"] = traj_now_us

                # Send a sync point once to align trajectory time
                if not _stream_state["sync_sent"]:
                    _send_setpoint_mm(value_mm, vel_now, int(traj_now_us), s.range_mm)
                    _stream_state["sync_sent"] = True

                # Fill future buffer up to target horizon
                rate_hz = max(1.0, 1.0 / max(0.001, s.update_rate))
                max_iters = max(4, int(rate_hz / 60.0 * 6.0))
                tick_start = time.perf_counter()
                iters = 0
                while _stream_state["publish_horizon_us"] + step_us <= target_horizon_us:
                    if iters >= max_iters:
                        break
                    if (time.perf_counter() - tick_start) > 0.003:
                        break
                    _stream_state["publish_horizon_us"] += step_us
                    ahead_s = (_stream_state["publish_horizon_us"] - traj_now_us) * 1e-6
                    if ahead_s < 0.0:
                        ahead_s = 0.0
                    pos_pred = value_mm + vel_now * ahead_s
                    _send_setpoint_mm(pos_pred, vel_now, int(_stream_state["publish_horizon_us"]), s.range_mm)
                    iters += 1

                s.last_value = value_mm
                self._last_sent = value_mm
            except Exception:
                # Avoid spamming reports; silently continue
                pass
        return {'PASS_THROUGH'}

    def finish(self, context):
        wm = context.window_manager
        if self._timer is not None:
            wm.event_timer_remove(self._timer)
            self._timer = None
        context.scene.esp32_sa.streaming = False
        _stream_state["t0_us"] = None
        _stream_state["publish_horizon_us"] = 0
        _stream_state["last_mm"] = None
        _stream_state["last_us"] = None
        _stream_state["last_vel"] = 0.0
        _stream_state["sync_sent"] = False

    def execute(self, context):
        s = context.scene.esp32_sa
        if s.streaming:
            self.report({'INFO'}, "Already streaming")
            return {'CANCELLED'}
        if not _ensure_connected(s, self.report):
            return {'CANCELLED'}
        s.streaming = True
        self._last_sent = None
        _stream_state["t0_us"] = None
        wm = context.window_manager
        interval = max(0.001, min(0.5, s.update_rate))
        self._timer = wm.event_timer_add(interval, window=context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


class ESP32SA_OT_StopStreaming(bpy.types.Operator):
    bl_idname = "esp32_sa.stop_stream"
    bl_label = "Stop Streaming"
    bl_description = "Stop sending values"

    def execute(self, context):
        # Find running modal operator by toggling the flag; modal will exit
        s = context.scene.esp32_sa
        s.streaming = False
        self.report({'INFO'}, "Streaming stopped")
        return {'FINISHED'}


class ESP32SA_OT_RefreshPorts(bpy.types.Operator):
    bl_idname = "esp32_sa.refresh_ports"
    bl_label = "Refresh"
    bl_description = "Refresh available serial ports"

    def execute(self, context):
        # No state to keep; UI draws dynamically
        self.report({'INFO'}, "Ports refreshed")
        return {'FINISHED'}


def _get_available_ports_items(self, context):
    # Deprecated in favor of _port_items; kept for compatibility if referenced elsewhere
    return _port_items(self, context)


class ESP32SA_PT_Panel(bpy.types.Panel):
    bl_label = "ESP32 Single Axis"
    bl_idname = "ESP32SA_PT_panel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'ESP32'

    def draw(self, context):
        layout = self.layout
        s = context.scene.esp32_sa

        box = layout.box()
        box.label(text="Target (Location)")
        box.prop(s, "target_object")
        row = box.row(align=True)
        row.prop(s, "axis", expand=True)
        box.prop(s, "space")
        row = box.row(align=True)
        row.prop(s, "scale")
        row.prop(s, "decimals")
        box.prop(s, "range_mm")

        box2 = layout.box()
        box2.label(text="Serial")
        row = box2.row(align=True)
        if hasattr(s, "port_enum"):
            # Dynamic drop-down of ports
            row.prop(s, "port_enum", text="Port")
        else:
            row.prop(s, "port", text="Port")
        row.operator(ESP32SA_OT_RefreshPorts.bl_idname, text="Refresh", icon='FILE_REFRESH')
        box2.prop(s, "baud")
        row = box2.row(align=True)
        row.operator(ESP32SA_OT_Connect.bl_idname, icon='PLUGIN')
        row.operator(ESP32SA_OT_Disconnect.bl_idname, icon='X')

        # Controls
        box_ctrl = layout.box()
        box_ctrl.label(text="Controls")
        row = box_ctrl.row(align=True)
        row.prop(s, "auto_stream_on_enable")
        row = box_ctrl.row(align=True)
        row.operator(ESP32SA_OT_CmdEnable.bl_idname, icon='PLAY')
        row.operator(ESP32SA_OT_CmdStop.bl_idname, icon='CANCEL')
        row = box_ctrl.row(align=True)
        row.operator(ESP32SA_OT_CmdHome.bl_idname, icon='HOME')
        row.operator(ESP32SA_OT_CmdCalib.bl_idname, icon='MOD_WAVE')

        box3 = layout.box()
        box3.label(text="Streaming")
        box3.prop(s, "update_rate")
        box3.prop(s, "buffer_ahead_ms")
        box3.label(text="Tip: Buffer >= 100 ms")

        box4 = layout.box()
        box4.label(text="Limits")
        min_active = bool(s.status_flags & 0x10)
        max_active = bool(s.status_flags & 0x20)
        box4.label(text=f"MIN: {'ACTIVE' if min_active else 'ok'}")
        box4.label(text=f"MAX: {'ACTIVE' if max_active else 'ok'}")
        box3.label(text="Tip: 0.005s = 200 Hz")
        row = box3.row(align=True)
        row.operator(ESP32SA_OT_SendOnce.bl_idname, icon='EXPORT')
        if s.streaming:
            row.operator(ESP32SA_OT_StopStreaming.bl_idname, icon='PAUSE')
        else:
            row.operator(ESP32SA_OT_StartStreaming.bl_idname, icon='PLAY')
        box3.label(text=f"Last value: {s.last_value:.{max(0, min(8, s.decimals))}f} mm")

        box4 = layout.box()
        box4.label(text="Status")
        box4.label(text=f"RX frames: {s.rx_count}")
        box4.label(text=f"CAN flags: 0x{s.can_rx_flags:04X}")
        box4.label(text=f"Last CAN ID: 0x{s.last_can_id:08X}")
        # Decode status flags bits: 1=estop, 2=calib_active, 4=wd, 8=underrun
        flags = []
        if s.status_flags & 0x1: flags.append('ESTOP')
        if s.status_flags & 0x2: flags.append('CALIB')
        if s.status_flags & 0x4: flags.append('WD')
        if s.status_flags & 0x8: flags.append('UNDERRUN')
        ok = (not flags) and (s.last_error_code == 0)
        row = box4.row()
        if ok:
            row.label(text=f"Flags: OK", icon='CHECKMARK')
        else:
            row.alert = True
            row.label(text=f"Flags: {' | '.join(flags)}", icon='ERROR')
        if s.last_error_code:
            box4.label(text=f"Last error: {s.last_error_code} ({s.last_error_msg})")
        layout.separator()
        layout.label(text=f"PySerial: {'OK' if _serial_available else 'Missing'}")


classes = (
    ESP32SA_AddonPreferences,
    ESP32SA_SceneSettings,
    ESP32SA_OT_Connect,
    ESP32SA_OT_Disconnect,
    ESP32SA_OT_TelemetryMonitor,
    ESP32SA_OT_CmdEnable,
    ESP32SA_OT_CmdStop,
    ESP32SA_OT_CmdHome,
    ESP32SA_OT_CmdCalib,
    ESP32SA_OT_SendOnce,
    ESP32SA_OT_StartStreaming,
    ESP32SA_OT_StopStreaming,
    ESP32SA_OT_RefreshPorts,
    ESP32SA_PT_Panel,
)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    bpy.types.Scene.esp32_sa = PointerProperty(type=ESP32SA_SceneSettings)

    # Avoid accessing bpy.context.scene here; registration can run with a restricted context.


def unregister():
    # Ensure serial closed
    try:
        _SERIAL.close()
    except Exception:
        pass
    # Remove properties and classes
    if hasattr(ESP32SA_SceneSettings, 'port_enum'):
        try:
            delattr(ESP32SA_SceneSettings, 'port_enum')
        except Exception:
            pass
    if hasattr(bpy.types.Scene, 'esp32_sa'):
        del bpy.types.Scene.esp32_sa
    for c in reversed(classes):
        try:
            bpy.utils.unregister_class(c)
        except Exception:
            pass
