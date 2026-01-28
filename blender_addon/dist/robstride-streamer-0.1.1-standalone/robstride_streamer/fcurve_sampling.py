import bpy
import math


def _get_channel_value(obj, channel: str):
    if channel == 'LOC_X':
        return obj.location.x
    if channel == 'LOC_Y':
        return obj.location.y
    if channel == 'LOC_Z':
        return obj.location.z
    if channel == 'ROT_X':
        return obj.rotation_euler.x
    if channel == 'ROT_Y':
        return obj.rotation_euler.y
    if channel == 'ROT_Z':
        return obj.rotation_euler.z
    return 0.0


def _find_fcurve(obj, channel: str, scene_frame: float = None):
    ad = obj.animation_data
    if not ad:
        return None, None
    data_path = None
    array_index = None
    if channel.startswith('LOC_'):
        data_path = 'location'
    elif channel.startswith('ROT_'):
        data_path = 'rotation_euler'
    if data_path is None:
        return None, None
    map_idx = {'X': 0, 'Y': 1, 'Z': 2}
    array_index = map_idx.get(channel[-1], 0)
    # First, try active action
    if ad.action is not None:
        fcurves = getattr(ad.action, 'fcurves', None)
        if fcurves is not None:
            for fc in fcurves:
                if fc.data_path == data_path and fc.array_index == array_index:
                    return fc, None
    # Next, try NLA strips at current scene frame
    if scene_frame is not None and getattr(ad, 'nla_tracks', None):
        for tr in ad.nla_tracks:
            if getattr(tr, 'mute', False):
                continue
            for strip in getattr(tr, 'strips', []) or []:
                if scene_frame < strip.frame_start or scene_frame > strip.frame_end:
                    continue
                act = getattr(strip, 'action', None)
                if not act:
                    continue
                fcurves = getattr(act, 'fcurves', None)
                if fcurves is None:
                    continue
                for fc in fcurves:
                    if fc.data_path == data_path and fc.array_index == array_index:
                        return fc, strip
    return None, None


def _is_constant_between(fcurve, frame0: float, frame1: float) -> bool:
    if not fcurve:
        return False
    kfps = getattr(fcurve, 'keyframe_points', None)
    if kfps is None:
        return False
    for kp in kfps:
        if kp.co[0] >= frame0 and kp.co[0] < frame1:
            if kp.interpolation == 'CONSTANT':
                return True
    # Also treat segments between keyframes with constant interpolation
    # Find segment covering frame0
    kps = list(kfps)
    if len(kps) < 2:
        return False
    for i in range(len(kps) - 1):
        if kps[i].co[0] <= frame0 < kps[i + 1].co[0]:
            if kps[i].interpolation == 'CONSTANT':
                return True
    return False


def sample_stream_points(
    obj,
    channel: str,
    fps: float,
    start_time_s: float,
    horizon_s: float,
    rate_hz: float,
    unit_scale: float,
    rotation_in_radians: bool = True,
    scene=None,
    use_scene_eval: bool = False,
):
    dt = 1.0 / max(1.0, rate_hz)
    t = start_time_s
    frame_base = bpy.context.scene.frame_start
    scene_frame0 = start_time_s * fps
    fcurve, nla_strip = _find_fcurve(obj, channel, scene_frame0)
    points = []
    # Helper for scene evaluation at fractional frame
    def eval_scene(frame_f: float) -> float:
        if scene is None:
            return _get_channel_value(obj, channel)
        # Wrap frame into scene frame range to support looping playback
        try:
            f_start = float(getattr(scene, 'frame_start', 1))
            f_end = float(getattr(scene, 'frame_end', f_start))
            length = max(1.0, (f_end - f_start + 1.0))
            f_wrapped = f_start + ((frame_f - f_start) % length)
        except Exception:
            f_wrapped = frame_f
        frame_int = int(math.floor(f_wrapped))
        sub = float(f_wrapped - frame_int)
        scene.frame_set(frame_int, subframe=sub)
        return _get_channel_value(obj, channel)

    # Save and restore scene time around evaluations if using scene eval
    orig_frame = None
    orig_sub = 0.0
    if use_scene_eval and scene is not None:
        orig_frame = scene.frame_current
        orig_sub = getattr(scene, 'frame_subframe', 0.0)
    prev_val = None
    while t <= start_time_s + horizon_s + 1e-9:
        f = t * fps
        # Evaluate at subframe
        if use_scene_eval and scene is not None:
            val = eval_scene(f)
        elif fcurve:
            if nla_strip is not None:
                # Map scene frame to action frame
                sf = f
                total_sf = max(1e-6, (nla_strip.frame_end - nla_strip.frame_start))
                total_af = (nla_strip.action_frame_end - nla_strip.action_frame_start)
                af = nla_strip.action_frame_start + (sf - nla_strip.frame_start) * (total_af / total_sf)
                val = fcurve.evaluate(af)
            else:
                val = fcurve.evaluate(f)
        else:
            val = _get_channel_value(obj, channel)
        # Convert units
        if channel.startswith('ROT_') and rotation_in_radians:
            pos = float(val) * unit_scale
        else:
            pos = float(val) * unit_scale
        points.append((t, pos))
        prev_val = val
        t += dt

    # Compute velocity and acceleration using finite differences around each sample time.
    # Use direct FCurve evaluation at t-dt and t+dt to avoid zero-derivative at edges.
    samples = []
    n = len(points)
    eps = 1e-6
    dt_nom = 1.0 / max(1.0, rate_hz)
    # Use a slightly larger symmetric window for derivatives to reduce numerical cancellation.
    # Quarter-frame window typically yields stable slopes for linear/Bezier segments.
    h_d = max(dt_nom, 0.25 / max(1e-6, fps))
    for i, (t_i, p_i) in enumerate(points):
        # Evaluate neighbors at t +/- h_d using either scene eval or FCurve
        if use_scene_eval and scene is not None:
            f_prev = (t_i - h_d) * fps
            f_next = (t_i + h_d) * fps
            val_prev = eval_scene(f_prev)
            val_next = eval_scene(f_next)
            if channel.startswith('ROT_') and rotation_in_radians:
                p_prev = float(val_prev) * unit_scale
                p_next = float(val_next) * unit_scale
            else:
                p_prev = float(val_prev) * unit_scale
                p_next = float(val_next) * unit_scale
        elif fcurve:
            f_prev = (t_i - h_d) * fps
            f_next = (t_i + h_d) * fps
            if nla_strip is not None:
                total_sf = max(1e-6, (nla_strip.frame_end - nla_strip.frame_start))
                total_af = (nla_strip.action_frame_end - nla_strip.action_frame_start)
                af_prev = nla_strip.action_frame_start + (f_prev - nla_strip.frame_start) * (total_af / total_sf)
                af_next = nla_strip.action_frame_start + (f_next - nla_strip.frame_start) * (total_af / total_sf)
                val_prev = fcurve.evaluate(af_prev)
                val_next = fcurve.evaluate(af_next)
            else:
                val_prev = fcurve.evaluate(f_prev)
                val_next = fcurve.evaluate(f_next)
            if channel.startswith('ROT_') and rotation_in_radians:
                p_prev = float(val_prev) * unit_scale
                p_next = float(val_next) * unit_scale
            else:
                p_prev = float(val_prev) * unit_scale
                p_next = float(val_next) * unit_scale
        else:
            # No curve and no scene eval: fallback flat
            p_prev = p_i
            p_next = p_i
        # Velocity central difference
        v = (p_next - p_prev) / (2.0 * max(1e-9, h_d))
        # Acceleration central difference
        a = (p_next - 2 * p_i + p_prev) / max(1e-9, h_d * h_d)
        hold = False
        step = False
        # Hold detection only based on FCurve interpolation when not using scene eval
        if (not use_scene_eval) and (fcurve and _is_constant_between(fcurve, (t_i) * fps, (t_i + h_d) * fps)):
            hold = True
            v = 0.0
            a = 0.0
            # Step detection: if the function changes across this time window while interpolation is constant
            if fcurve:
                f_prev = (t_i - h_d) * fps
                f_next = (t_i + h_d) * fps
                try:
                    if nla_strip is not None:
                        total_sf = max(1e-6, (nla_strip.frame_end - nla_strip.frame_start))
                        total_af = (nla_strip.action_frame_end - nla_strip.action_frame_start)
                        af_prev = nla_strip.action_frame_start + (f_prev - nla_strip.frame_start) * (total_af / total_sf)
                        af_next = nla_strip.action_frame_start + (f_next - nla_strip.frame_start) * (total_af / total_sf)
                        val_prev = fcurve.evaluate(af_prev)
                        val_next = fcurve.evaluate(af_next)
                    else:
                        val_prev = fcurve.evaluate(f_prev)
                        val_next = fcurve.evaluate(f_next)
                    # Apply same unit scaling
                    if channel.startswith('ROT_') and rotation_in_radians:
                        val_prev = float(val_prev) * unit_scale
                        val_next = float(val_next) * unit_scale
                    else:
                        val_prev = float(val_prev) * unit_scale
                        val_next = float(val_next) * unit_scale
                    if abs(val_next - val_prev) > eps:
                        step = True
                except Exception:
                    pass
        samples.append({
            't': t_i,
            'pos': p_i,
            'vel': v,
            'acc': a,
            'flags': (1 if hold else 0) | (2 if step else 0),
        })
    # Restore scene time
    if use_scene_eval and scene is not None and orig_frame is not None:
        scene.frame_set(orig_frame, subframe=orig_sub)
    return samples
