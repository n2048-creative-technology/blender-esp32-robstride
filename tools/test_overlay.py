import os, importlib.util

root = os.path.join(os.path.dirname(__file__), "..")
path = os.path.join(root, "blender_addon", "robstride_streamer", "telemetry_view.py")
spec = importlib.util.spec_from_file_location("telemetry_view", path)
telemetry_view = importlib.util.module_from_spec(spec)
spec.loader.exec_module(telemetry_view)


def test_format_status_bar():
    last_telem = {
        1: {"status_flags": 1 | 8, "last_can_id": 0x20000001},
        2: {"status_flags": 0, "last_can_id": 0},
        3: {"status_flags": 2 | 4, "last_can_id": 0x1ABCDE},
    }
    s = telemetry_view.format_status_bar(last_telem)
    assert "RobStride:" in s
    assert "ID 1 EU" in s  # E and U bits set
    assert "ID 2 -" in s
    assert "ID 3 CW" in s  # C and W bits set
    print("OK: overlay formatting passed")


if __name__ == "__main__":
    test_format_status_bar()
    print("All overlay tests passed")

