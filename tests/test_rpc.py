# SPDX-License-Identifier: GPL-3.0-or-later
"""Serial timeouts may return a partial line; only parse newline-terminated frames."""
import ast
import base64
import importlib.util
import io
import json
from pathlib import Path
import sys
import types
import time

root = Path(__file__).resolve().parents[1]
sys.modules["serial"] = types.SimpleNamespace()
spec = importlib.util.spec_from_file_location("badge_rpc", root / "tools/badge_rpc.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Port:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.writes = []

    def readline(self):
        return self.chunks.pop(0) if self.chunks else b""

    def write(self, bytes):
        self.writes.append(bytes)

    def flush(self):
        pass


body = json.dumps({"active": True, "frame": 323, "payload": "x" * 700})
frame = b"@BC1 1 200 " + base64.b64encode(body.encode()) + b"\n"
chunks = [frame[:256], b"", frame[256:512], b"", frame[512:-1], frame[-1:]]
rpc = module.BadgeRPC.__new__(module.BadgeRPC)
rpc.serial = Port(chunks)
rpc.log = io.StringIO()
rpc.timed = io.StringIO()
rpc.rx_pending = bytearray()
rpc.sequence = 0
rpc.session = "local-test"
rpc.fault = False
rpc.home_ready = False
assert rpc.call("video_status") == json.loads(body)
assert len(rpc.serial.writes) == 1 and not rpc.rx_pending
assert len(rpc.log.getvalue().splitlines()) == 1

# Exercise the actual standalone refresh reader against the same timeout fragments.
source = ast.parse((root / "tools/auto_refresh.py").read_text())
reader = next(
    node
    for node in source.body
    if isinstance(node, ast.FunctionDef) and node.name == "line_read"
)
namespace = {
    "s": Port(chunks),
    "fault": False,
    "home_ready": False,
    "rx_pending": bytearray(),
    "log": io.StringIO(),
    "timed": io.StringIO(),
    "json": json,
    "time": time,
}
exec(
    compile(
        ast.Module(body=[reader], type_ignores=[]), "<actual refresh reader>", "exec"
    ),
    namespace,
)
for _ in range(5):
    assert namespace["line_read"]() == ""
assert namespace["line_read"]() == frame.decode().strip()
assert not namespace["rx_pending"]
namespace["s"] = Port([b"Panel BUSY timeout\n"])
try:
    namespace["line_read"]()
except RuntimeError:
    pass
else:
    raise AssertionError("Panel fault must fail-stop")
assert namespace["fault"]
print(
    "PASS USB host framing: fragmented 256-byte replies, empty timeout reads, complete-line decoding, one request only, and fault detection"
)
