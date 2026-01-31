#!/usr/bin/env python3
import argparse
import math
import struct
import time

try:
    import serial  # type: ignore
except Exception as e:
    raise SystemExit("pyserial required. Install with: pip install pyserial")

HEADER = b"\xA5\x5A"
VERSION = 1
MSG_SETPOINTS = 1


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


def pack_setpoints(sequence: int, timestamp_us: int, items: list):
    payload = struct.pack(
        "<BBIIB",
        VERSION,
        MSG_SETPOINTS,
        sequence & 0xFFFFFFFF,
        timestamp_us & 0xFFFFFFFF,
        len(items) & 0xFF,
    )
    for it in items:
        payload += struct.pack(
            "<BffffffH",
            int(it.get("motor_id", 1)) & 0xFF,
            float(it.get("pos", 0.0)),
            float(it.get("vel", 0.0)),
            float(it.get("acc", 0.0)),
            float(it.get("kp", 30.0)),
            float(it.get("kd", 0.5)),
            float(it.get("t_ff", 0.0)),
            int(it.get("flags", 0)) & 0xFFFF,
        )
    crc = crc16_ccitt(payload)
    return HEADER + payload + struct.pack("<H", crc)


def main():
    ap = argparse.ArgumentParser(description="Set a RobStride motor target position via ESP32 serial")
    ap.add_argument("port", help="Serial port, e.g. /dev/ttyACM0")
    ap.add_argument("--id", type=int, required=True, help="Motor ID (node ID)")
    ap.add_argument("--pos", type=float, required=True, help="Target position (radians by default)")
    ap.add_argument("--deg", action="store_true", help="Interpret --pos as degrees (converted to radians)")
    ap.add_argument("--baud", type=int, default=921600, help="Baud rate (default 921600)")
    ap.add_argument("--kp", type=float, default=30.0, help="Kp (ignored in RobStride mode, default 30.0)")
    ap.add_argument("--kd", type=float, default=0.5, help="Kd (ignored in RobStride mode, default 0.5)")
    ap.add_argument("--hold-ms", type=int, default=500, help="Future buffer ahead in ms (default 500)")
    ap.add_argument("--burst-s", type=float, default=1.0, help="How long to keep sending the target (default 1.0 s)")
    ap.add_argument("--hz", type=float, default=50.0, help="Send rate during burst (default 50 Hz)")
    args = ap.parse_args()

    target = args.pos * (math.pi / 180.0) if args.deg else args.pos

    ser = serial.Serial(args.port, args.baud, timeout=0, write_timeout=1)
    try:
        seq = 1
        t0 = time.perf_counter_ns()
        period = 1.0 / max(1e-3, args.hz)
        end_time = time.perf_counter() + max(0.05, args.burst_s)

        while time.perf_counter() < end_time:
            now_ns = time.perf_counter_ns()
            elapsed_us = (now_ns - t0) // 1000
            ts_us = int(elapsed_us + args.hold_ms * 1000)
            items = [{
                'motor_id': args.id,
                'pos': target,
                'vel': 0.0,
                'acc': 0.0,
                'kp': args.kp,
                'kd': args.kd,
                't_ff': 0.0,
                'flags': 0,
            }]
            frame = pack_setpoints(seq, ts_us, items)
            ser.write(frame)
            seq = (seq + 1) & 0xFFFFFFFF
            time.sleep(period)

        print(f"Sent position {target:.6f} rad to motor ID {args.id} on {args.port}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()

