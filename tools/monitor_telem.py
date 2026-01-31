#!/usr/bin/env python3
import argparse
import importlib.util
import os
import sys

try:
    import serial  # type: ignore
except Exception as e:
    print("pyserial required: pip install pyserial", file=sys.stderr)
    raise


def load_protocol():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    proto_path = os.path.join(root, 'blender_addon', 'robstride_streamer', 'protocol.py')
    spec = importlib.util.spec_from_file_location('protocol', proto_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)  # type: ignore
    return mod


def main():
    ap = argparse.ArgumentParser(description="Parse RobStride telemetry/error frames from serial")
    ap.add_argument('port', help='Serial port, e.g. /dev/ttyACM0')
    ap.add_argument('--baud', type=int, default=921600)
    args = ap.parse_args()

    proto = load_protocol()
    parser = proto.Parser()

    ser = serial.Serial(args.port, args.baud, timeout=0)
    print(f"Monitoring {args.port} @ {args.baud}… (Ctrl+C to quit)")
    try:
        buf = bytearray()
        while True:
            data = ser.read(512)
            if not data:
                continue
            frames = parser.feed(data)
            for fr in frames:
                t = fr.get('type')
                if t == proto.MSG_TELEMETRY:
                    for it in fr.get('items', []):
                        mid = it.get('motor_id')
                        rx = it.get('rx_count')
                        last = it.get('last_can_id')
                        status = it.get('status_flags')
                        flags = []
                        if status & 1: flags.append('ESTOP')
                        if status & 2: flags.append('CAL')
                        if status & 4: flags.append('WD')
                        if status & 8: flags.append('UNDERRUN')
                        print(f"TELEM id={mid} rx_ok={rx} last_can=0x{last:X} status={'|'.join(flags) if flags else 'OK'}")
                elif t == proto.MSG_ERROR:
                    for it in fr.get('items', []):
                        mid = it.get('motor_id')
                        code = it.get('error_code')
                        print(f"ERROR id={mid} code={code}")
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == '__main__':
    main()

