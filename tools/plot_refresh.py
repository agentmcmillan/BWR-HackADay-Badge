#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plot the published aggregate checkerboard measurements, not simulated rates."""
import csv
from pathlib import Path
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

root = Path(__file__).resolve().parents[1]
original = list(csv.DictReader((root / "data/refresh-2026-11-08.csv").open()))
latest = list(csv.DictReader((root / "data/refresh-pipeline-2026-11-08.csv").open()))
old = [
    next(
        float(r["fps"])
        for r in original
        if r["pattern"] == "2" and r["requested_mhz"] == str(rate)
    )
    for rate in [4, 20]
]
new = [
    next(
        float(r["fps"])
        for r in reversed(latest)
        if r["firmware"] == "0.8.35" and r["pattern"] == "2" and r["mhz"] == str(rate)
    )
    for rate in [4, 20]
]
plt.rcParams.update(
    {"font.family": "DejaVu Sans", "font.size": 11, "svg.fonttype": "none"}
)
fig, ax = plt.subplots(figsize=(8, 4.8))
x = np.arange(2)
for offset, values, color, label in [
    (-0.18, old, "#666666", "Original 0.8.31"),
    (0.18, new, "#a40000", "Optimized 0.8.35"),
]:
    bars = ax.bar(x + offset, values, 0.34, color=color, label=label)
    ax.bar_label(bars, labels=[f"{v:.3f}" for v in values], padding=4)
ax.set_xticks(x, ["4 MHz", "20 MHz (experimental)"])
ax.set_ylabel("Completed physical refreshes / second")
ax.set_ylim(0, 1.8)
ax.set_title(
    "Measured checkerboard throughput on one badge", loc="left", weight="bold", pad=18
)
ax.legend(frameon=False, loc="upper left")
ax.spines[["top", "right"]].set_visible(False)
ax.grid(axis="y", alpha=0.15)
ax.set_axisbelow(True)
fig.text(
    0.12,
    0.025,
    "60 changed frames per run; includes session reopen. Camera confirmed 60 reversals.\nMonochrome experimental path; full-quality color refresh is a separate mode.",
    fontsize=9,
    color="#444444",
)
fig.subplots_adjust(bottom=0.22, top=0.85)
out = root / "docs/assets"
out.mkdir(exist_ok=True)
fig.savefig(out / "refresh-throughput.svg", metadata={"Date": None})
svg = out / "refresh-throughput.svg"
svg.write_text("\n".join(line.rstrip() for line in svg.read_text().splitlines()) + "\n")
fig.savefig(out / "refresh-throughput.png", dpi=150)
print(out / "refresh-throughput.svg")
