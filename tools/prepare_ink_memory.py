#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare detailed native poses, with optional still holds and physical afterimages."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import zlib
import cv2
import numpy as np
from clip_pack import packbits_envelope

MODEL_SHA256 = "552d8a984054e59b5d773d24b9b12022b22046ceb2bbc4c9aaeaceb36a9ddf24"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("input", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--poses", type=int, default=12)
parser.add_argument("--start", type=float, default=1)
parser.add_argument("--source-step", type=float, default=0.6)
parser.add_argument(
    "--hold",
    type=float,
    default=4,
    help="Seconds of stillness after refresh/shutdown, or 0 for continuous playback",
)
parser.add_argument(
    "--subject-model",
    type=Path,
    help="Optional local OpenCV Zoo PPHumanSeg 2023mar model",
)
parser.add_argument("--packbits", action="store_true")
args = parser.parse_args()
if (
    not args.input.is_file()
    or args.output.exists()
    or not 1 <= args.poses <= 36
    or not math.isfinite(args.start)
    or not 0 <= args.start
    or not 0.1 <= args.source_step <= 30
    or not (args.hold == 0 or 1 <= args.hold <= 30)
):
    parser.error(
        "Use a local file, new output, 1–36 poses, nonnegative start, 0.1–30 source-step and hold 0 or 1–30s"
    )
net = None
if args.subject_model:
    if hashlib.sha256(args.subject_model.read_bytes()).hexdigest() != MODEL_SHA256:
        parser.error("Subject model does not match the documented OpenCV Zoo revision")
    net = cv2.dnn.readNet(str(args.subject_model))
raw = subprocess.run(
    [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-ss",
        str(args.start),
        "-i",
        str(args.input),
        "-an",
        "-vf",
        f"fps={1/args.source_step},scale=960:624:force_original_aspect_ratio=decrease,pad=960:624:(ow-iw)/2:(oh-ih)/2:white",
        "-frames:v",
        str(args.poses),
        "-f",
        "rawvideo",
        "-pix_fmt",
        "bgr24",
        "pipe:1",
    ],
    stdout=subprocess.PIPE,
    check=True,
).stdout
if not raw or len(raw) % (960 * 624 * 3):
    raise RuntimeError("Incomplete native pose stream")
frames = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 624, 960, 3)
# Fixed fine stipple: no large dot grid, and the grain stays put between poses.
rng = np.random.default_rng(20261108)
noise = rng.random((624, 960)).astype(np.float32)
noise -= cv2.GaussianBlur(noise, (0, 0), 1)
rank = np.empty(noise.size, dtype=np.float32)
rank[np.argsort(noise, axis=None)] = np.arange(noise.size, dtype=np.float32)
threshold = (rank.reshape(624, 960) + 0.5) * (255 / noise.size)
packed = []
for frame in frames:
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
    if net is not None:
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        image = cv2.resize(rgb, (192, 192)).astype(np.float32) / 127.5 - 1
        net.setInput(cv2.dnn.blobFromImage(image))
        prediction = net.forward()
        if (
            prediction.ndim != 4
            or prediction.shape[:2] != (1, 2)
            or not np.isfinite(prediction).all()
        ):
            raise RuntimeError("Unexpected segmentation model output")
        alpha = cv2.resize(prediction[0, 1], (960, 624), interpolation=cv2.INTER_LINEAR)
        alpha = cv2.GaussianBlur(np.clip(alpha, 0, 1), (0, 0), 2)
        gray = 255 - (255 - gray) * alpha
    gray = np.clip((gray - 24) * 255 / 220, 0, 255)
    packed.append(np.packbits(gray < threshold, axis=1, bitorder="big").tobytes())
payload = b"".join(packed)
header = struct.pack(
    "<8sHHIIIII",
    b"BWRINK1\0",
    960,
    624,
    len(packed),
    round(args.hold * 1000),
    zlib.crc32(payload),
    0,
    0,
)
output = header + payload
if args.packbits:
    output = packbits_envelope(output)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(output)
print(
    json.dumps(
        {
            "poses": len(packed),
            "resolution": [960, 624],
            "hold_ms": round(args.hold * 1000),
            "bytes": len(output),
            "payload_crc32": f"{zlib.crc32(payload):08x}",
        }
    )
)
