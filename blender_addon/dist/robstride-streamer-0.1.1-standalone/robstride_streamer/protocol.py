import struct

HEADER = b"\xA5\x5A"
VERSION = 1

MSG_SETPOINTS = 1
MSG_COMMAND = 2
MSG_TELEMETRY = 3


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
    # items: list of dicts with keys: motor_id, pos, vel, acc, kp, kd, t_ff, flags
    count = len(items)
    payload = struct.pack(
        "<BBIIB",
        VERSION,
        MSG_SETPOINTS,
        sequence & 0xFFFFFFFF,
        timestamp_us & 0xFFFFFFFF,
        count & 0xFF,
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
    frame = HEADER + payload + struct.pack("<H", crc)
    return frame


def pack_command(sequence: int, cmd: int, motor_id: int = 0, timestamp_us: int = 0):
    payload = struct.pack(
        "<BBIIBB",
        VERSION,
        MSG_COMMAND,
        sequence & 0xFFFFFFFF,
        timestamp_us & 0xFFFFFFFF,
        1,  # count or payload length hint
        cmd & 0xFF,
    ) + struct.pack("<B", motor_id & 0xFF)
    crc = crc16_ccitt(payload)
    frame = HEADER + payload + struct.pack("<H", crc)
    return frame


class Parser:
    def __init__(self):
        self.state = 0
        self.buf = bytearray()
        self.payload = bytearray()
        self.need = 0
        self.fixed = bytearray()

    def feed(self, data: bytes):
        out = []
        for b in data:
            if self.state == 0:  # find header 1
                if b == 0xA5:
                    self.state = 1
                else:
                    self.state = 0
            elif self.state == 1:  # find header 2
                if b == 0x5A:
                    self.state = 2
                    self.fixed = bytearray()
                else:
                    self.state = 0
            elif self.state == 2:
                # Read fixed 11 bytes
                self.fixed.append(b)
                if len(self.fixed) == 11:
                    version, mtype = self.fixed[0], self.fixed[1]
                    seq = int.from_bytes(self.fixed[2:6], 'little')
                    ts = int.from_bytes(self.fixed[6:10], 'little')
                    count = self.fixed[10]
                    if mtype == MSG_TELEMETRY:
                        self.need = count * (1 + 4 + 2 + 4 + 2)
                    elif mtype == MSG_SETPOINTS:
                        self.need = count * (1 + 6 * 4 + 2)
                    elif mtype == MSG_COMMAND:
                        self.need = 2
                    else:
                        self.state = 0
                        continue
                    self.payload = bytearray()
                    self.state = 3
            elif self.state == 3:
                self.payload.append(b)
                if len(self.payload) == self.need:
                    self.state = 4
                    self.crc = bytearray()
            elif self.state == 4:
                self.crc.append(b)
                if len(self.crc) == 2:
                    # Validate
                    version, mtype = self.fixed[0], self.fixed[1]
                    seq = int.from_bytes(self.fixed[2:6], 'little')
                    ts = int.from_bytes(self.fixed[6:10], 'little')
                    count = self.fixed[10]
                    calc = crc16_ccitt(bytes(self.fixed) + bytes(self.payload))
                    rx = int.from_bytes(self.crc, 'little')
                    if rx == calc:
                        if mtype == MSG_TELEMETRY:
                            items = []
                            off = 0
                            for _ in range(count):
                                motor_id = self.payload[off]
                                rx_count = int.from_bytes(self.payload[off+1:off+5], 'little')
                                can_flags = int.from_bytes(self.payload[off+5:off+7], 'little')
                                last_id = int.from_bytes(self.payload[off+7:off+11], 'little')
                                status = int.from_bytes(self.payload[off+11:off+13], 'little')
                                off += 13
                                items.append({
                                    'motor_id': motor_id,
                                    'rx_count': rx_count,
                                    'can_rx_flags': can_flags,
                                    'last_can_id': last_id,
                                    'status_flags': status,
                                    'timestamp_us': ts,
                                })
                            out.append({
                                'type': MSG_TELEMETRY,
                                'version': version,
                                'seq': seq,
                                'timestamp_us': ts,
                                'items': items,
                            })
                    self.state = 0
        return out


# Testing helper: pack a telemetry frame (not used by UI)
def _pack_telemetry_for_test(items: list, sequence: int = 0, timestamp_us: int = 0):
    # items: list of dicts with motor_id, rx_count, can_rx_flags, last_can_id, status_flags
    count = len(items)
    payload = struct.pack(
        "<BBIIB",
        VERSION,
        MSG_TELEMETRY,
        sequence & 0xFFFFFFFF,
        timestamp_us & 0xFFFFFFFF,
        count & 0xFF,
    )
    for it in items:
        payload += struct.pack(
            "<B I H I H",
            int(it.get("motor_id", 1)) & 0xFF,
            int(it.get("rx_count", 0)) & 0xFFFFFFFF,
            int(it.get("can_rx_flags", 0)) & 0xFFFF,
            int(it.get("last_can_id", 0)) & 0xFFFFFFFF,
            int(it.get("status_flags", 0)) & 0xFFFF,
        )
    crc = crc16_ccitt(payload)
    return HEADER + payload + struct.pack("<H", crc)
