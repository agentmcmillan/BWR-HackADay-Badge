#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Match sampled camera frames against a private clip without exporting its images."""
import argparse
import json
from pathlib import Path
import struct
import cv2
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("clip", type=Path)
parser.add_argument("recording", type=Path)
parser.add_argument(
    "--quad",
    required=True,
    help="TL,TR,BR,BL camera coordinates of the video area, excluding status bar",
)
parser.add_argument("--times", default="10,30,60,90,120,150,180,210")
args = parser.parse_args()
data = args.clip.read_bytes()
magic, w, h, count, *_ = struct.unpack("<8sHHIIIII", data[:32])
if magic != b"BWRVID1\0" or (w, h) != (240, 156) or len(data) != 32 + count * 4680:
    raise ValueError("Invalid reference clip")
reference = 1 - np.unpackbits(
    np.frombuffer(data[32:], dtype=np.uint8), bitorder="big"
).reshape(count, h, w)
reference = np.stack(
    [cv2.GaussianBlur(frame.astype(np.float32), (0, 0), 1.2) for frame in reference]
)
reference = reference[:, 2:-2:2, 2:-2:2].reshape(count, -1).astype(np.float32)
reference -= reference.mean(axis=1, keepdims=True)
norm = np.linalg.norm(reference, axis=1, keepdims=True)
reference /= np.maximum(norm, 1e-6)
quad = np.array(list(map(float, args.quad.split(","))), dtype=np.float32).reshape(4, 2)
transform = cv2.getPerspectiveTransform(
    quad, np.float32([[0, 0], [240, 0], [240, 156], [0, 156]])
)
cap = cv2.VideoCapture(str(args.recording))
if not cap.isOpened():
    raise ValueError("Could not open recording")
results = []
for seconds in map(float, args.times.split(",")):
    cap.set(cv2.CAP_PROP_POS_MSEC, seconds * 1000)
    ok, image = cap.read()
    if not ok:
        continue
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    sample = (
        cv2.GaussianBlur(cv2.warpPerspective(gray, transform, (240, 156)), (0, 0), 1.2)[
            2:-2:2, 2:-2:2
        ]
        .reshape(-1)
        .astype(np.float32)
    )
    sample -= sample.mean()
    scores = np.einsum(
        "ij,j->i",
        reference,
        sample / max(float(np.linalg.norm(sample)), 1e-6),
        optimize=False,
    )
    if not np.isfinite(scores).all():
        raise RuntimeError("Non-finite optical correlation")
    index = int(np.argmax(scores))
    results.append(
        {
            "camera_seconds": seconds,
            "best_source_frame": index,
            "correlation": float(scores[index]),
        }
    )
cap.release()
summary = {
    "samples": results,
    "method": "Perspective-corrected grayscale with 1.2-pixel Gaussian blur, every second interior pixel, normalized correlation against all reference frames; not a bit-perfect pixel test",
}
(args.recording.parent / "video-optical.json").write_text(
    json.dumps(summary, indent=2) + "\n"
)
print(json.dumps(summary))
