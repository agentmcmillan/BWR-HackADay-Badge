#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run bounded Refresh Lab trials over authenticated USB RPC; record video without audio."""
import argparse
import base64
import datetime
import json
import re
import signal
import subprocess
import time
from pathlib import Path
import serial

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("port")
p.add_argument("--cases", default="0:4,0:5")
p.add_argument("--camera", default="0")
p.add_argument("--crop", help="Optional FFmpeg crop: width:height:x:y")
p.add_argument("--frames", type=int, default=60)
p.add_argument("--review", action="store_true")
p.add_argument("--firmware-version", default="0.8.31")
args = p.parse_args()
if args.crop and not re.fullmatch(r"[1-9][0-9]*:[1-9][0-9]*:[0-9]+:[0-9]+", args.crop):
    p.error("--crop requires positive width:height and nonnegative x:y")
video_filter = "[0:v]" + ("crop=" + args.crop + "," if args.crop else "")
video_filter += "split[v][s];[s]fps=1[snap]"
cases = [tuple(map(int, item.split(":"))) for item in args.cases.split(",")]
if not 1 <= args.frames <= 60 or any(
    pattern not in range(5) or rate not in (4, 5, 8, 12, 20) for pattern, rate in cases
):
    raise SystemExit("Invalid bounded case list")
root = Path(__file__).resolve().parents[1] / "artifacts/benchmarks"
out = root / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
out.mkdir(parents=True, exist_ok=False)
s = serial.Serial(port=None, baudrate=115200, timeout=0.1)
s.dtr = False
s.rts = False
s.port = args.port
s.open()
log = (out / "serial.log").open("w")
timed = (out / "serial-timed.jsonl").open("w")
seq = 0
session = None
fault = False
all_results = []
home_ready = False
rx_pending = bytearray()


def line_read():
    global fault, home_ready
    data = s.readline()
    if not data:
        return ""
    rx_pending.extend(data)
    if len(rx_pending) > 8192:
        raise RuntimeError("Oversized serial line")
    if not rx_pending.endswith(b"\n"):
        return ""
    line = rx_pending.decode(errors="replace").strip()
    rx_pending.clear()
    if "Home ready:" in line:
        home_ready = True
    log.write(line + "\n")
    log.flush()
    timed.write(json.dumps({"monotonic": time.monotonic(), "line": line}) + "\n")
    timed.flush()
    if "Panel stopped;" in line or "Panel BUSY timeout" in line:
        fault = True
        raise RuntimeError("Panel fail-stop; no further trials or resets")
    return line


def rpc(verb, *parts, timeout=5):
    global seq, session
    seq += 1
    fields = (
        ["@BC1", str(seq), verb]
        + ([] if verb == "hello" else [session])
        + list(map(str, parts))
    )
    s.write((" ".join(fields) + "\n").encode())
    s.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = line_read()
        m = re.match(r"@BC1 (\d+) (\d+) (\S+)", line)
        if m and int(m[1]) == seq:
            code = int(m[2])
            body = base64.b64decode(m[3], validate=True).decode()
            if code != 200:
                raise RuntimeError(f"{verb}: {code}: {body}")
            try:
                return json.loads(body)
            except json.JSONDecodeError:
                return body
    raise TimeoutError(verb + " response timed out")


def stop_video(proc):
    if proc.poll() is None:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


try:
    hello = rpc("hello")
    expected_version = args.firmware_version
    if hello["version"] != expected_version:
        raise RuntimeError("Wrong test firmware: " + str(hello))
    # Set only after the version check, so cleanup never commands unknown firmware.
    session = hello["session"]
    # macOS may reset the chip when this port opens. Do not enqueue work before
    # Home's initialization/btn_flush has completed. Keep this connection open.
    boot_deadline = time.monotonic() + 20
    while not home_ready and time.monotonic() < boot_deadline:
        line_read()
    rpc("bench_open")
    deadline = time.monotonic() + 30
    while True:
        status = rpc("bench_status")
        if status["active"]:
            break
        if time.monotonic() > deadline:
            raise TimeoutError("Refresh Lab did not open")
        time.sleep(0.2)
    for trial_index, (pattern, rate) in enumerate(cases, 1):
        trial = out / f"{trial_index:02d}-pattern-{pattern}-{rate}mhz"
        trial.mkdir()
        camera_log = (trial / "camera.log").open("wb")
        proc = subprocess.Popen(
            [
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
                video_filter,
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
                "90",
                "-y",
                str(trial / "camera.mp4"),
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
                "90",
                "-y",
                str(trial / "latest.jpg"),
            ],
            stdout=subprocess.DEVNULL,
            stderr=camera_log,
        )
        result = {
            "pattern": pattern,
            "mhz": rate,
            "frames": args.frames,
            "status": "starting",
            "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        }
        try:
            time.sleep(1.5)
            if proc.poll() is not None:
                raise RuntimeError("Camera failed; inspect camera.log")
            before = rpc("bench_status")["sequence"]
            rpc("bench_run", pattern, rate, args.frames)
            print("RUNNING " + str(trial), flush=True)
            deadline = time.monotonic() + 90
            while True:
                status = rpc("bench_status")
                if (
                    status["sequence"] > before
                    and not status["running"]
                    and not status["pending"]
                ):
                    if (
                        status["pattern"] != pattern
                        or status["mhz"] != rate
                        or status["calls"] != args.frames
                    ):
                        raise RuntimeError("Mismatched or incomplete trial")
                    result.update(status="completed", result=status)
                    break
                if not status["active"]:
                    raise RuntimeError("Refresh Lab exited during trial")
                if time.monotonic() > deadline:
                    raise TimeoutError("Timed burst stalled")
                time.sleep(0.5)
            time.sleep(2)
        except BaseException as exc:
            result["status"] = "error"
            result["error"] = str(exc)
            if not fault:
                try:
                    rpc("bench_stop")
                except Exception:
                    pass
            raise
        finally:
            stop_video(proc)
            camera_log.close()
            result["finished_utc"] = datetime.datetime.now(
                datetime.timezone.utc
            ).isoformat()
            (trial / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            all_results.append(result)
        print("RESULT " + json.dumps(result["result"]), flush=True)
        if args.review:
            print("REVIEW " + str(trial), flush=True)
            review_deadline = time.monotonic() + 180
            while not (trial / "continue").exists():
                if (trial / "stop").exists():
                    raise RuntimeError("Stopped after camera review")
                if time.monotonic() > review_deadline:
                    raise TimeoutError("Review checkpoint expired")
                time.sleep(0.5)
        # Inspect each requested set before launching another overclock batch.
    print("CAPTURED " + str(out), flush=True)
finally:
    if session and not fault:
        try:
            rpc("bench_exit")
        except Exception:
            pass
    (out / "results.json").write_text(json.dumps(all_results, indent=2) + "\n")
    s.close()
    log.close()
    timed.close()
