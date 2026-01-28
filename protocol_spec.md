RobStride Live Streamer Serial Protocol v1

Transport
- Byte stream over Serial at 921600 baud by default.
- Little-endian fields.
- Framed with header and CRC.

Frame
- Header: 0xA5 0x5A
- Version: uint8 (1)
- Type: uint8
  - 1 = SETPOINTS
  - 2 = COMMAND
  - 3 = TELEMETRY
- Sequence: uint32 (monotonic increment per transmitter)
- Timestamp_us: uint32 (trajectory time in microseconds)
- Count: uint8 (number of motors in payload, currently 1)

SETPOINTS Payload (per motor, repeated Count times)
- motor_id: uint8 (1..32 typical)
- pos: float32 (SI units. For rotation, radians. For translation, meters or user scale)
- vel: float32 (SI units per second)
- acc: float32 (SI units per second^2)
- kp: float32
- kd: float32
- t_ff: float32 (feedforward torque or effort)
- flags: uint16
  - bit 0: hold (constant interpolation region)
  - bit 1: step (discrete step at this time)
  - bit 2: e-stop
  - others reserved

COMMAND Payload
- cmd: uint8
  - 1 = enable
  - 2 = disable
  - 3 = stop (hold with low gains)
  - 4 = zero_offset
  - 5 = ping
  - 6 = home (software home at current position)
  - 7 = calibrate (bounded sine profile for a few seconds)
- motor_id: uint8 (0 means broadcast)

TELEMETRY Payload (optional)
- motor_id: uint8
- rx_count: uint32 (Serial frames received OK)
- can_rx_flags: uint16 (driver flags, zero if not used)
- last_can_id: uint32 (last received CAN ID)
- status_flags: uint16
  - bit 0: estop active
  - bit 1: calibration active
  - bit 2: watchdog tripped
  - bit 3: buffer underrun detected

CRC
- CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflect, no xorout) over bytes from Version through end of payload, excludes header bytes.
- CRC is appended as uint16 little-endian.

Notes
- Timestamp_us is trajectory time. Firmware aligns it to local clock on first packet to maintain schedule.
- Blender streams ahead to maintain a future buffer. Firmware interpolates at 1 kHz.
