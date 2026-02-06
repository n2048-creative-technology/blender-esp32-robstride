ESP32 Single-Axis Blender Add-on
================================

This add-on streams the X, Y, or Z location of a chosen Blender object to an ESP32 over a serial port using the firmware’s binary setpoint protocol.

Features
- Location only: stream object location
- Select target object and axis (X/Y/Z)
- World or Local space
- Adjustable scale factor, decimal precision, and range clamp
- 
- Connect/Disconnect serial, Send Once, and Start/Stop streaming
- Update rate control (0.01–2s)
- Dropdown lists only `/dev/ttyUSB*` and `/dev/ttyACM*` devices (pyserial required)
- Location values are always sent in millimeters
- Control buttons: Enable, Stop, Home (SW), Calibrate (sent as broadcast)

Installation
1. Ensure Blender 3.0+.
2. Install pyserial into Blender’s Python:
   - Windows (from Blender’s Python): `blender_python -m pip install pyserial`
   - macOS/Linux: launch Blender’s Python and run `pip install pyserial`
3. Zip the folder `addons/blender_esp32_single_axis` as `blender_esp32_single_axis.zip` (the zip must contain the `__init__.py` at its root).
4. In Blender: Edit → Preferences → Add-ons → Install… → choose the zip → enable the add-on.

Usage
1. Open 3D View → Sidebar (N) → ESP32 tab → “ESP32 Single Axis”.
2. Target: pick the object to track. Choose axis and space (World/Local).
3. Serial: select the port from the dropdown (e.g., `/dev/ttyACM0`) and set baud (default 921600). Click Connect.
4. Controls: use Enable/Stop/Home/Calibrate as needed (commands are broadcast; firmware uses its hardcoded motor ID).
5. Streaming: the add-on sends binary setpoints with ~500 ms look-ahead (margin over the 100 ms minimum). Default update is 200 Hz (0.005 s). Use Send Once or Start Streaming to push setpoints.

Protocol
- Uses binary frames (0xA5 0x5A header, CRC16-CCITT) matching the firmware’s SerialProtocol. The user does not configure message format.

Notes
- Units: Location is always sent in millimeters. Additional scaling is applied via the Scale field.
- Port list: Use Refresh if a new device was plugged in after Blender started. If the dropdown is missing, install `pyserial` in Blender’s Python.
- If pyserial is missing, the panel will show “PySerial: Missing”. Install pyserial and re-enable the add-on.

Troubleshooting
- Permissions on Linux: you may need to add your user to the `dialout` group or adjust udev permissions.
- macOS security: allow access to USB serial devices when prompted.
- Port busy: ensure no other program is using the port.

Protocol Compatibility
Firmware expects binary frames (0xA5 0x5A header, payload, CRC16-CCITT). The add-on generates MSG_SETPOINTS with timestamps in microseconds and positions in millimeters, plus a small velocity estimate. This matches the parser in `single-axis-firmware/src/protocol_serial.cpp` and is not user-configurable.

Firmware integration notes
- Homing gate: setpoints are ignored until homing calibration computes `rad_per_mm` and `homing_zero_pos_rad`. If you don’t have limit switches, you can enable one of these in `include/config.h`:
  - `#define ALLOW_SETPOINTS_WITHOUT_HOMING 1` (accept setpoints before homing)
  - `#define STATIC_CALIB_ENABLE 1`, set `STATIC_RAD_PER_MM` and `STATIC_ZERO_POS_RAD` (bypass homing)
- Buffering: firmware requires ≥100 ms buffer. Add-on sends with ~150 ms look-ahead.
