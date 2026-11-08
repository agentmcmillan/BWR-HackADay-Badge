# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep the public TCON runner bounded to the documented experiment matrix."""
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import types


root = Path(__file__).resolve().parents[1]
sys.modules["badge_rpc"] = types.SimpleNamespace(BadgeRPC=object)
spec = importlib.util.spec_from_file_location("tcon_lab", root / "tools/tcon_lab.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class RPC:
    def __init__(self):
        self.calls = []

    def call(self, *args, **kwargs):
        self.calls.append((args, kwargs))
        return {"ok": True}


assert module.SCAN_ROWS == (16, 32, 64, 128, 256, 384, 624)
assert len(module.SCAN) == 3 * len(module.SCAN_ROWS)
assert all(end == 0xA9 and 16 <= rows <= 624 for end, rows in module.SCAN)
assert module.HOLD == [(16, 8), (8, 8), (4, 8), (2, 8), (1, 8), (1, 120)]
assert module.PINGPONG == [(16, 120), (8, 120), (4, 120), (2, 120), (1, 120)]
assert module.EDGE == [
    ("scan_pingpong", 1, 120),
    ("scan_pingpong_edge", 1, 120),
    ("scan_pingpong", 16, 120),
    ("scan_pingpong_edge", 16, 120),
]
assert {end for end, _ in module.DRFW} == {0xA8, 0xA9}
assert {value for value, _ in module.DCTL} == {-1, 0, 1, 8, 9}
assert all(16 <= rows <= 624 and rows % 16 == 0 for _, _, rows in module.TCON)

rpc = RPC()
module.call_profile(rpc, "scan", (0xA9, 16))
assert rpc.calls == [(('drfw_trial', '0xa9', 16), {'timeout': 20})]
rpc.calls.clear()
module.call_profile(rpc, "hold", (1, 120))
assert rpc.calls == [(('scan_hold', 1, 120), {'timeout': 30})]
rpc.calls.clear()
module.call_profile(rpc, "pingpong", (1, 120))
assert rpc.calls == [(('scan_pingpong', 1, 120), {'timeout': 30})]
rpc.calls.clear()
module.call_profile(rpc, "edge", ("scan_pingpong_edge", 1, 120))
assert rpc.calls == [(('scan_pingpong_edge', 1, 120), {'timeout': 30})]

otp = json.loads((root / "data/tcon-otp-b1-2026-11-08.json").read_text())
otp_bytes = bytes.fromhex(otp["hex"])
assert len(otp_bytes) == otp["bytes"] == 128
assert hashlib.sha256(otp_bytes).hexdigest() == otp["sha256"]
assert hashlib.sha256(otp_bytes[:88]).hexdigest() == otp["prefix_88_sha256"]
assert otp_bytes[0x16] == 0x3B and otp_bytes[0x0D] == 0x3B
assert otp_bytes[0x28] & 0x80
identity = otp["identity_ascii"].encode()
offset = otp["identity_offset"]
assert otp_bytes[offset : offset + len(identity)] == identity
assert otp_bytes[88:offset] == b"\xff" * (offset - 88)
assert otp_bytes[offset + len(identity) :] == b"\xff" * (128 - offset - len(identity))

with (root / "data/tcon-held-scan-2026-11-08.csv").open(newline="") as source:
    held = list(csv.DictReader(source))
sustained = held[-1]
assert sustained["rows"] == "1" and sustained["pulls"] == "120"
assert abs(float(sustained["held_call_hz"]) - 120_000_000 / 6_812_503) < 0.0001

with (root / "data/tcon-pingpong-2026-11-08.csv").open(newline="") as source:
    pingpong = list(csv.DictReader(source))
one_row = [row for row in pingpong if row["rows"] == "1"]
assert len(one_row) == 2 and all(row["pulls"] == "120" for row in one_row)
one_row_us = sum(int(row["total_us"]) for row in one_row)
assert abs(240_000_000 / one_row_us - 23.0463844) < 0.0001
optical = [row for row in pingpong if row["optical_status"].startswith("confirmed")]
assert len(optical) == 1
assert optical[0]["rows"] == "16" and optical[0]["pulls"] == "120"
assert abs(float(optical[0]["held_trigger_hz"]) - 120_000_000 / 6_667_570) < 0.0001

with (root / "data/tcon-edge-wait-2026-11-08.csv").open(newline="") as source:
    edge_wait = list(csv.DictReader(source))
full_edge = [
    row for row in edge_wait if row["run"] == "132236" and row["pulls"] == "120"
]
assert len(full_edge) == 4
one_edge = next(
    row
    for row in full_edge
    if row["verb"].endswith("_edge") and row["rows"] == "1"
)
assert abs(float(one_edge["trigger_hz"]) - 120_000_000 / 4_996_143) < 0.0001
assert int(one_edge["max_assert_us"]) <= 135
edge_optical = [
    row for row in edge_wait if row["optical_status"].startswith("confirmed")
]
assert len(edge_optical) == 1
assert edge_optical[0]["rows"] == "16" and edge_optical[0]["pulls"] == "120"
assert abs(float(edge_optical[0]["trigger_hz"]) - 120_000_000 / 6_440_927) < 0.0001

with (root / "data/tcon-register-trials-2026-11-08.csv").open(newline="") as source:
    register_trials = list(csv.DictReader(source))
dctl_one = [
    row
    for row in register_trials
    if row["family"] == "dctl_screen" and row["setting"] == "0x01"
]
assert len(dctl_one) == 1 and dctl_one[0]["samples"] == "1"
assert dctl_one[0]["mean_pull_us"] == "145301"
print("PASS TCON lab suites are fixed, bounded, and use the expected RPC verb")
