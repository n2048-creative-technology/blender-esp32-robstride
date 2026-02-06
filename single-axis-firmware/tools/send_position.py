#!/usr/bin/env python3
import argparse
import struct
import time

import serial


HDR1 = 0xA5
HDR2 = 0x5A
MSG_SETPOINTS = 1


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_setpoint_frame(pos_mm: float, ts_us: int) -> bytes:
    version = 1
    typ = MSG_SETPOINTS
    seq = 0
    count = 1
    vel = 0.0
    acc = 0.0
    kp = 0.0
    kd = 0.0
    t_ff = 0.0
    flags = 0
    payload = bytearray()
    payload += struct.pack("<BB", version, typ)
    payload += struct.pack("<I", seq)
    payload += struct.pack("<I", ts_us & 0xFFFFFFFF)
    payload += struct.pack("<B", count)
    payload += struct.pack("<ffffffH", pos_mm, vel, acc, kp, kd, t_ff, flags)
    crc = crc16_ccitt(payload)
    frame = bytearray([HDR1, HDR2])
    frame += payload
    frame += struct.pack("<H", crc)
    return bytes(frame)


def main() -> int:
    parser = argparse.ArgumentParser(description="Send single-axis position setpoint (mm) to ESP32.")
    parser.add_argument("port", help="Serial port (e.g. /dev/ttyACM0 or COM3)")
    parser.add_argument("pos_mm", type=float, help="Target position in mm (centered at 0)")
    parser.add_argument("--baud", type=int, default=921600, help="Serial baud rate")
    parser.add_argument("--lookahead-us", type=int, default=150_000, help="Lookahead timestamp in us")
    parser.add_argument("--rate-hz", type=float, default=30.0, help="Send rate in Hz")
    parser.add_argument("--duration-s", type=float, default=2.0, help="Duration to stream setpoint")
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        period = 1.0 / max(1e-3, args.rate_hz)
        end_t = time.time() + max(0.1, args.duration_s)
        while time.time() < end_t:
            ts_us = int(time.time() * 1_000_000) + args.lookahead_us
            frame = build_setpoint_frame(args.pos_mm, ts_us)
            ser.write(frame)
            ser.flush()
            time.sleep(period)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
