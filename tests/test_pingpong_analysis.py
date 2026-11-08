# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the optical analyzer with a deterministic synthetic recording."""
import importlib.util
from pathlib import Path

import numpy as np


root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "analyze_pingpong", root / "tools/analyze_pingpong.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

assert module.parse_box("190:399:510:19") == (190, 399, 510, 19)

fps = 30.0
signal = np.zeros(360)
frames = np.arange(200)
signal[100:300] = 12.0 * np.sin(2.0 * np.pi * 9.0 * frames / fps)
signal += np.linspace(-3.0, 4.0, len(signal))
result = module.analyze_signal(signal, fps, 100, 300)
assert result["frames"] == 200
assert abs(result["duration_s"] - 20.0 / 3.0) < 1e-9
assert abs(result["state_hz"] - 9.0) < 1e-9
assert abs(result["visible_reversals_hz"] - 18.0) < 1e-9
assert result["implied_visible_reversals"] == 120
assert result["spectral_peak_to_median"] > 10.0
print("PASS ping-pong optical frequency and reversal count")
