RobStride Live Streamer Blender Add-on

Install
- In Blender, File > User Preferences > Add-ons > Install, select the robstride_streamer folder zipped.
- Enable the add-on named RobStride Live Streamer.
- The UI appears in View3D > Sidebar > RobStride.

Usage
- Select the Serial port and baud 921600.
- Choose the object per motor. Add up to 6 motor rows in the Motors box. For each enabled motor select an object, channel, motor ID, unit scale, and gains.
- Set stream rate (default 200 Hz) and buffer ahead ms (default 500 ms).
- Click Connect, then Send Enable, then Start Streaming. Play the timeline.
- Use Home to set software home at current position, Calibrate to run a short bounded sine profile on the ESP32.
- Telemetry box shows last CAN ID seen and counters.
- Loopback mode prints packets to the console instead of sending.

Sampling at Sub-frame Times
- The streamer samples at dt = 1/stream_rate in seconds.
- Frame float is computed as t_seconds * scene_fps.
- Values are evaluated using Blender FCurve evaluation so interpolation type is respected.
- Velocities use central differences and accelerations use a second difference.

Constant Interpolation Handling
- Regions with constant interpolation are treated as hold.
- During hold regions, vel and acc are published as zero.
- At steps, the new position is published with vel zero and a step flag so the ESP32 can apply its motion limits.

Notes
- CSV logging is not built in. Use loopback prints or sniff serial to capture raw frames if needed.
