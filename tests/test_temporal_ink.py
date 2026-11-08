# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from temporal_ink import ink_motion_metrics, smooth_ink_release

frames = np.zeros((7, 2, 3), dtype=np.bool_)
frames[0, 0, 0] = True
frames[2, 0, 1] = True
frames[4:, 1, 2] = True
saved = frames.copy()
smoothed = smooth_ink_release(frames, release_frames=3)
assert np.array_equal(frames, saved)
assert np.array_equal(smoothed[:, 0, 0], [1, 1, 1, 0, 0, 0, 0])
assert np.array_equal(smoothed[:, 0, 1], [0, 0, 1, 1, 1, 0, 0])
assert np.array_equal(smoothed[:, 1, 2], [0, 0, 0, 0, 1, 1, 1])
assert np.array_equal(smooth_ink_release(frames, 1), frames)

cyclic = smooth_ink_release(frames, release_frames=3, cyclic=True)
assert cyclic[0, 1, 2] and cyclic[1, 1, 2]
before = ink_motion_metrics(frames, cyclic=True)
after = ink_motion_metrics(cyclic, cyclic=True)
assert before["pixel_toggles"] == after["pixel_toggles"]
assert before["ink_additions"] == before["ink_releases"]
assert after["ink_additions"] == after["ink_releases"]
assert after["ink_fraction"] > before["ink_fraction"]
assert before["changed_rows"] == after["changed_rows"]

for invalid in [frames.astype(np.uint8), frames[0], np.empty((0, 2, 3), dtype=np.bool_)]:
    try:
        smooth_ink_release(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid temporal input accepted")
for release in [0, 9, 3.0]:
    try:
        smooth_ink_release(frames, release)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid temporal release accepted")

print("PASS temporal ink: immediate black attack, delayed white release and cyclic history")
