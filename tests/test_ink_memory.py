# SPDX-License-Identifier: GPL-3.0-or-later
"""Native converter and host parser checks; no video, serial device or camera needed."""
import argparse
import ast
import contextlib
import io
import json
from pathlib import Path
import runpy
import struct
import sys
import tempfile
from unittest.mock import patch
import zlib
import numpy as np

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools"))
raw = np.full((2, 624, 960, 3), 255, dtype=np.uint8)
raw[:, 70:540, 479:480] = 0  # A single native-pixel-wide detail must survive.
raw[:, 100:500, 200:260] = 0
saved = raw.copy()


def unpack(envelope):
    magic, size, crc = struct.unpack("<8sII", envelope[:16])
    assert magic == b"BWRPACK1"
    output = bytearray()
    i = 16
    while i < len(envelope):
        code = envelope[i]
        i += 1
        assert code != 128
        if code < 128:
            output.extend(envelope[i : i + code + 1])
            i += code + 1
        else:
            output.extend(envelope[i : i + 1] * (257 - code))
            i += 1
    assert len(output) == size and zlib.crc32(output) == crc
    return bytes(output)


with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    source = folder / "source.local"
    source.touch()
    outputs = []
    for packed, hold_ms in [(False, 4000), (True, 4000), (False, 0)]:
        output = folder / (
            ("clip.bwrp" if packed else "clip.bwrink")
            if hold_ms
            else "continuous.bwrink"
        )
        arguments = [
            "prepare_ink_memory.py",
            str(source),
            str(output),
            "--poses",
            "2",
            "--hold",
            str(hold_ms / 1000),
        ] + (["--packbits"] if packed else [])

        def decode(command, **kwargs):
            assert command[0] == "ffmpeg" and "-an" in command and kwargs["check"]
            assert command[command.index("-frames:v") + 1] == "2"
            return type("Decoded", (), {"stdout": raw.tobytes()})()

        stdout = io.StringIO()
        with patch.object(sys, "argv", arguments), patch(
            "subprocess.run", decode
        ), contextlib.redirect_stdout(stdout):
            runpy.run_path(
                str(root / "tools/prepare_ink_memory.py"), run_name="__main__"
            )
        data = unpack(output.read_bytes()) if packed else output.read_bytes()
        magic, w, h, count, hold, crc, flags, reserved = struct.unpack(
            "<8sHHIIIII", data[:32]
        )
        assert (magic, w, h, count, hold, flags, reserved) == (
            b"BWRINK1\0",
            960,
            624,
            2,
            hold_ms,
            0,
            0,
        )
        assert len(data) == 32 + count * 74880 and zlib.crc32(data[32:]) == crc
        mask = np.unpackbits(
            np.frombuffer(data[32:], dtype=np.uint8), bitorder="big"
        ).reshape(2, 624, 960)
        assert mask[:, 70:540, 479].all() and mask[:, 100:500, 200:260].all()
        assert not mask[:, 0:60].any()
        assert np.array_equal(mask[0], mask[1]) and np.array_equal(raw, saved)
        assert json.loads(stdout.getvalue())["hold_ms"] == hold_ms
        outputs.append(data)
    assert outputs[0] == outputs[1]
    for option, value in [
        ("--start", "inf"),
        ("--start", "nan"),
        ("--source-step", "inf"),
        ("--hold", "nan"),
        ("--hold", "inf"),
        ("--hold", "0.5"),
        ("--hold", "-1"),
        ("--poses", "37"),
    ]:
        arguments = [
            "prepare_ink_memory.py",
            str(source),
            str(folder / "invalid.bwrink"),
            option,
            value,
        ]
        with patch.object(sys, "argv", arguments), patch(
            "subprocess.run", side_effect=AssertionError("Invalid CLI reached decoder")
        ), contextlib.redirect_stderr(io.StringIO()):
            try:
                runpy.run_path(
                    str(root / "tools/prepare_ink_memory.py"), run_name="__main__"
                )
            except SystemExit as error:
                assert error.code == 2
            else:
                raise AssertionError("Invalid native CLI accepted")

# Execute the actual host parser and identity checker without running its CLI.
module = ast.parse((root / "tools/video_benchmark.py").read_text())
functions = [
    node
    for node in module.body
    if isinstance(node, ast.FunctionDef)
    and node.name in ["parse_clip", "verify_builtin_clip"]
]
namespace = {"struct": struct, "zlib": zlib}
exec(
    compile(
        ast.Module(body=functions, type_ignores=[]), "<actual clip validators>", "exec"
    ),
    namespace,
)
parse = namespace["parse_clip"]
verify = namespace["verify_builtin_clip"]
data = outputs[0]
native, count, crc, hold = parse(data)
assert native and count == 2 and hold == 4000
assert parse(outputs[2]) == (True, 2, crc, 0)
legacy_payload = b"\x01" * 4680
legacy = (
    struct.pack(
        "<8sHHIIIII", b"BWRVID1\0", 240, 156, 1, 1500, zlib.crc32(legacy_payload), 0, 0
    )
    + legacy_payload
)
assert parse(legacy) == (False, 1, zlib.crc32(legacy_payload), 0)
for offset, value in [
    (0, ord("X")),
    (8, 1),
    (12, 37),
    (16, 0),
    (17, 0),
    (24, 1),
    (28, 1),
    (40, 123),
]:
    invalid = bytearray(data)
    if offset == 16:
        invalid[16:20] = struct.pack("<I", 999)
    elif offset == 17:
        invalid[16:20] = struct.pack("<I", 30001)
    else:
        invalid[offset] = value
    try:
        parse(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid native header/payload accepted")
for invalid in [data[:31], data[:-1], data + b"\x00"]:
    try:
        parse(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid exact size accepted")
file_crc = zlib.crc32(data)
verify(
    {"loaded": count, "clip_crc32": crc, "clip_file_crc32": file_crc},
    count,
    crc,
    file_crc,
)
other = bytearray(data)
other[16:20] = struct.pack("<I", 5000)
assert parse(other)[2] == crc
try:
    verify(
        {"loaded": count, "clip_crc32": crc, "clip_file_crc32": zlib.crc32(other)},
        count,
        crc,
        file_crc,
    )
except RuntimeError:
    pass
else:
    raise AssertionError(
        "Same pixels but different hold metadata accepted as matching asset"
    )
print(
    "PASS native poses: full-resolution detail, deterministic stipple, PackBits/CRC, finite CLI bounds, strict host parsing and whole-file identity"
)

# Controller cleanup must preserve ink only on success, never on a partial hold
# cancellation or a camera that exits mid-run. Every dependency below is mocked.
import datetime
import subprocess
import time
import types

controller_source = (root / "tools/video_benchmark.py").read_text()
for failure in ["camera", "cancelled", "success"]:
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        clip_file = folder / "native.bwrink"
        clip_file.write_bytes(data)
        state = {"commands": [], "closed": False, "sequence": 0}

        class RPC:
            def __init__(self, port, out):
                self.session = "local-test"
                self.fault = False

            def connect(self, version):
                pass

            def call(self, verb, *args):
                state["commands"].append(verb)
                if verb == "video_run":
                    state["sequence"] = 1
                if verb == "video_status":
                    return {
                        "active": True,
                        "sequence": state["sequence"],
                        "running": False,
                        "frame": 2,
                        "calls": 2,
                        "cancelled": failure == "cancelled",
                    }
                return "ok"

            def close(self):
                state["closed"] = True

        class Camera:
            def __init__(self, *args, **kwargs):
                self.polls = 0
                self.done = False

            def poll(self):
                self.polls += 1
                return (
                    0
                    if self.done or (failure == "camera" and self.polls >= 2)
                    else None
                )

            def send_signal(self, signal):
                self.done = True

            def wait(self, timeout):
                return 0

            def kill(self):
                self.done = True

        args = [
            "video_benchmark.py",
            "fake-port",
            str(clip_file),
            "--clocks",
            "4",
            "--leave-ink",
        ]
        stdout = io.StringIO()
        with patch.object(sys, "argv", args), patch.dict(
            sys.modules, {"badge_rpc": types.SimpleNamespace(BadgeRPC=RPC)}
        ), patch("subprocess.Popen", Camera), patch(
            "time.sleep", lambda seconds: None
        ), contextlib.redirect_stdout(
            stdout
        ):
            try:
                exec(
                    compile(
                        controller_source,
                        str(root / "tools/video_benchmark.py"),
                        "exec",
                    ),
                    {
                        "__name__": "__main__",
                        "__file__": str(folder / "tools/video_benchmark.py"),
                    },
                )
            except RuntimeError:
                assert failure != "success"
            else:
                assert failure == "success"
        result_path = next((folder / "artifacts/video").glob("*/*/result.json"))
        result = json.loads(result_path.read_text())
        assert result["status"] == ("completed" if failure == "success" else "error")
        assert state["closed"]
        assert ("video_exit" in state["commands"]) == (failure != "success")
        if failure != "success":
            assert result.get("error")
print(
    "PASS native controller: camera-loss and cancelled-hold errors close safely; only successful --leave-ink preserves the final pose"
)
