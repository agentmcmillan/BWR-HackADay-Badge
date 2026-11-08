# Refresh benchmark — 8 November 2026

One ESP32-S3 Billboard Badge, E2969JS0B1 960 × 672 BWR panel, ESP-IDF 6.0.2,
custom firmware 0.8.31. Eight consecutive autonomous USB-controlled runs each
completed 60 measured frames. A MacBook camera recorded the changing test field
at 30 fps without audio (1000 × 700 crop at camera position 480,350). Anonymized telemetry is in
[`data/refresh-2026-11-08.csv`](../data/refresh-2026-11-08.csv).

## Results

| Pattern | Requested / actual MHz | FPS | Mean call ms | Steady call ms | RAM write ms | Panel pull ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Moving block | 4 / 4 | 1.490 | 671.2 | 627.0 | 8.7 | 587.6 |
| Moving block | 5 / 5 | 1.493 | 669.8 | 626.7 | 8.1 | 587.5 |
| Checkerboard | 4 / 4 | 1.287 | 777.0 | 732.6 | 91.6 | 588.1 |
| Checkerboard | 5 / 5 | 1.319 | 758.3 | 715.1 | 74.7 | 587.6 |
| Checkerboard | 8 / 8 | 1.367 | 731.4 | 689.7 | 50.2 | 586.8 |
| Checkerboard | 12 / 11.428 | 1.389 | 719.8 | 679.1 | 38.2 | 587.3 |
| Checkerboard | 20 / 20 | 1.414 | 707.1 | 667.0 | 26.1 | 588.3 |
| Checkerboard, repeat | 4 / 4 | 1.289 | 775.9 | 731.7 | 90.2 | 588.7 |

20 MHz improved checkerboard throughput by **9.9%** over the first 4 MHz run.
Mean write time fell by about 71.5%, while the panel pull stayed near 588 ms.
The repeat 4 MHz run differed by about 0.15% in throughput. Moving a small block
benefited very little because it transfers fewer changed pixels.

## What was timed

The test field is below a static status area. Warmup and the result screen are
excluded. There is no deliberate pacing between measured calls. FPS is completed
frames divided by device-measured burst elapsed time; each changed call in these
runs triggered one panel pull. Every run includes one partial-session reopen.
The mean includes that reopen; the steady mean excludes reopened calls. The CSV
also records rendering, difference scanning, bytes and session reopen counts.
Phase averages need not sum to the whole call because other work and session
setup are outside those phases. CPU rendering and scanning are not SPI writes.

The requested 12 MHz profile resolved to 11.428 MHz on the ESP32-S3 divider.
The integration applies the selected clock at a transaction boundary and reports
the actual configured rate. Power sequencing, BUSY waits, waveform, voltage,
session limits and shutdown handling were unchanged. Exit restored 4 MHz and
Home; that return was confirmed by camera.

## Optical and electrical limits

Heavy ghosting and pink residuals were visible at the baseline and higher clocks.
Reviewed recordings showed no obvious new gross transfer corruption, and USB
telemetry recorded no panel/controller fault. This was visual screening, not
bit-perfect optical verification, a contrast measurement or a lifetime test.
Different clocks put the animation at different phases at a given video time;
stills alone cannot establish that one profile has better contrast.

The camera can reveal missed transitions and large artifacts but cannot resolve
SPI integrity or certify waveform settling. A logic analyzer/oscilloscope and
controlled optical chart measurements would be needed for those claims. This
single-board sequence does not establish a reliable maximum operating clock.

The vendor's [archived Large BW/BWR SPI page](https://docs.pervasivedisplays.com/knowledge/Archives/Hardware/Screens/large-epds/spi-protocol.html)
lists a 5 MHz write limit. A [newer Wide Large SPI page](https://docs.pervasivedisplays.com/knowledge/Hardware/epd-usage/Screens/Wide_Large/spi-timing-format.html)
lists 20 MHz for that family; it does not certify this older panel/board.
Here 8, 12 and 20 MHz are experimental overclock profiles. The normal default
remains 4 MHz. Faster transfers alone cannot eliminate the measured pull time;
shorter waveforms would be a separate experiment requiring panel-specific evidence.

## Reproduce

Use compatible firmware exposing the [bounded USB commands](USB_BENCHMARK_PROTOCOL.md).
Install pyserial and FFmpeg; frame the entire changing field and start with an
idle panel. Opening serial can reset the board on macOS. The controller waits
for Home before requesting the benchmark app.

```sh
python3 tools/auto_refresh.py /dev/cu.usbmodem101 --camera 0 --crop 1000:700:480:350 --frames 60 \
  --cases 0:4,0:5,2:4,2:5,2:8,2:12,2:20,2:4 --review
```

At each REVIEW, inspect the local recording/latest image, then create `continue`
in that trial directory, or `stop` to end. The review wait is bounded. Do not
retry a BUSY fault automatically. Raw video and device-specific serial logs stay
in ignored local artifacts; only aggregate measurements are published.
