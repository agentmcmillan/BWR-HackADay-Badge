# BWR Hackaday Badge additions

Reusable display, animation and benchmark code developed for the Hackaday
Supercon 2026 Billboard Badge: ESP32-S3 and a 960 × 672 E2969JS0B1 panel.

This repository contains our original additions, host tools, tests and measured
results. It deliberately **does not include the upstream badge OS, panel driver,
stock artwork, or complete firmware images**: the badge firmware repositories
we checked did not include a project license. Their redistribution remains
pending clarification. See [provenance and licenses](docs/LICENSING.md).

## Included

- A name-tag compositor with equal red HELLO/name bars, a configurable white
  name, and a complete game canvas fitted into the white area.
- Spirograph and DVD-style animations, including same-curve reprojection on a
  name-tag toggle and randomized DVD trajectories.
- Byte raster operations, cache-aware difference scanning and pipelined DMA window packing.
- Refresh timing/statistics helpers and bounded USB benchmark control.
- RAM video playback and local-file conversion, with finite live-panel benchmarks.
- ESP-IDF app adapters and a reference for integrating the new driver hooks.
- Standalone image preview, three-color conversion and USB portal integration fragments.
- Sanitizer-backed host tests and anonymized physical benchmark measurements.

The additions are GPL-3.0-or-later. Third-party font and dithering files retain
their public-domain/MIT notices. No license is asserted over omitted upstream
code.

## Build and test without a badge

Requires CMake, a C11 compiler, Python 3.9+, and Node.js for the web renderer test.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
node tests/test_eink_rendering.js
```

AddressSanitizer and UndefinedBehaviorSanitizer are enabled by default for host
C builds. Turn them off with `-DBWR_SANITIZERS=OFF` where unsupported. These are
software tests; they neither flash a board nor exercise its display electronics.

## Integrate with firmware

Add `components/bwr_display` to an ESP-IDF project, then implement the small
port interface in `examples/esp-idf/bwr_badge_port.h`. The app adapters expect
that interface; they are not a standalone replacement firmware.

[Integration notes](docs/INTEGRATION.md) describe rendering, button routing,
panel ownership, timing instrumentation, session limits and the USB controller.
The integrations used for our hardware measurements run ESP-IDF 6.0.2; the
[measurement notes](docs/BENCHMARKS.md) identify each tested firmware version. That full firmware is kept out of this public tree.

## Refresh experiments

[Results and methods](docs/BENCHMARKS.md) include the initial eight 60-frame tests,
software-pipeline follow-ups and full video benchmarks on one badge, using USB
telemetry and webcam observation.

The latest optimized 20 MHz checkerboard run completed 60 physical pulls at
1.503 frames/sec, about 17% above the original 4 MHz path. Camera analysis
confirmed all 60 visible reversals. Byte raster operations, cache-aware scanning,
overlapping DMA preparation and finer BUSY polling contributed beyond the clock
change. The full 323-frame video test completed in 214.0 seconds at 20 MHz versus
251.5 seconds at 4 MHz, with the expected 319 physical pulls in both runs. Heavy ghosting remains. These are experimental monochrome updates; they
do not replace the panel’s full-quality color waveform or certify higher clocks.

On macOS, the controller can run a bounded test sequence and record video with
no audio:

```sh
python3 -m pip install pyserial
python3 tools/auto_refresh.py /dev/cu.usbmodem101 --camera 0 --cases 0:4,0:5
```

It requires compatible firmware implementing the [USB benchmark protocol](docs/USB_BENCHMARK_PROTOCOL.md),
FFmpeg, camera permission, and a camera aimed at the test field. Use `--crop width:height:x:y`
when needed; the default records the whole 1920 × 1080 camera frame. Start while the
panel is idle: opening a USB serial port can reset an ESP32-S3 on macOS.
The script restores normal operation on exit. Captures stay in ignored local
`artifacts/` directories. Raw camera video, flash backups and device logs are not
included in this repository.

## Layout

| Directory | Contents |
| --- | --- |
| `components/bwr_display/` | Portable C models, compositor, statistics and ESP-IDF mailbox |
| `examples/esp-idf/` | Original app adapters, port contract and new driver hooks |
| `web/` | Image preview, conversion and USB integration fragments |
| `tools/` | Automated USB/video test controller |
| `tests/` | Host C, Python and JavaScript checks |
| `docs/` | Integration, protocol, provenance and benchmark methods |
| `data/` | Anonymized aggregate measurements |

[Refresh pipeline](docs/REFRESH_PIPELINE.md) explains the data path and buffer ownership.
[Video mode](docs/VIDEO.md) covers preparation, USB loading and optional private built-in clips.

Format project C with `.clang-format` and Python with Black. Keep vendor files
and their notices unchanged. See [contributing](CONTRIBUTING.md).
