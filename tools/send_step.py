#!/usr/bin/env python3
import argparse
import struct
import time

try:
    import serial  # type: ignore
except Exception as e:  # pragma: no cover
    serial = None
    _serial_err = str(e)


HEADER = b"\xA5\x5A"
VERSION = 1

MSG_SETPOINTS = 1
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
    ap = argparse.ArgumentParser(description="Send a step setpoint sequence to RobStride over serial")
    ap.add_argument("port", help="Serial port, e.g. /dev/ttyACM0 or COM3")
    ap.add_argument("--baud", type=int, default=921600, help="Baud rate (default 921600)")
    ap.add_argument("--id", type=int, default=4, help="Motor ID (default 4)")
    ap.add_argument("--pos", type=float, default=1.0, help="Target position in radians (default 1.0)")
    ap.add_argument("--kp", type=float, default=30.0, help="Kp (default 30.0)")
    ap.add_argument("--kd", type=float, default=0.5, help="Kd (default 0.5)")
    ap.add_argument("--hz", type=float, default=50.0, help="Send rate in Hz (default 50)")
    ap.add_argument("--buffer-ms", type=int, default=500, help="Future buffer ahead in ms (default 500)")
    ap.add_argument("--duration", type=float, default=2.0, help="Duration to stream seconds (default 2.0)")
    ap.add_argument("--enable-first", action="store_true", help="Send enable command before setpoints")
    ap.add_argument("--verbose", action="store_true", help="Print what is being sent")
    args = ap.parse_args()

    if serial is None:
        raise SystemExit(
            "pyserial not available. Install with: pip install pyserial\n"
            f"Import error: {_serial_err}"
        )

    ser = serial.Serial(
        port=args.port,
        baudrate=args.baud,
        timeout=0,
        write_timeout=1,
        rtscts=False,
        xonxoff=False,
        dsrdtr=False,
    )
    try:
        # Ensure DTR is asserted and RTS is deasserted
        try:
            ser.dtr = True
            ser.rts = False
        except Exception:
            pass

        seq = 1

        if args.enable_first:
            if args.verbose:
                print(f"Sending ENABLE to ID {args.id}")
            ser.write(pack_command(seq, 1, motor_id=args.id, timestamp_us=0))
            seq = (seq + 1) & 0xFFFFFFFF
            time.sleep(0.05)

        # Stream a step: constant position with zero velocity/acceleration
        period = 1.0 / max(1e-6, args.hz)
        buffer_us = int(args.buffer_ms * 1000)
        t0_ns = time.perf_counter_ns()
        n_frames = max(1, int(args.duration * args.hz))

        for i in range(n_frames):
            now_ns = time.perf_counter_ns()
            elapsed_us = (now_ns - t0_ns) // 1000
            ts_us = int(elapsed_us + buffer_us)
            items = [{
                'motor_id': args.id,
                'pos': args.pos,
                'vel': 0.0,
                'acc': 0.0,
                'kp': args.kp,
                'kd': args.kd,
                't_ff': 0.0,
                'flags': 0,
            }]
            frame = pack_setpoints(seq, ts_us, items)
            ser.write(frame)
            if args.verbose and (i % max(1, int(args.hz // 5)) == 0):
                print(f"SETPOINT seq={seq} ts_us={ts_us} id={args.id} pos={args.pos}")
            seq = (seq + 1) & 0xFFFFFFFF
            # Sleep to maintain send rate
            t_elapsed = (time.perf_counter_ns() - now_ns) / 1e9
            time.sleep(max(0.0, period - t_elapsed))

    finally:
        try:
            ser.close()
        except Exception:
            pass


if __name__ == "__main__":
    main()

