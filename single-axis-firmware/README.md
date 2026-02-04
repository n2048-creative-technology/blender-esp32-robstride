ESP32-C6 Firmware for RobStride Live Streamer

Hardware
- Target: ESP32-C6-DevKitC-1
- External CAN transceiver required. ESP32-C6 has TWAI controller only.
- Wire ESP32-C6 TWAI TX and RX pins to transceiver TXD and RXD.
- Connect transceiver CANH and CANL to RobStride RS01 or RS02 bus. Ensure 120 ohm termination at bus ends.
- Default pins in include/config.h: TWAI_TX_PIN 5, TWAI_RX_PIN 6. Adjust as needed.

Build and Upload
- Use PlatformIO in VS Code or CLI.
- Board: esp32-c6-devkitc-1, Framework: Arduino.
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

RobStride MIT Mode over CAN
- CAN 2.0B extended frames at 1,000,000 bps.
- Command ID: 0x200 + motor_id.
- Enable: send 8 bytes of 0xFF to the command ID.
- Position and velocity control: packs p, v, kp, kd, t into 8 bytes using scaling defined in include/config.h. Adjust ranges to match your motor firmware.
- Firmware logs if any CAN RX frame is received.

Configuration
- Edit include/config.h to set pins, limits, and scaling constants.
- DEFAULT_MOTOR_ID selects the single CAN motor ID (default 4). Setpoints and commands for other IDs are ignored.
- Adjust POS_MIN/MAX, VEL_MIN/MAX, KP_MIN/MAX, KD_MIN/MAX, T_MIN/MAX to match RobStride firmware expectations.

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
