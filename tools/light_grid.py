# SPDX-License-Identifier: GPL-3.0-or-later
"""Translate a moving grayscale image into a sparse field of paper apertures."""
import cv2
import numpy as np


def render_light_grid(frames, columns=32, rows=20, persistence=0.18):
    """Return 1=ink masks at 240x156, using cell area to encode source luminance.

    Low-pass filtering precedes sampling. Temporal smoothing is deterministic
    and local to this clip; it does not claim to increase physical refresh rate.
    """
    if (
        not isinstance(frames, np.ndarray)
        or frames.ndim != 3
        or frames.shape[1:] != (156, 240)
        or frames.dtype != np.uint8
        or not 1 <= len(frames) <= 600
    ):
        raise ValueError("Expected uint8 grayscale frames at 240x156")
    if (
        not isinstance(columns, (int, np.integer))
        or not isinstance(rows, (int, np.integer))
        or not 4 <= columns <= 80
        or not 4 <= rows <= 52
        or not 0 <= persistence < 1
    ):
        raise ValueError("Invalid grid or temporal persistence")
    columns, rows = int(columns), int(rows)
    y, x = np.mgrid[:156, :240]
    cell_x = np.minimum(x * columns // 240, columns - 1)
    cell_y = np.minimum(y * rows // 156, rows - 1)
    dx = (x + 0.5) - (cell_x + 0.5) * 240 / columns
    dy = (y + 0.5) - (cell_y + 0.5) * 156 / rows
    distance_squared = dx * dx + dy * dy
    max_radius = min(240 / columns, 156 / rows) * 0.44
    sigma = 0.42 * min(240 / columns, 156 / rows)
    output = np.empty(frames.shape, dtype=np.uint8)
    previous = None
    for index, frame in enumerate(frames):
        soft = cv2.GaussianBlur(frame, (0, 0), sigma)
        light = (
            cv2.resize(soft, (columns, rows), interpolation=cv2.INTER_AREA).astype(
                np.float32
            )
            / 255
        )
        light = np.clip((light - 0.06) / 0.88, 0, 1)
        light = (
            light
            if previous is None
            else (1 - persistence) * light + persistence * previous
        )
        previous = light
        # Radius squared makes the paper aperture's area track brightness.
        level = np.rint(light * 7) / 7
        intensity = level[cell_y, cell_x]
        paper = (intensity > 0) & (
            distance_squared <= max_radius * max_radius * intensity
        )
        output[index] = ~paper
    return output
