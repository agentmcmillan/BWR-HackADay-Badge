#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Upload a RAM clip and run finite video benchmarks with private camera recordings."""
import argparse
import base64
import datetime
import json
from pathlib import Path
import signal
import struct
import subprocess
import time
import zlib
from badge_rpc import BadgeRPC

parser = argparse.ArgumentParser()
parser.add_argument("port")
parser.add_argument("clip", type=Path)
parser.add_argument("--firmware-version", default="0.8.34")
parser.add_argument("--clocks", default="4,20")
parser.add_argument("--frames", type=int)
parser.add_argument("--camera", default="0")
parser.add_argument("--review", action="store_true")
parser.add_argument(
    "--builtin",
    action="store_true",
    help="Use the matching clip already built into private firmware",
)
args = parser.parse_args()
data = args.clip.read_bytes()
if len(data) < 32:
    parser.error("Truncated clip")
magic, w, h, count, fps, crc, flags, reserved = struct.unpack("<8sHHIIIII", data[:32])
if (
    (magic, w, h) != (b"BWRVID1\0", 240, 156)
    or not 1 <= count <= 600
    or len(data) != 32 + count * 4680
    or not 500 <= fps <= 30000
    or flags
    or reserved
    or zlib.crc32(data[32:]) != crc
):
    parser.error("Invalid BWR clip")
clocks = list(map(int, args.clocks.split(",")))
frames = count if args.frames is None else args.frames
if (
    not 1 <= frames <= count
    or not clocks
    or any(clock not in (4, 5, 8, 12, 20) for clock in clocks)
):
    parser.error("Invalid bounded benchmark request")
out = (
    Path(__file__).resolve().parents[1]
    / "artifacts/video"
    / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
)
out.mkdir(parents=True)
rpc = BadgeRPC(args.port, out)
results = []
try:
    rpc.connect(args.firmware_version)
    if args.builtin:
        if rpc.call("video_status")["loaded"] != count:
            raise RuntimeError(
                "Built-in clip frame count does not match the supplied reference"
            )
    else:
        rpc.call("begin", "video", len(data))
        for offset in range(0, len(data), 768):
            rpc.call(
                "chunk", offset, base64.b64encode(data[offset : offset + 768]).decode()
            )
            if offset % (768 * 100) == 0:
                print(f"UPLOAD {offset}/{len(data)}", flush=True)
        rpc.call("commit")
    rpc.call("video_open")
    deadline = time.monotonic() + 40
    while not rpc.call("video_status")["active"]:
        if time.monotonic() > deadline:
            raise TimeoutError("Video app did not open")
        time.sleep(0.2)
    for trial, clock in enumerate(clocks, 1):
        folder = out / f"{trial:02d}-{clock}mhz"
        folder.mkdir()
        log = (folder / "camera.log").open("wb")
        command = [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "warning",
            "-f",
            "avfoundation",
            "-pixel_format",
            "uyvy422",
            "-framerate",
            "30",
            "-video_size",
            "1920x1080",
            "-i",
            args.camera + ":none",
            "-filter_complex",
            "[0:v]split[v][s];[s]fps=1[snap]",
            "-map",
            "[v]",
            "-an",
            "-c:v",
            "libx264",
            "-preset",
            "ultrafast",
            "-crf",
            "24",
            "-pix_fmt",
            "yuv420p",
            "-t",
            "1800",
            "-y",
            str(folder / "camera.mp4"),
            "-map",
            "[snap]",
            "-c:v",
            "mjpeg",
            "-q:v",
            "3",
            "-update",
            "1",
            "-atomic_writing",
            "1",
            "-t",
            "1800",
            "-y",
            str(folder / "latest.jpg"),
        ]
        camera = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=log)
        result = {
            "mhz": clock,
            "frames": frames,
            "status": "starting",
            "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        }
        try:
            time.sleep(1.5)
            if camera.poll() is not None:
                raise RuntimeError("Camera failed")
            before = rpc.call("video_status")["sequence"]
            rpc.call("video_run", clock, frames)
            print("RUNNING " + str(folder), flush=True)
            deadline = time.monotonic() + min(1800, frames * 3 + 120)
            next_progress = 30
            while True:
                status = rpc.call("video_status")
                if status["running"] and status["frame"] >= next_progress:
                    print(
                        f"PROGRESS {clock} MHz: {status['frame']}/{frames} frames",
                        flush=True,
                    )
                    next_progress = status["frame"] + 30
                if status["sequence"] > before and not status["running"]:
                    if status["calls"] != frames:
                        raise RuntimeError("Incomplete video run")
                    result.update(status="completed", result=status)
                    break
                if not status["active"]:
                    raise RuntimeError("Video app exited")
                if time.monotonic() > deadline:
                    raise TimeoutError("Video run stalled")
                time.sleep(0.5)
            time.sleep(2)
        finally:
            if camera.poll() is None:
                camera.send_signal(signal.SIGINT)
                try:
                    camera.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    camera.kill()
                    camera.wait()
            log.close()
            (folder / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            results.append(result)
        print("RESULT " + json.dumps(result), flush=True)
        if args.review:
            print("REVIEW " + str(folder), flush=True)
            deadline = time.monotonic() + 180
            while not (folder / "continue").exists():
                if (folder / "stop").exists() or time.monotonic() > deadline:
                    raise RuntimeError("Review stopped or expired")
                time.sleep(0.5)
finally:
    if rpc.session and not rpc.fault:
        try:
            rpc.call("video_exit")
        except Exception:
            pass
    (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    rpc.close()
