# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import sys
import numpy as np

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools"))
from adaptive_video import adaptive_band_frames


frames = np.zeros((18, 24, 32), dtype=np.bool_)
for index in range(len(frames)):
    frames[index, 4:9, 2 + index % 16 : 8 + index % 16] = True
    frames[index, 15:21, 20:28] = index % 3 != 0

compiled, report = adaptive_band_frames(
    frames, band_rows=4, max_age=5, motion_weight=3, focus=(0, 0, 20, 14)
)
assert report["input_frames"] == 18
assert report["panel_band_rows"] == 16
assert report["output_frames"] == len(compiled) <= 600
assert report["source_operations"] > 0 and report["closure_operations"] > 0
assert not compiled[-1].any()

previous = np.zeros_like(compiled[0])
for frame in compiled:
    changed = np.flatnonzero(np.any(frame != previous, axis=1))
    assert len(changed) <= 4
    if len(changed):
        assert changed[-1] - changed[0] < 4
    previous = frame
wrap = np.flatnonzero(np.any(compiled[0] != compiled[-1], axis=1))
assert len(wrap) <= 4

repeat, repeat_report = adaptive_band_frames(
    frames, band_rows=4, max_age=5, motion_weight=3, focus=(0, 0, 20, 14)
)
assert np.array_equal(compiled, repeat) and report == repeat_report

try:
    adaptive_band_frames(frames.astype(np.uint8), band_rows=4)
    raise AssertionError("non-boolean input accepted")
except ValueError:
    pass
try:
    adaptive_band_frames(frames, band_rows=4, focus=(30, 20, 4, 5))
    raise AssertionError("out-of-bounds focus accepted")
except ValueError:
    pass
try:
    adaptive_band_frames(frames, band_rows=24, max_frames=1)
    raise AssertionError("impossible frame budget accepted")
except ValueError:
    pass

print("PASS adaptive video: deterministic motion/age scheduling, narrow transitions, bounded cyclic closure")
