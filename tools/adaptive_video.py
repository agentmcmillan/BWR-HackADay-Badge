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
):
    """Return whole-frame states whose consecutive differences fit one band.

    The firmware's existing partial driver discovers the narrow difference and
    limits both RAM traffic and the physical gate scan. The final state is white,
    making the last-to-first loop transition narrow as well.
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
    source_limit = max_frames - len(bands)
    if source_limit < 1:
        raise ValueError("Band geometry leaves no room for source operations")
    targets = frames[:source_limit]
    state = np.zeros((height, width), dtype=np.bool_)
    previous_target = state.copy()
    age = np.zeros(len(bands), dtype=np.int32)
    weights = np.ones((height, width), dtype=np.float32)
    if focus is not None:
        x, y, w, h = map(int, focus)
        weights[y : y + h, x : x + w] = 4.0
    output = []
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

    # Close the loop through a white state one band at a time. The app blanks
    # before playback, so white -> frame 0 is also bounded by one band.
    closure_operations = 0
    while state.any():
        ink = np.array([state[y0:y1].sum() for y0, y1 in bands])
        chosen = int(np.argmax(ink))
        if not ink[chosen]:
            break
        y0, y1 = bands[chosen]
        state[y0:y1] = False
        output.append(state.copy())
        closure_operations += 1
    if not output:
        output.append(state.copy())

    compiled = np.stack(output)
    previous = np.zeros_like(compiled[0])
    changed_rows = []
    for frame in compiled:
        changed_rows.append(int(np.count_nonzero(np.any(frame != previous, axis=1))))
        previous = frame
    wrap_rows = int(np.count_nonzero(np.any(compiled[0] != compiled[-1], axis=1)))
    if max(changed_rows + [wrap_rows]) > band_rows or len(compiled) > max_frames:
        raise RuntimeError("Adaptive compiler violated its band/frame bound")
    return compiled, {
        "input_frames": int(len(frames)),
        "consumed_frames": int(len(targets)),
        "output_frames": int(len(compiled)),
        "source_operations": source_operations,
        "closure_operations": closure_operations,
        "band_rows": int(band_rows),
        "panel_band_rows": int(band_rows * 4),
        "mean_changed_rows": float(np.mean(changed_rows)),
        "max_changed_rows": int(max(changed_rows)),
        "wrap_changed_rows": wrap_rows,
        "band_count": int(math.ceil(height / band_rows)),
    }
