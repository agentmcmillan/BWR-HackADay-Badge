# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import ast
import argparse
import re
import sys
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from light_grid import render_light_grid
from poster import render_poster

black = np.zeros((2, 156, 240), dtype=np.uint8)
white = np.full_like(black, 255)
assert render_light_grid(black).all()
# Some grids align a circle center exactly with a raster pixel: zero light must
# still produce zero paper aperture, rather than a stray pinhole.
assert render_light_grid(black, columns=80, rows=52).all()
bright = render_light_grid(white)
assert 0.3 < (bright == 0).mean() < 0.7
source = white.copy()
source[:, 40:116, 80:160] = 0
saved = source.copy()
mask = render_light_grid(source)
assert (
    np.array_equal(source, saved)
    and mask.dtype == np.uint8
    and set(np.unique(mask)) <= {0, 1}
)
assert (mask[:, 55:100, 95:145] == 0).mean() < 0.03
assert np.array_equal(mask, render_light_grid(source))
assert np.array_equal(
    mask, render_light_grid(source, columns=np.uint64(32), rows=np.int64(20))
)
transition = np.stack([white[0], black[0]])
assert (render_light_grid(transition, persistence=0.5)[1] == 0).any()
assert render_light_grid(transition, persistence=0)[1].all()
for kwargs in [
    {"columns": 0},
    {"rows": 53},
    {"columns": 32.5},
    {"rows": 20.0},
    {"persistence": 1},
    {"persistence": float("nan")},
]:
    try:
        render_light_grid(source, **kwargs)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid setting accepted")
for invalid in [
    source.astype(np.float32),
    source[0],
    np.empty((0, 156, 240), dtype=np.uint8),
    np.empty((601, 156, 240), dtype=np.uint8),
]:
    try:
        render_light_grid(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid frame sequence accepted")

# Verify the actual converter's bounded CLI parser without importing its main.
root = Path(__file__).resolve().parents[1]
parsed = ast.parse((root / "tools/prepare_video.py").read_text())
function = next(
    n for n in parsed.body if isinstance(n, ast.FunctionDef) and n.name == "parse_grid"
)
namespace = {"re": re, "argparse": argparse}
exec(
    compile(ast.Module(body=[function], type_ignores=[]), "<grid CLI parser>", "exec"),
    namespace,
)
parse = namespace["parse_grid"]
assert parse("32x20") == (32, 20) and parse("80x52") == (80, 52)
for value in [
    "3x20",
    "81x20",
    "32x53",
    "32x0",
    "32.0x20",
    "32x20 extra",
    "99999999x20",
]:
    try:
        parse(value)
    except argparse.ArgumentTypeError:
        pass
    else:
        raise AssertionError("Invalid grid accepted")
print(
    "PASS light-grid: bounded deterministic masks, immutable source, dark/bright response, zero-light pinholes, temporal smoothing and CLI bounds"
)

# Run the converter with deterministic decoded frames to verify that its style
# selection, existing file header, CRC and MSB-first payload stay compatible.
import contextlib
import io
import json
import runpy
import struct
import tempfile
from unittest.mock import patch
import zlib

raw = np.stack([black[0], white[0]])
with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    source_file = folder / "source.local"
    source_file.touch()
    for style in ["dither", "light-grid", "poster"]:
        output = folder / (style + ".bwrv")
        arguments = [
            "prepare_video.py",
            str(source_file),
            str(output),
            "--style",
            style,
            "--grid",
            "32x20",
            "--frames",
            "2",
        ]

        def decode(command, **kwargs):
            assert command[0] == "ffmpeg" and kwargs["check"]
            return type("Decoded", (), {"stdout": raw.tobytes()})()

        stdout = io.StringIO()
        with patch.object(sys, "argv", arguments), patch(
            "subprocess.run", decode
        ), contextlib.redirect_stdout(stdout):
            runpy.run_path(str(root / "tools/prepare_video.py"), run_name="__main__")
        data = output.read_bytes()
        magic, w, h, n, fps, crc, flags, reserved = struct.unpack(
            "<8sHHIIIII", data[:32]
        )
        assert (magic, w, h, n, fps, flags, reserved) == (
            b"BWRVID1\0",
            240,
            156,
            2,
            1500,
            0,
            0,
        )
        assert len(data) == 32 + 2 * 4680 and zlib.crc32(data[32:]) == crc
        if style == "dither":
            assert (
                data[32 : 32 + 4680] == b"\xff" * 4680
                and data[32 + 4680 :] == b"\x00" * 4680
            )
        elif style == "light-grid":
            expected = np.packbits(
                render_light_grid(raw), axis=2, bitorder="big"
            ).tobytes()
            assert data[32:] == expected
        else:
            expected = np.packbits(
                render_poster(raw), axis=2, bitorder="big"
            ).tobytes()
            assert data[32:] == expected
        assert json.loads(stdout.getvalue())["style"] == style
print(
    "PASS converter style integration: BWRVID1 header/CRC and MSB-first dither, light-grid and poster payloads"
)
