#!/usr/bin/env python3
import argparse
import struct
import time

try:
    import serial  # type: ignore
except Exception as e:
    raise SystemExit("pyserial required. Install with: pip install pyserial")

HEADER = b"\xA5\x5A"
VERSION = 1
MSG_COMMAND = 2


def crc16_ccitt(data: bytes, poly=0x1021, init=0xFFFF) -> int:
    crc = init
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ poly
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def pack_command(sequence: int, cmd: int, motor_id: int = 0, timestamp_us: int = 0):
    payload = struct.pack(
        "<BBIIBB",
        VERSION,
        MSG_COMMAND,
        sequence & 0xFFFFFFFF,
        timestamp_us & 0xFFFFFFFF,
        1,
        cmd & 0xFF,
    ) + struct.pack("<B", motor_id & 0xFF)
    crc = crc16_ccitt(payload)
    return HEADER + payload + struct.pack("<H", crc)


def main():
    ap = argparse.ArgumentParser(description="Enable a RobStride motor via ESP32 serial")
    ap.add_argument("port", help="Serial port, e.g. /dev/ttyACM0")
    ap.add_argument("--id", type=int, required=True, help="Motor ID (node ID)")
    ap.add_argument("--baud", type=int, default=921600, help="Baud rate (default 921600)")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0, write_timeout=1)
    try:
        seq = 1
        frame = pack_command(seq, 1, motor_id=args.id, timestamp_us=0)
        ser.write(frame)
        # small delay for the two CAN frames in firmware enable sequence
        time.sleep(0.05)
        print(f"Sent ENABLE for motor ID {args.id} to {args.port}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()

