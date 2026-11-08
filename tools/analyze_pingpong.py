#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure a narrow two-state e-paper band against adjacent background."""
import argparse
import json
from pathlib import Path

import numpy as np


def parse_box(value):
    """Parse X:Y:WIDTH:HEIGHT into NumPy slice coordinates."""
    x, y, width, height = (int(part) for part in value.split(":"))
    if min(x, y) < 0 or width < 1 or height < 1:
        raise ValueError("boxes must have nonnegative origins and positive dimensions")
    return x, y, width, height


def analyze_signal(signal, fps, start_frame, end_frame):
    """Return the dominant state frequency after removing slow camera drift."""
    active = np.asarray(signal, dtype=float)[start_frame:end_frame]
    if fps <= 0 or len(active) < 30:
        raise ValueError("invalid frame rate or active interval")
    axis = np.linspace(-1.0, 1.0, len(active))
    detrended = active - np.polyval(np.polyfit(axis, active, 3), axis)
    spectrum = np.abs(np.fft.rfft(detrended * np.hanning(len(active))))
    frequencies = np.fft.rfftfreq(len(active), d=1.0 / fps)
    selection = (frequencies >= 2.0) & (frequencies <= min(14.0, fps / 2.0))
    if not np.any(selection):
        raise ValueError("no usable frequency bins")
    selected_amplitudes = spectrum[selection]
    selected_frequencies = frequencies[selection]
    peak_index = int(np.argmax(selected_amplitudes))
    peak = float(selected_amplitudes[peak_index])
    background = float(np.median(selected_amplitudes))
    state_hz = float(selected_frequencies[peak_index])
    duration_s = len(active) / fps
    return {
        "frames": len(active),
        "duration_s": duration_s,
        "state_hz": state_hz,
        "visible_reversals_hz": 2.0 * state_hz,
        "implied_visible_reversals": int(round(2.0 * state_hz * duration_s)),
        "spectral_peak_to_median": peak / max(background, np.finfo(float).eps),
    }


def box_mean(gray, box):
    x, y, width, height = box
    if x + width > gray.shape[1] or y + height > gray.shape[0]:
        raise ValueError("sample box falls outside the recording")
    return float(gray[y : y + height, x : x + width].mean())


def main():
    import cv2

    parser = argparse.ArgumentParser()
    parser.add_argument("video", type=Path)
    parser.add_argument("--band", required=True, type=parse_box)
    parser.add_argument("--above", required=True, type=parse_box)
    parser.add_argument("--below", required=True, type=parse_box)
    parser.add_argument("--start-frame", required=True, type=int)
    parser.add_argument("--end-frame", required=True, type=int)
    parser.add_argument("--expected-triggers", type=int)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    capture = cv2.VideoCapture(str(args.video))
    if not capture.isOpened():
        raise RuntimeError("could not open recording")
    fps = float(capture.get(cv2.CAP_PROP_FPS))
    signal = []
    while True:
        ok, image = capture.read()
        if not ok:
            break
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        background = (box_mean(gray, args.above) + box_mean(gray, args.below)) / 2.0
        signal.append(box_mean(gray, args.band) - background)
    capture.release()

    result = analyze_signal(signal, fps, args.start_frame, args.end_frame)
    result.update(
        {
            "video": str(args.video),
            "fps": fps,
            "start_frame": args.start_frame,
            "end_frame": args.end_frame,
            "band": args.band,
            "above": args.above,
            "below": args.below,
            "method": "band mean minus adjacent background; cubic detrend; Hann-window FFT",
        }
    )
    valid = result["spectral_peak_to_median"] >= 10.0
    if args.expected_triggers is not None:
        result["expected_triggers"] = args.expected_triggers
        valid &= abs(result["implied_visible_reversals"] - args.expected_triggers) <= 2
    result["valid"] = bool(valid)
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")
    if not valid:
        raise SystemExit("optical validation failed")


if __name__ == "__main__":
    main()
