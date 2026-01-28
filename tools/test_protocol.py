import sys, os, importlib.util

root = os.path.join(os.path.dirname(__file__), "..")
proto_path = os.path.join(root, "blender_addon", "robstride_streamer", "protocol.py")
spec = importlib.util.spec_from_file_location("protocol", proto_path)
protocol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)
_pack_telemetry_for_test = protocol._pack_telemetry_for_test
Parser = protocol.Parser


def test_telem_roundtrip():
    items = [
        {
            "motor_id": 1,
            "rx_count": 123,
            "can_rx_flags": 0,
            "last_can_id": 0x1ABCDE,
            "status_flags": 3,
        },
        {
            "motor_id": 2,
            "rx_count": 456,
            "can_rx_flags": 1,
            "last_can_id": 0x20000001,
            "status_flags": 0,
        },
    ]
    frame = _pack_telemetry_for_test(items, sequence=42, timestamp_us=1000)
    # Feed in chunks to parser
    p = Parser()
    out = []
    for i in range(0, len(frame), 7):
        out.extend(p.feed(frame[i:i+7]))
    assert out and out[-1]["type"] == 3, "No telemetry parsed"
    parsed_items = out[-1]["items"]
    assert len(parsed_items) == 2
    assert parsed_items[0]["motor_id"] == 1
    assert parsed_items[0]["rx_count"] == 123
    assert parsed_items[0]["last_can_id"] == 0x1ABCDE
    assert parsed_items[0]["status_flags"] == 3
    assert parsed_items[1]["motor_id"] == 2
    assert parsed_items[1]["rx_count"] == 456
    print("OK: telemetry roundtrip passed")


def test_telem_crc_failure():
    items = [{"motor_id": 1, "rx_count": 1, "can_rx_flags": 0, "last_can_id": 0, "status_flags": 0}]
    frame = bytearray(_pack_telemetry_for_test(items, sequence=1, timestamp_us=2))
    frame[-1] ^= 0xFF  # corrupt CRC
    p = Parser()
    out = p.feed(bytes(frame))
    assert not out, "CRC failure should not produce output"
    print("OK: CRC failure rejected")


if __name__ == "__main__":
    test_telem_roundtrip()
    test_telem_crc_failure()
    print("All protocol tests passed")
