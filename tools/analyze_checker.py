#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure visible checker reversals in a fixed camera view; reject absent contrast."""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("video", type=Path)
parser.add_argument(
    "--quad",
    required=True,
    help="Eight comma-separated pixel coordinates: TL, TR, BR, BL of the checker field",
)
parser.add_argument("--expected", type=int, default=60)
args = parser.parse_args()
quad = np.array(list(map(float, args.quad.split(","))), dtype=np.float32).reshape(4, 2)
transform = cv2.getPerspectiveTransform(
    np.float32([[0, 0], [28, 0], [28, 11], [0, 11]]), quad
)
coords = []
signs = []
for row in range(2, 9):
    for col in range(3, 25):
        point = transform @ np.array([col + 0.5, row + 0.5, 1])
        coords.append(tuple(np.rint(point[:2] / point[2]).astype(int)))
        signs.append(1 if (row + col) % 2 else -1)
cap = cv2.VideoCapture(str(args.video))
if not cap.isOpened():
    raise RuntimeError("Could not open recording")
samples = []
while True:
    ok, image = cap.read()
    if not ok:
        break
    time = cap.get(cv2.CAP_PROP_POS_MSEC) / 1000
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    if any(
        x < 2 or y < 2 or x + 2 >= gray.shape[1] or y + 2 >= gray.shape[0]
        for x, y in coords
    ):
        raise ValueError("Checker samples fall outside the recording")
    contrast = sum(
        float(gray[y - 2 : y + 3, x - 2 : x + 3].mean()) * sign
        for (x, y), sign in zip(coords, signs)
    ) / len(coords)
    samples.append((time, contrast))
cap.release()
if len(samples) < 30:
    raise RuntimeError("Recording too short")
samples = np.array(samples)
low, high = np.quantile(samples[:, 1], [0.15, 0.85])
mid, amplitude = (low + high) / 2, (high - low) / 2
changes = []
state = 0
if amplitude >= 5:
    for time, value in samples:
        new = (
            1
            if value > mid + 0.3 * amplitude
            else -1 if value < mid - 0.3 * amplitude else state
        )
        if state and new != state:
            changes.append(float(time))
        state = new
intervals = np.diff(changes)
steady = intervals[(intervals > 0.4) & (intervals < 1.2)]
result = {
    "valid": bool(amplitude >= 5 and len(changes) == args.expected),
    "expected": args.expected,
    "transitions": len(changes),
    "contrast_half_range": float(amplitude),
    "mean_steady_interval_s": float(np.mean(steady)) if len(steady) else None,
    "steady_intervals": len(steady),
    "median_steady_interval_s": float(np.median(steady)) if len(steady) else None,
    "quad": quad.tolist(),
    "transition_times_s": changes,
    "intervals_s": intervals.tolist(),
    "method": "154 fixed 5x5 patches, signed alternating contrast, 15/85 percentiles, 30% hysteresis; excludes >1.2s reopen intervals from steady median",
}
(args.video.parent / "optical.json").write_text(json.dumps(result, indent=2) + "\n")
np.savetxt(
    args.video.parent / "optical-signal.csv",
    samples,
    delimiter=",",
    header="seconds,signed_checker_contrast",
    comments="",
)
print(json.dumps(result))
if not result["valid"]:
    raise SystemExit(
        "Optical validation failed: inspect framing, contrast and transition count"
    )
