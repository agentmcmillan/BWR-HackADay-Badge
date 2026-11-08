# SPDX-License-Identifier: GPL-3.0-or-later
"""Temporal ink-state filtering for binary e-paper video."""
import numpy as np


def smooth_ink_release(frames, release_frames=3, cyclic=False):
    """Turn ink on immediately and erase it after sustained paper input.

    ``release_frames`` is the number of consecutive paper frames required
    before a black pixel is released. In cyclic mode, history wraps across the
    clip boundary so playback does not pulse at the loop point.
    """
    if (
        not isinstance(frames, np.ndarray)
        or frames.ndim != 3
        or frames.dtype != np.bool_
        or not 1 <= len(frames) <= 600
        or not isinstance(release_frames, (int, np.integer))
        or not 1 <= release_frames <= 8
        or not isinstance(cyclic, (bool, np.bool_))
    ):
        raise ValueError("Expected 1-600 boolean frames and a release of 1-8 frames")
    if release_frames == 1:
        return frames.copy()

    output = frames.copy()
    for delay in range(1, release_frames):
        if cyclic:
            history = np.roll(frames, delay, axis=0)
        else:
            history = np.zeros_like(frames)
            history[delay:] = frames[:-delay]
        output |= history
    return output


def ink_motion_metrics(frames, cyclic=False):
    """Return compact motion statistics for a boolean frame sequence."""
    if (
        not isinstance(frames, np.ndarray)
        or frames.ndim != 3
        or frames.dtype != np.bool_
        or not 1 <= len(frames) <= 600
        or not isinstance(cyclic, (bool, np.bool_))
    ):
        raise ValueError("Expected 1-600 boolean frames")
    current = frames[1:]
    previous = frames[:-1]
    if cyclic:
        current = np.concatenate((current, frames[:1]), axis=0)
        previous = np.concatenate((previous, frames[-1:]), axis=0)
    additions = current & ~previous
    releases = previous & ~current
    changes = additions | releases
    row_changes = np.any(changes, axis=2) if len(changes) else np.empty((0, frames.shape[1]), bool)
    addition_rows = np.any(additions, axis=2) if len(additions) else row_changes
    release_rows = np.any(releases, axis=2) if len(releases) else row_changes
    return {
        "ink_fraction": float(frames.mean()),
        "pixel_toggles": int(changes.sum()),
        "ink_additions": int(additions.sum()),
        "ink_releases": int(releases.sum()),
        "changed_rows": int(row_changes.sum()),
        "addition_rows": int(addition_rows.sum()),
        "release_rows": int(release_rows.sum()),
        "unchanged_transitions": int(np.count_nonzero(~np.any(changes, axis=(1, 2)))),
        "mean_changed_rows": float(row_changes.sum(axis=1).mean()) if len(row_changes) else 0.0,
    }
