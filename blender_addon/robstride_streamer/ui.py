import bpy
import time
from math import pi

from .serial_link import SerialLink
from . import fcurve_sampling
from . import protocol
from .telemetry_view import format_status_bar


def list_serial_ports_items(self, context):
    ports = SerialLink.list_ports()
    items = []
    for dev, label, hwid in ports:
        desc = f"{label} {hwid}".strip()
        items.append((dev, label, desc))
    return items


class MotorSlot(bpy.types.PropertyGroup):
    enabled: bpy.props.BoolProperty(name="Enable", description="Include this motor in streaming and command operations", default=False)
    motor_id: bpy.props.IntProperty(name="ID", description="RobStride motor CAN ID (1..32). Command ID is 0x200 + ID", default=1, min=1, max=32)
    object_ref: bpy.props.PointerProperty(name="Object", description="Object to sample animation from for this motor", type=bpy.types.Object)
    channel: bpy.props.EnumProperty(
        name="Channel",
        description="Animated channel to sample from the selected object",
        items=[
            ('LOC_X', 'Loc X', ''),
            ('LOC_Y', 'Loc Y', ''),
            ('LOC_Z', 'Loc Z', ''),
            ('ROT_X', 'Rot X', ''),
            ('ROT_Y', 'Rot Y', ''),
            ('ROT_Z', 'Rot Z', ''),
        ],
        default='ROT_Z',
    )
    unit_scale: bpy.props.FloatProperty(name="Scale", description="Multiply sampled values by this factor (radians for rotation when enabled below)", default=1.0)
    kp: bpy.props.FloatProperty(name="Kp", description="Position gain used in MIT command for this motor", default=30.0)
    kd: bpy.props.FloatProperty(name="Kd", description="Velocity gain used in MIT command for this motor", default=0.5)
    t_ff: bpy.props.FloatProperty(name="T_ff", description="Feedforward torque field in MIT command (units per motor firmware)", default=0.0)


class RobStrideProps(bpy.types.PropertyGroup):
    serial_port: bpy.props.EnumProperty(name="Port", description="Serial device used to communicate with ESP32-C6", items=list_serial_ports_items)
    baudrate: bpy.props.IntProperty(name="Baud", description="Serial baud rate. Firmware default is 921600", default=921600, min=9600, max=5000000)
    stream_rate: bpy.props.IntProperty(name="Stream Hz", description="Outgoing sample rate from Blender. Default 200 Hz", default=200, min=10, max=1000)
    buffer_ahead_ms: bpy.props.IntProperty(name="Buffer ms", description="Keep this much future trajectory queued on ESP32", default=500, min=100, max=2000)
    radians_for_rotation: bpy.props.BoolProperty(name="Radians for rotation", description="Interpret rotation channels as radians before scaling", default=True)
    connected: bpy.props.BoolProperty(name="Connected", description="Shows whether the serial link is open", default=False)
    streaming: bpy.props.BoolProperty(name="Streaming", description="Shows whether streaming timer is active", default=False)
    loopback: bpy.props.BoolProperty(name="Loopback", description="Offline mode. Print packets instead of sending", default=False)
    motors: bpy.props.CollectionProperty(type=MotorSlot)
    motors_index: bpy.props.IntProperty(name="Index", default=0)


_serial = None
_timer_running = False
_publish_horizon_us = 0
_traj_t0_us = None
_scene_t0_s = 0.0
_last_samples = {}


def _get_serial(context):
    global _serial
    props = context.scene.robstride
    if _serial is None:
        _serial = SerialLink(loopback=props.loopback)
    return _serial



class ROBSTRIDE_OT_connect(bpy.types.Operator):
    bl_idname = "robstride.connect"
    bl_label = "Connect"
    bl_description = "Open serial port at selected baud and start background reader"

    def execute(self, context):
        props = context.scene.robstride
        ser = _get_serial(context)
        try:
            # Update loopback mode from UI
            ser.loopback = bool(props.loopback)
            port = props.serial_port
            if ser.loopback:
                port = "loopback"
            ser.open(port, props.baudrate)
            props.connected = True
            if ser.loopback:
                print("[RobStride] Loopback mode enabled. No serial port opened.")
            return {'FINISHED'}
        except Exception as e:
            self.report({'ERROR'}, f"Connect failed: {e}")
            return {'CANCELLED'}


class ROBSTRIDE_OT_send_enable(bpy.types.Operator):
    bl_idname = "robstride.send_enable"
    bl_label = "Send Enable"
    bl_description = "Send RobStride enable (8x 0xFF) to each enabled motor"

    def execute(self, context):
        props = context.scene.robstride
        ser = _get_serial(context)
        # Enable all enabled motors
        for m in props.motors:
            if m.enabled:
                ser.send_command(1, m.motor_id)
        return {'FINISHED'}


class ROBSTRIDE_OT_send_disable(bpy.types.Operator):
    bl_idname = "robstride.send_disable"
    bl_label = "Send Disable"
    bl_description = "Enter safe stop for each enabled motor (disable semantics may vary)"

    def execute(self, context):
        props = context.scene.robstride
        ser = _get_serial(context)
        for m in props.motors:
            if m.enabled:
                ser.send_command(2, m.motor_id)
        return {'FINISHED'}


class ROBSTRIDE_OT_zero_offset(bpy.types.Operator):
    bl_idname = "robstride.zero_offset"
    bl_label = "Zero Offset"
    bl_description = "Software zero: set current reference position as offset for enabled motors"

    def execute(self, context):
        props = context.scene.robstride
        ser = _get_serial(context)
        for m in props.motors:
            if m.enabled:
                ser.send_command(4, m.motor_id)
        return {'FINISHED'}


class ROBSTRIDE_OT_home(bpy.types.Operator):
    bl_idname = "robstride.home"
    bl_label = "Home"
    bl_description = "Home at current position on firmware (software home)"

    def execute(self, context):
        props = context.scene.robstride
        ser = _get_serial(context)
        for m in props.motors:
            if m.enabled:
                ser.send_command(6, m.motor_id)
        return {'FINISHED'}


class ROBSTRIDE_OT_calibrate(bpy.types.Operator):
    bl_idname = "robstride.calibrate"
    bl_label = "Calibrate"
    bl_description = "Run a bounded sine profile (~3 s) on firmware for enabled motors"

    def execute(self, context):
        props = context.scene.robstride
        ser = _get_serial(context)
        for m in props.motors:
            if m.enabled:
                ser.send_command(7, m.motor_id)
        return {'FINISHED'}


def _timer_step():
    global _timer_running, _publish_horizon_us, _traj_t0_us
    scn = bpy.context.scene
    props = scn.robstride
    if not props.streaming or not props.connected:
        _timer_running = False
        return None
    # Sync loopback state live so toggling the checkbox while streaming takes effect immediately
    ser = _get_serial(bpy.context)
    ser.loopback = bool(props.loopback)
    # Build list of active targets (motor, object)
    targets = []
    for m in props.motors:
        if not m.enabled:
            continue
        obj = m.object_ref or bpy.context.active_object
        if obj is not None:
            targets.append((m, obj))
    if not targets:
        return 1.0 / max(1, props.stream_rate)
    rate_hz = max(1, props.stream_rate)
    fps = scn.render.fps / scn.render.fps_base
    buffer_s = props.buffer_ahead_ms / 1000.0
    if _traj_t0_us is None:
        _traj_t0_us = int(time.time() * 1e6)
        _publish_horizon_us = 0
    now_us = int(time.time() * 1e6)
    # Align trajectory time with wall time for simplicity here
    traj_now_us = now_us - _traj_t0_us
    target_horizon_us = traj_now_us + int(buffer_s * 1e6)
    if _publish_horizon_us < traj_now_us:
        _publish_horizon_us = traj_now_us
    # Helper to wrap frame for logging
    def wrap_frame(frame_f_raw: float) -> float:
        try:
            f_start = float(getattr(scn, 'frame_start', 1))
            f_end = float(getattr(scn, 'frame_end', f_start))
            length = max(1.0, (f_end - f_start + 1.0))
            return f_start + ((frame_f_raw - f_start) % length)
        except Exception:
            return frame_f_raw

    # Detect playback state
    is_playing = False
    try:
        scr = bpy.context.screen
        is_playing = bool(getattr(scr, 'is_animation_playing', False))
    except Exception:
        is_playing = False

    # Fill until horizon
    while _publish_horizon_us + int(1e6 / rate_hz) <= target_horizon_us:
        # Scene time for this tick
        if is_playing:
            t_s = _scene_t0_s + (_publish_horizon_us / 1e6)
        else:
            sub = float(getattr(scn, 'frame_subframe', 0.0))
            t_s = (scn.frame_current + sub) / fps
        frame_f_raw = t_s * fps
        frame_f = wrap_frame(frame_f_raw)
        items = []
        for m, obj in targets:
            samples = fcurve_sampling.sample_stream_points(
                obj,
                m.channel,
                fps,
                t_s,
                horizon_s=1.0 / rate_hz,
                rate_hz=rate_hz,
                unit_scale=(m.unit_scale if not m.channel.startswith('ROT_') else (1.0 if props.radians_for_rotation else 180.0 / pi) * m.unit_scale),
                rotation_in_radians=True,
                scene=scn,
                use_scene_eval=True,
            )
            if not samples:
                continue
            sp = samples[0]
            # Fallback numeric derivative using last sent sample for this motor if curve-based vel ~ 0
            v = sp['vel']
            a = sp['acc']
            mid = m.motor_id
            t_us = int(_publish_horizon_us)
            last = _last_samples.get(mid)
            if last is not None:
                dt_s = max(1e-9, (t_us - last['t_us']) / 1e6)
                if abs(v) < 1e-9 and abs(sp['pos'] - last['pos']) > 0:
                    v_new = (sp['pos'] - last['pos']) / dt_s
                    a = (v_new - last.get('vel', 0.0)) / dt_s
                    v = v_new
            # If paused, freeze derivatives to zero
            if not is_playing:
                v = 0.0
                a = 0.0
            items.append({
                'motor_id': mid,
                'pos': sp['pos'],
                'vel': v,
                'acc': a,
                'kp': m.kp,
                'kd': m.kd,
                't_ff': m.t_ff,
                'flags': sp['flags'],
                'frame': int(frame_f),
                'frame_f': frame_f,
            })
        if items:
            ser.send_setpoints(int(_publish_horizon_us), items)
            # Update last sample cache per motor
            for it in items:
                _last_samples[it['motor_id']] = {'t_us': int(_publish_horizon_us), 'pos': it['pos'], 'vel': it['vel']}
        _publish_horizon_us += int(1e6 / rate_hz)
    return 0.002


class ROBSTRIDE_OT_start_stream(bpy.types.Operator):
    bl_idname = "robstride.start_stream"
    bl_label = "Start Streaming"
    bl_description = "Start timer-based streaming at the configured rate"

    def execute(self, context):
        global _timer_running, _traj_t0_us, _publish_horizon_us, _scene_t0_s
        props = context.scene.robstride
        props.streaming = True
        # Anchor trajectory to current scene frame time
        scn = bpy.context.scene
        fps = scn.render.fps / scn.render.fps_base
        _scene_t0_s = (scn.frame_current) / fps
        _traj_t0_us = None
        _publish_horizon_us = 0
        ser = _get_serial(context)
        # Sync loopback mode with current UI state
        ser.loopback = bool(props.loopback)
        if not _timer_running:
            _timer_running = True
            bpy.app.timers.register(_timer_step, first_interval=0.01)
        if ser.loopback:
            print("[RobStride] Starting streaming in loopback mode.")
        else:
            if not ser.is_open():
                print("[RobStride] Serial not open. Click Connect to send to ESP32.")
        return {'FINISHED'}


class ROBSTRIDE_OT_stop_stream(bpy.types.Operator):
    bl_idname = "robstride.stop_stream"
    bl_label = "Stop Streaming"
    bl_description = "Stop streaming and keep current hold"

    def execute(self, context):
        global _timer_running
        props = context.scene.robstride
        props.streaming = False
        _timer_running = False
        return {'FINISHED'}


class ROBSTRIDE_PT_panel(bpy.types.Panel):
    bl_label = "RobStride"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'RobStride'

    def draw(self, context):
        layout = self.layout
        props = context.scene.robstride
        row = layout.row()
        row.prop(props, "loopback")
        layout.prop(props, "serial_port", text="Port")
        layout.prop(props, "baudrate")
        layout.prop(props, "stream_rate")
        layout.prop(props, "buffer_ahead_ms")
        layout.prop(props, "radians_for_rotation")
        box = layout.box()
        box.label(text="Motors")
        col = box.column()
        if len(props.motors) == 0:
            col.label(text="No motor slots configured")
            col.operator("robstride.seed_motors", text="Seed Defaults")
        else:
            pass
        for i, m in enumerate(props.motors):
            row = col.row(align=True)
            row.prop(m, "enabled", text="")
            row.prop(m, "motor_id")
            row.prop(m, "object_ref", text="Obj")
            row.prop(m, "channel")
            row.prop(m, "unit_scale")
            row.prop(m, "kp")
            row.prop(m, "kd")
            row.prop(m, "t_ff")
        row = box.row()
        row.operator("robstride.add_motor")
        row.operator("robstride.remove_motor")
        row.operator("robstride.seed_motors", text="Seed Defaults")
        row = layout.row()
        row.operator("robstride.connect", text="Connect" if not props.connected else "Reconnect")
        row = layout.row()
        row.operator("robstride.send_enable")
        row.operator("robstride.send_disable")
        row.operator("robstride.zero_offset")
        row = layout.row()
        row.operator("robstride.home")
        row.operator("robstride.calibrate")
        row = layout.row()
        row.operator("robstride.start_stream")
        row.operator("robstride.stop_stream")
        # Telemetry summary
        ser = _get_serial(context)
        if ser and ser.last_telem:
            tbox = layout.box()
            tbox.label(text="Telemetry")
            for mid, it in ser.last_telem.items():
                tbox.label(text=f"ID {mid} rx_ok {it.get('rx_count',0)} last_can 0x{it.get('last_can_id',0):X} status {it.get('status_flags',0)}")


class ROBSTRIDE_OT_add_motor(bpy.types.Operator):
    bl_idname = "robstride.add_motor"
    bl_label = "Add Motor"
    bl_description = "Add a new motor row (up to 6)"
    def execute(self, context):
        props = context.scene.robstride
        if len(props.motors) < 6:
            item = props.motors.add()
            item.enabled = True
            item.motor_id = len(props.motors)
        return {'FINISHED'}


class ROBSTRIDE_OT_remove_motor(bpy.types.Operator):
    bl_idname = "robstride.remove_motor"
    bl_label = "Remove Motor"
    bl_description = "Remove the last motor row"
    def execute(self, context):
        props = context.scene.robstride
        if len(props.motors) > 0:
            props.motors.remove(len(props.motors) - 1)
        return {'FINISHED'}


def _seed_default_motors(props):
    # Seed 3 rotational axes with IDs 1..3, enable the first only
    if len(props.motors) > 0:
        return
    defaults = [
        {"id": 1, "channel": 'ROT_Z', "enabled": True},
        {"id": 2, "channel": 'ROT_Y', "enabled": False},
        {"id": 3, "channel": 'ROT_X', "enabled": False},
    ]
    for d in defaults:
        m = props.motors.add()
        m.enabled = d["enabled"]
        m.motor_id = d["id"]
        m.channel = d["channel"]
        m.object_ref = bpy.context.active_object
        m.unit_scale = 1.0
        m.kp = 30.0
        m.kd = 0.5
        m.t_ff = 0.0


class ROBSTRIDE_OT_seed_motors(bpy.types.Operator):
    bl_idname = "robstride.seed_motors"
    bl_label = "Seed Motors"
    bl_description = "Overwrite current list with default motor slots (IDs 1..3 on Rot Z/Y/X)"

    def execute(self, context):
        props = context.scene.robstride
        # Clear existing
        while len(props.motors) > 0:
            props.motors.remove(len(props.motors) - 1)
        _seed_default_motors(props)
        return {'FINISHED'}


classes = (
    MotorSlot,
    RobStrideProps,
    ROBSTRIDE_OT_connect,
    ROBSTRIDE_OT_send_enable,
    ROBSTRIDE_OT_send_disable,
    ROBSTRIDE_OT_zero_offset,
    ROBSTRIDE_OT_home,
    ROBSTRIDE_OT_calibrate,
    ROBSTRIDE_OT_add_motor,
    ROBSTRIDE_OT_remove_motor,
    ROBSTRIDE_OT_seed_motors,
    ROBSTRIDE_OT_start_stream,
    ROBSTRIDE_OT_stop_stream,
    ROBSTRIDE_PT_panel,
)


def register_props():
    bpy.types.Scene.robstride = bpy.props.PointerProperty(type=RobStrideProps)

    # Status bar overlay
    try:
        bpy.types.STATUSBAR_HT_header.append(draw_statusbar)
    except Exception:
        pass


def unregister_props():
    if hasattr(bpy.types.Scene, 'robstride'):
        del bpy.types.Scene.robstride
    try:
        bpy.types.STATUSBAR_HT_header.remove(draw_statusbar)
    except Exception:
        pass


def draw_statusbar(self, context):
    scene = getattr(context, 'scene', None)
    if not scene:
        return
    props = getattr(scene, 'robstride', None)
    if not props:
        return
    ser = None
    try:
        ser = _get_serial(context)
    except Exception:
        return
    if not ser or not ser.last_telem:
        return
    row = self.layout.row()
    row.label(text=format_status_bar(ser.last_telem))
