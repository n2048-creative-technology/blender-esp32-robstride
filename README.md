RobStride Live Streamer: Blender to ESP32-C6 over Serial to CAN (TWAI)

Overview
- Blender add-on streams real-time setpoints from an animated object at high rate.
- ESP32-C6 firmware receives framed binary packets, buffers and interpolates at 1 kHz, and commands RobStride RS01 or RS02 motors over CAN 2.0B extended frames using MIT mode.
- Default rates: Blender streaming 200 Hz, ESP32 control loop 1 kHz. Default future buffer 500 ms.

Key Specs
- Protocol: Little-endian, header 0xA5 0x5A, CRC16-CCITT over payload.
- CAN: 1 Mbps, extended 29-bit IDs. Command ID: 0x200 + motor_id. Enable uses 8 bytes of 0xFF.
- MIT mode payload packing: 8 bytes total using configurable scaling in firmware. See esp32_firmware/include/config.h and robstride_can.cpp. Adjust scales to match your motor firmware.
- Multi-motor streaming up to 6 motors with per-motor channels and gains.

Repo Layout
- blender_addon/
  - robstride_streamer/ Python modules
  - README.md add-on instructions
- esp32_firmware/
  - PlatformIO Arduino project for ESP32-C6
  - README.md wiring, build, config
- protocol_spec.md complete serial protocol reference

- Blender add-on: install zip from blender_addon folder, enable, select serial port, add motor rows with IDs and channels, click Connect, Send Enable, Start Streaming while the timeline plays.
- ESP32-C6: flash via PlatformIO to ESP32-C6-DevKitC-1, connect external CAN transceiver to TWAI pins and RobStride CANH/CANL with proper termination. Ensure 1 Mbps bus rate.

Safety and Telemetry
- Firmware watchdog: if no valid setpoints for > 100 ms, it ramps to stop and holds.
- Soft limits, max velocity, and max acceleration enforced.
- E-stop via COMMAND message forces immediate safe behavior.
- Telemetry frames from ESP32 to Blender report last CAN activity and status bits.
- Homing and calibration commands available from Blender.
- CSV logging is not included; use loopback prints for quick inspection.

Troubleshooting
- If no motion: confirm CAN bitrate, IDs, transceiver wiring, termination, and scaling constants.
- If CRC errors: check baud rate 921600 and cable quality.
- If buffer underruns: increase buffer ahead ms in Blender or reduce stream rate.
