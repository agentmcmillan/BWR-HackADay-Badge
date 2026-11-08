# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile monochrome source frames into narrow, panel-priced update bands."""
import math
import numpy as np


def adaptive_band_frames(
    frames,
    band_rows=8,
    max_age=12,
    motion_weight=2.0,
    focus=None,
    max_frames=600,
    cyclic_base=False,
    base_occupancy=0.65,
):
    """Return whole-frame states whose consecutive differences fit one band.

    The firmware's existing partial driver discovers the narrow difference and
    limits both RAM traffic and the physical gate scan. By default the final
    state is white. With cyclic_base, frame zero is a one-time complete base;
    later operations remain narrow and return to that base without dissolving
    the subject to white.
    """
    if (
        not isinstance(frames, np.ndarray)
        or frames.ndim != 3
        or frames.dtype != np.bool_
        or not len(frames)
        or not 1 <= band_rows <= frames.shape[1]
        or not 1 <= max_age <= 1000
        or not 0 <= motion_weight <= 100
        or not 1 <= max_frames <= 600
        or not isinstance(cyclic_base, (bool, np.bool_))
        or (cyclic_base and len(frames) < 2)
        or not isinstance(base_occupancy, (int, float, np.integer, np.floating))
        or not np.isfinite(base_occupancy)
        or not 0.05 <= base_occupancy <= 1.0
    ):
        raise ValueError("Invalid monochrome frames or adaptive-band settings")
    height, width = frames.shape[1:]
    if focus is not None:
        if len(focus) != 4:
            raise ValueError("Focus must be x,y,width,height")
        x, y, w, h = map(int, focus)
        if x < 0 or y < 0 or w < 1 or h < 1 or x + w > width or y + h > height:
            raise ValueError("Focus rectangle is outside the frame")
    bands = [(y, min(y + band_rows, height)) for y in range(0, height, band_rows)]
    reserved = len(bands) + (1 if cyclic_base else 0)
    source_limit = max_frames - reserved
    if source_limit < 1:
        raise ValueError("Band geometry leaves no room for source operations")
    # Whole-clip occupancy isolates ink that is stable enough to draw once.
    # Moving pixels remain in the newest target and are corrected in both their
    # old and new locations by the same error-mask score.
    base = (
        np.mean(frames, axis=0) >= float(base_occupancy)
        if cyclic_base
        else np.zeros((height, width), dtype=np.bool_)
    )
    targets = frames[1 : 1 + source_limit] if cyclic_base else frames[:source_limit]
    state = base.copy()
    previous_target = state.copy()
    age = np.zeros(len(bands), dtype=np.int32)
    weights = np.ones((height, width), dtype=np.float32)
    if focus is not None:
        x, y, w, h = map(int, focus)
        weights[y : y + h, x : x + w] = 4.0
    output = [state.copy()] if cyclic_base else []
    source_operations = 0
    for target in targets:
        age += 1
        motion = target != previous_target
        scores = np.full(len(bands), -1.0, dtype=np.float64)
        for index, (y0, y1) in enumerate(bands):
            error_mask = target[y0:y1] != state[y0:y1]
            if not error_mask.any():
                continue
            weighted_error = float(weights[y0:y1][error_mask].sum())
            weighted_motion = float(weights[y0:y1][motion[y0:y1]].sum())
            scores[index] = weighted_error + motion_weight * weighted_motion
            scores[index] *= 1.0 + min(age[index], max_age) / max_age
            if age[index] >= max_age:
                scores[index] += height * width * (age[index] - max_age + 1)
        chosen = int(np.argmax(scores))
        if scores[chosen] >= 0:
            y0, y1 = bands[chosen]
            state[y0:y1] = target[y0:y1]
            age[chosen] = 0
            output.append(state.copy())
            source_operations += 1
        previous_target = target

    # Ordinary adaptive clips close through white. A cyclic-base clip instead
    # returns each band to its complete base pose, keeping the subject present.
    closure_operations = 0
    while np.any(state != base):
        error = np.array([np.count_nonzero(state[y0:y1] != base[y0:y1]) for y0, y1 in bands])
        chosen = int(np.argmax(error))
        if not error[chosen]:
            break
        y0, y1 = bands[chosen]
        state[y0:y1] = base[y0:y1]
        output.append(state.copy())
        closure_operations += 1
    if not output:
        output.append(state.copy())
    if cyclic_base and len(output) == 1:
        output.append(state.copy())

    compiled = np.stack(output)
    previous = compiled[0] if cyclic_base else np.zeros_like(compiled[0])
    changed_rows = []
    for frame in compiled[1:] if cyclic_base else compiled:
        changed_rows.append(int(np.count_nonzero(np.any(frame != previous, axis=1))))
        previous = frame
    wrap_target = compiled[1] if cyclic_base else compiled[0]
    wrap_rows = int(np.count_nonzero(np.any(wrap_target != compiled[-1], axis=1)))
    if max(changed_rows + [wrap_rows]) > band_rows or len(compiled) > max_frames:
        raise RuntimeError("Adaptive compiler violated its band/frame bound")
    return compiled, {
        "input_frames": int(len(frames)),
        "consumed_frames": int(len(targets)),
        "output_frames": int(len(compiled)),
        "source_operations": source_operations,
        "closure_operations": closure_operations,
        "cyclic_base": bool(cyclic_base),
        "loop_start": 1 if cyclic_base else 0,
        "initializer_changed_rows": int(np.count_nonzero(np.any(compiled[0], axis=1))),
        "base_occupancy": float(base_occupancy) if cyclic_base else None,
        "base_ink_pixels": int(np.count_nonzero(base)),
        "band_rows": int(band_rows),
        "panel_band_rows": int(band_rows * 4),
        "mean_changed_rows": float(np.mean(changed_rows)),
        "max_changed_rows": int(max(changed_rows)),
        "wrap_changed_rows": wrap_rows,
        "band_count": int(math.ceil(height / band_rows)),
    }
