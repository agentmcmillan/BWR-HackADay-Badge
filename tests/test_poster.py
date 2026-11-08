# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from poster import render_poster

frames = np.full((3, 156, 240), 210, dtype=np.uint8)
frames[:, 25:150, 75:165] = 45
frames[:, 45:85, 95:145] = 145  # paper face hole must remain open
frames[1, 70, 20] = 0            # isolated codec fleck must disappear
saved = frames.copy()
ink = render_poster(frames)
assert np.array_equal(frames, saved)
assert ink.dtype == np.uint8 and set(np.unique(ink)) <= {0, 1}
assert ink[:, 110:140, 90:150].mean() > 0.98
assert ink[:, 55:75, 105:135].mean() < 0.01
assert ink[:, :20].mean() == 0 and ink[1, 70, 20] == 0
assert np.array_equal(ink, render_poster(frames))

for invalid in [frames.astype(np.float32), frames[0], np.empty((0, 156, 240), np.uint8)]:
    try:
        render_poster(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid poster input accepted")
for kwargs in [
    {"threshold": 0},
    {"threshold": 112.0},
    {"blur_sigma": "soft"},
    {"blur_sigma": np.nan},
    {"minimum_ink_area": -1},
    {"border": 25},
]:
    try:
        render_poster(frames, **kwargs)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid poster setting accepted")

print("PASS poster renderer: stable hard threshold, clean fields, preserved paper holes")
