#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture one manually started Refresh Lab burst: USB telemetry plus video, no audio."""
import datetime, json, re, signal, subprocess, sys, time
from pathlib import Path
import serial

if len(sys.argv) != 4:
    raise SystemExit("Usage: record_refresh.py PORT LABEL CAMERA_INDEX")
port, label, camera = sys.argv[1:]
if not re.fullmatch(r"[a-zA-Z0-9_-]+", label):
    raise SystemExit("Invalid label")
root = Path(__file__).resolve().parents[1] / "artifacts/benchmarks"
out = root / (datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + label)
out.mkdir(parents=True, exist_ok=False)
s = serial.Serial(port=None, baudrate=115200, timeout=0.1)
s.dtr = False
s.rts = False
s.port = port
s.open()
record = {
    "label": label,
    "port": port,
    "camera": camera,
    "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "summary": None,
    "status": "waiting",
}
video_log = (out / "camera.log").open("wb")
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
        camera + ":none",
        "-t",
        "90",
        "-an",
        "-c:v",
        "libx264",
        "-preset",
        "ultrafast",
        "-crf",
        "20",
        "-pix_fmt",
        "yuv420p",
        "-y",
        str(out / "camera.mp4"),
    ],
    stdout=subprocess.DEVNULL,
    stderr=video_log,
)
try:
    time.sleep(1.5)
    if proc.poll() is not None:
        raise RuntimeError("Camera exited; inspect camera.log")
    print("READY " + str(out), flush=True)
    begin = time.monotonic()
    buffer = b""
    tail_until = None
    with (out / "serial.log").open("wb") as log, (out / "serial-timed.jsonl").open(
        "w"
    ) as timed:
        while time.monotonic() - begin < 80:
            data = s.read(8192)
            if data:
                log.write(data)
                log.flush()
                buffer += data
                while b"\n" in buffer:
                    raw, buffer = buffer.split(b"\n", 1)
                    line = raw.decode(errors="replace").strip()
                    timed.write(
                        json.dumps({"host_monotonic": time.monotonic(), "line": line})
                        + "\n"
                    )
                    timed.flush()
                    if "refresh_lab:" in line and "elapsed_us=" in line:
                        record["summary"] = {
                            k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", line)
                        }
                        record["status"] = "completed"
                        tail_until = time.monotonic() + 2
                        print(line, flush=True)
                    if "Panel stopped;" in line:
                        record["status"] = "panel_fault"
                        tail_until = time.monotonic() + 2
            if tail_until and time.monotonic() >= tail_until:
                break
    if not record["summary"] and record["status"] != "panel_fault":
        record["status"] = "timeout"
finally:
    s.close()
    if proc.poll() is None:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    video_log.close()
    record["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / "result.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record), flush=True)
