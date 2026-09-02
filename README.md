# blender-esp32-robstride

STATUS: FINISHED (usable, well documented), but contains two separate,
overlapping Blender add-ons and should be read carefully.

## Two add-ons in this repo

This repo actually ships two distinct Blender add-ons, at different points
of the same family of work (stream Blender animation to RobStride actuators
via an ESP32 acting as a serial-to-CAN bridge):

1. **`addons/blender_esp32_single_axis/`** — "ESP32 Single-Axis Sender".
   Streams one object's X/Y/Z location to a single motor over serial, using
   a compact binary setpoint protocol (see `protocol_spec.md`). This is what
   the top-level `README.md` documents, and matches the most recent commit
   ("fix firmware for single axis") and `single-axis-firmware/`. Treat this
   as the **current, actively-worked path** for a single-motor setup.

2. **`blender_addon/robstride_streamer/`** — "RobStride Live Streamer".
   A more feature-rich, multi-motor version: up to several independently
   configured motor rows, per-motor channel/gain mapping, sub-frame FCurve
   sampling, and telemetry display. It's the older of the two (first
   packaged January 2025 per its internal docs) and comes with extensive
   packaging/distribution tooling (`setup.py`, `build_dist.py`,
   `install_addon.py`, multiple prebuilt zips in `dist/`).

Neither formally supersedes the other — pick based on your motor count:
single-axis add-on for one motor, `robstride_streamer` for multiple motors
on one bus. If you're only running one motor, start with the single-axis
add-on; it's simpler and is what's actively maintained.

## Relationship to other repos in this account

This repo is a sister effort to
[blender-robstride](https://github.com/n2048-creative-technology/blender-robstride),
which drives RobStride actuators directly over CAN from the host machine
(via python-can/CANopen) instead of bridging through an ESP32 over serial.
Pick blender-robstride if your host has a native CAN interface; pick this
repo if you want an ESP32 doing the CAN talking and a simple serial link to
Blender. Both are also related in spirit to the currently-unimplemented
[blender-net](https://github.com/n2048-creative-technology/blender-net)
concept (WiFi instead of CAN/serial).

## Hardware / protocol dependency

Requires:
- A **RobStride actuator** (RS01/RS02 or compatible) on a **CAN bus**
  (1 Mbps, CAN 2.0B extended frames, RobStride "MIT mode" command format).
- An **ESP32** (ESP32-C6 or ESP32-S3, see `esp32_firmware/` and
  `single-axis-firmware/`) with an external CAN transceiver, running the
  firmware in this repo, bridging serial <-> CAN.
- `pyserial` importable from Blender's Python, for the add-on side.

The serial wire protocol between Blender and the ESP32 is a custom binary
framing (header `0xA5 0x5A`, CRC16-CCITT) fully specified in
`protocol_spec.md` — not something you configure, just something the add-on
and matching firmware already agree on.

## Blender version compatibility

- Single-axis add-on: `bl_info` declares `"blender": (3, 0, 0)`. Uses
  standard `bpy.props`/`Operator`/`Panel`/`bpy.app.timers` — expected to work
  on Blender 4.x, not separately re-verified.
- `robstride_streamer` add-on: check `blender_addon/robstride_streamer/__init__.py`
  for its own `bl_info`; built and packaged around the same timeframe (early
  2025) with the same API patterns.

## Install

Single-axis add-on (recommended starting point):
1. Ensure Blender 3.0+, and `pyserial` installed into Blender's Python
   (`<blender_python> -m pip install pyserial`).
2. Zip `addons/blender_esp32_single_axis/` so `__init__.py` sits at the
   zip root, named `blender_esp32_single_axis.zip`.
3. Edit > Preferences > Add-ons > Install..., select the zip, enable it.
4. Flash `single-axis-firmware/` to your ESP32-S3 via PlatformIO (see its
   own README), wire up the CAN transceiver, connect the RobStride motor.
5. Open View3D > Sidebar > ESP32, select target object/axis, pick the
   serial port, Connect, then Enable/Home/Calibrate as needed before
   streaming.

Multi-motor `robstride_streamer` add-on:
1. Use `blender_addon/install_addon.py` (auto-detects your Blender install)
   or manually copy `blender_addon/robstride_streamer/` into your Blender
   addons folder. See `blender_addon/INSTALL.md` for full detail.
2. Flash `esp32_firmware/` (ESP32-C6) per its README.
3. Open View3D > Sidebar > RobStride, configure up to several motor rows,
   Connect, Send Enable, Start Streaming.

## License

MIT (the nested `blender_addon/LICENSE` already declared MIT; a matching
root-level `LICENSE` has been added by this review so it applies to the
whole repo, including the single-axis add-on and firmware).

## Note on repo contents

`blender_addon/` carries a large amount of AI-assistant-generated packaging
documentation (`00_START_HERE.txt`, `FINAL_SUMMARY.txt`,
`INSTALLATION_COMPLETE.txt`, `PACKAGE_VERIFICATION.txt`, etc.) left over from
an automated packaging pass. It's not wrong, just redundant — `README.md`,
`INSTALL.md`, and `DISTRIBUTION.md` cover everything a user or maintainer
needs; the rest can be pruned in a future cleanup pass if you want a leaner
repo.
