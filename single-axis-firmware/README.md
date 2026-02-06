ESP32-S3 Firmware for RobStride Live Streamer (Single Axis)

Hardware
- Target: ESP32-S3-DevKitC-1
- External CAN transceiver required. ESP32-C6 has TWAI controller only.
- Wire ESP32-C6 TWAI TX and RX pins to transceiver TXD and RXD.
- Connect transceiver CANH and CANL to RobStride RS01 or RS02 bus. Ensure 120 ohm termination at bus ends.
- Default pins in include/config.h: TWAI_TX_PIN 5, TWAI_RX_PIN 6. Adjust as needed.

Build and Upload
- Use PlatformIO in VS Code or CLI.
- Board: esp32-s3-devkitc-1, Framework: espidf.
- Monitor speed 921600.

Serial Protocol
- Matches protocol_spec.md.
- Header 0xA5 0x5A, version 1, CRC16-CCITT over payload.

Control and Timing
- Control loop runs at 1 kHz in loop() using micros scheduling.
- When first setpoints arrive, firmware aligns trajectory time to local time with an offset.
- Maintains a ring buffer of future setpoints per motor. Default capacity 2048.
- Enforces a minimum buffer of 100 ms. If underrun occurs, transitions to a safe stop.

Interpolation
- Cubic Hermite interpolation between setpoints using positions and velocities.
- Hold flag prevents overshoot by freezing at the held value. Step flag is respected by clamping velocity via motion limits.
- Max velocity and acceleration limits are enforced from include/config.h.

RobStride Mode over CAN
- CAN 2.0B extended frames at 1,000,000 bps (configurable via TWAI_BAUD).
- Extended 29‑bit IDs: [type(5) | host_id(16) | motor_id(8)].
- Enable sequence: write 0x7005 = 1 (type 0x12), then enable (type 0x03).
- Position write: index 0x7016 (type 0x12), value float32 LE radians.
- Velocity write (homing): index 0x7017 (type 0x12), value float32 LE rad/s.
- Optional position read: request index 0x7016 (type 0x11); used to seed homing.

Initialization and Homing
- On boot, the firmware initializes TWAI, limit switch GPIOs (NC to GND with pull‑ups), LED driver, and loads the last commanded position from NVS.
- Homing starts immediately and serial is **not** started until homing completes or fails.
- Homing sequence:
  1) Optional position read over CAN to seed the homing integrator.
  2) Clear actuator target (STOP), then warm up with velocity=0 for HOMING_VEL_WARMUP_MS.
  3) Enable motor and move toward MAX at HOMING_VEL (velocity mode if supported).
  4) Reverse toward MIN at HOMING_VEL until the MIN switch triggers.
  5) Move off MIN by HOMING_CLEAR_MM so the switch releases.
  6) Calibrate travel range: compute radians per mm from MAX→MIN travel and TOTAL_RANGE_MM.
  7) Define 0 mm at the “cleared” MIN position; system waits for Blender setpoints.
- If velocity mode is unsupported, the firmware falls back to position‑based homing after HOMING_VEL_FALLBACK_MS.
- LED behavior (unique signals, priority order):
  - Limit switch active: solid GREEN.
  - Estop active: solid RED.
  - Watchdog tripped: fast RED blink.
  - Buffer underrun: double RED blink.
  - CAN TX failed: fast MAGENTA blink.
  - Interpolator empty: slow MAGENTA blink.
  - Serial pause (no new frames): slow AMBER blink.
  - Hold‑last mode: slow CYAN blink.
  - Calibration active: PURPLE breathe.
  - Homing active: BLUE breathe.
  - Streaming active: slow WHITE blink.
  - Idle: LED off.
  - Command received: brief CYAN pulse (only when idle/streaming).
  - CAN RX activity: brief WHITE pulse (only when idle/streaming).

Configuration
- Edit include/config.h to set pins, limits, and scaling constants.
- DEFAULT_MOTOR_ID selects the single CAN motor ID (default 4). Setpoints and commands for other IDs are ignored.
- TOTAL_RANGE_MM sets the full mechanical travel in millimeters (e.g., 2000.0).
- If using RobStride, adjust ROBSTRIDE_HOST_ID and indexes if your actuator firmware differs.

Troubleshooting
- No CAN frames: check transceiver wiring, TX/RX pins, CAN bitrate 1 Mbps, and termination.
- No motion: verify motor ID (ID = 0x200 + motor_id), send enable, and scaling constants.
- CRC errors: confirm baud rate and cable quality.
- Buffer underruns: increase Buffer ms in Blender or reduce Stream Hz.
Commands
- Enable: cmd 1
- Disable/Stop: cmd 2 or 3 (safe stop)
- Zero Offset: cmd 4 (software zero)
- Ping: cmd 5
- Home: cmd 6 (software home at current position)
- Calibrate: cmd 7 (bounded sine for ~3 s)

Telemetry
- Sent at ~10 Hz per motor with rx_count (serial frames OK), last_can_id, and status_flags bits (bit0 estop, bit1 calibration active).
