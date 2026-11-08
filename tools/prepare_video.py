#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert a local video into a bounded monochrome RAM clip; never fetch a URL."""
import argparse
import json
import re
from pathlib import Path
import struct
import subprocess
import zlib
import numpy as np
from clip_pack import packbits_envelope


def parse_grid(value):
    match = re.fullmatch(r"([0-9]{1,2})x([0-9]{1,2})", value)
    if not match:
        raise argparse.ArgumentTypeError("Use a grid such as 32x20")
    columns, rows = map(int, match.groups())
    if not 4 <= columns <= 80 or not 4 <= rows <= 52:
        raise argparse.ArgumentTypeError("Grid must be 4–80 columns and 4–52 rows")
    return columns, rows


parser = argparse.ArgumentParser()
parser.add_argument("input", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--fps", type=float, default=1.5)
parser.add_argument("--frames", type=int, default=600)
parser.add_argument("--style", choices=("dither", "light-grid"), default="dither")
parser.add_argument(
    "--grid",
    type=parse_grid,
    default=(32, 20),
    metavar="COLSxROWS",
    help="Sampling grid for --style light-grid (default: 32x20)",
)
parser.add_argument(
    "--packbits",
    action="store_true",
    help="PackBits envelope for a private built-in asset",
)
args = parser.parse_args()
if not args.input.is_file() or not 0.5 <= args.fps <= 30 or not 1 <= args.frames <= 600:
    parser.error("Use an existing local file, 0.5–30 fps, and 1–600 frames.")
if args.output.exists():
    parser.error("Output already exists; choose a new filename.")
# A fixed ordered mask avoids independent random noise between video frames.
bayer = np.array([[0]], dtype=np.float64)
for _ in range(3):
    bayer = np.block([[4 * bayer, 4 * bayer + 2], [4 * bayer + 3, 4 * bayer + 1]])
threshold = np.tile((bayer + 0.5) * 4, (20, 30))[:156, :240]
command = [
    "ffmpeg",
    "-hide_banner",
    "-loglevel",
    "error",
    "-i",
    str(args.input),
    "-an",
    "-vf",
    f"fps={args.fps},scale=240:156:force_original_aspect_ratio=decrease,pad=240:156:(ow-iw)/2:(oh-ih)/2:white,format=gray",
    "-frames:v",
    str(args.frames),
    "-f",
    "rawvideo",
    "-pix_fmt",
    "gray",
    "pipe:1",
]
raw = subprocess.run(command, check=True, stdout=subprocess.PIPE).stdout
if not raw or len(raw) % (240 * 156):
    raise RuntimeError("Incomplete decoded frame stream")
frames = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 156, 240)
if args.style == "light-grid":
    from light_grid import render_light_grid

    ink = render_light_grid(frames, columns=args.grid[0], rows=args.grid[1])
else:
    ink = frames < threshold
payload = np.packbits(ink, axis=2, bitorder="big").tobytes()
header = struct.pack(
    "<8sHHIIIII",
    b"BWRVID1\0",
    240,
    156,
    len(frames),
    round(args.fps * 1000),
    zlib.crc32(payload),
    0,
    0,
)
args.output.parent.mkdir(parents=True, exist_ok=True)
output = header + payload
if args.packbits:
    output = packbits_envelope(output)
args.output.write_bytes(output)
print(
    json.dumps(
        {
            "frames": len(frames),
            "style": args.style,
            "grid": args.grid if args.style == "light-grid" else None,
            "sample_fps": args.fps,
            "bytes": len(output),
            "crc32": f"{zlib.crc32(payload):08x}",
        }
    )
)
