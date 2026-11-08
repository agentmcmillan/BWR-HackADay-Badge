# SPDX-License-Identifier: GPL-3.0-or-later
"""High-contrast, dither-free rendering for fast e-paper motion studies."""
import cv2
import numpy as np


def render_poster(frames, threshold=112, blur_sigma=0.8, minimum_ink_area=4, border=8):
    """Return 1=ink poster masks for uint8 240x156 grayscale frames.

    A fixed clip-wide threshold avoids temporal pumping. Tiny disconnected ink
    flecks are removed without closing paper holes in faces or shirt details.
    """
    if (
        not isinstance(frames, np.ndarray)
        or frames.ndim != 3
        or frames.shape[1:] != (156, 240)
        or frames.dtype != np.uint8
        or not 1 <= len(frames) <= 600
        or not isinstance(threshold, (int, np.integer))
        or not 1 <= threshold <= 254
        or not isinstance(blur_sigma, (int, float, np.integer, np.floating))
        or not np.isfinite(blur_sigma)
        or not 0.4 <= blur_sigma <= 5
        or not isinstance(minimum_ink_area, (int, np.integer))
        or not 0 <= minimum_ink_area <= 64
        or not isinstance(border, (int, np.integer))
        or not 0 <= border <= 24
    ):
        raise ValueError("Expected 1-600 uint8 240x156 frames and bounded poster settings")

    output = np.empty(frames.shape, dtype=np.uint8)
    for index, frame in enumerate(frames):
        soft = cv2.GaussianBlur(frame, (0, 0), float(blur_sigma))
        ink = (soft < int(threshold)).astype(np.uint8)
        # Scaling old video can retain narrow black pillarbox edges. They are
        # framing, not subject motion, and otherwise waste adaptive pulls.
        if border:
            ink[:, :border] = 0
            ink[:, -border:] = 0
        if minimum_ink_area:
            count, labels, stats, _ = cv2.connectedComponentsWithStats(ink, connectivity=8)
            keep = np.zeros(count, dtype=np.uint8)
            keep[stats[:, cv2.CC_STAT_AREA] >= int(minimum_ink_area)] = 1
            keep[0] = 0
            ink = keep[labels]
        output[index] = ink
    return output
