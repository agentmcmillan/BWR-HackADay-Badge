# RAM video mode

The original video app adapter is in `examples/esp-idf/app_video.c`. It runs on
the host UI task and uses the same serialized partial-refresh driver as Refresh
Lab. Call `video_init` before starting USB or the UI. A clip is immutable while
the app is active. A successful `video_accept` takes ownership of its allocation;
a failed call leaves ownership with the caller.

A clip stores at most 600 monochrome frames at 240 × 156. Playback expands each
pixel to 4 × 4 in the full 960 × 624 app area. The top 48 status rows are preserved.
It does not skip frames or deliberately pace playback: it measures the rate the
panel can sustain. Source sampling FPS describes the source timeline, not a
promise of display FPS. Identical frames can produce no panel pull and are
reported separately from completed frame calls. Playback is silent.

A / Replay runs the loaded clip, MENU + selects/runs 4 MHz, MENU − selects/runs
the experimental 20 MHz profile, and B returns Home. USB stop/exit takes effect
at a frame boundary. Exiting restores the default 4 MHz request and shortcut
handling. The RAM clip remains available from Home until restart or replacement;
saved photos and flash partitions are not changed.

## Prepare and run

Install NumPy, pyserial and FFmpeg. Use a local video you are authorized to use.
Neither this repository nor its public CI contains music-video frames or audio.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-dev.txt
python tools/prepare_video.py /path/to/local-video.mpg artifacts/clip.bwrv --fps 1.5
python tools/video_benchmark.py /dev/cu.usbmodem101 artifacts/clip.bwrv \
  --firmware-version 0.8.35 --clocks 4,20 --camera 0 --review
```

The converter preserves the source aspect within the small canvas and applies
a fixed 8 × 8 ordered dither, avoiding independent random noise per frame. The
USB controller keeps its connection open across upload and playback, since
opening serial can reset an ESP32-S3 on macOS. Review recordings locally; do not
publish source clips, derived frames or device-specific logs unintentionally.

`--frames 60` limits a timing run. Omit it to run every loaded frame. After a
REVIEW checkpoint, create `continue` or `stop` in that trial directory. BUSY faults
remain fail-stops. Camera capture is macOS AVFoundation, 1920 × 1080 at 30 fps.

## Wire and file format

The `BWRVID1\0` header is 32 bytes, little-endian: 8-byte magic, uint16 width and
height (240,156), then uint32 frame count, source FPS × 1000, payload CRC-32,
flags (0) and reserved (0). Each frame is 4680 bytes, row-major MSB-first, 1=black.
Exact size, CRC, dimensions, count and supported FPS range are checked before
accepting ownership. No compressed data is interpreted as a pointer or code.

The existing authenticated `begin video SIZE`, ordered 768-byte `chunk` and
`commit` upload carries a raw clip. Compatible firmware routes `video_open`,
`video_status`, `video_run MHz FRAMES`, `video_stop`, and `video_exit` to the new
bounded app state. The host Home queue needs a dedicated video launch event;
USB must never draw or trigger a panel refresh directly.

For a private built-in asset, `prepare_video.py --packbits` adds the optional
`BWRPACK1` envelope: 8-byte magic, uint32 decoded length and CRC-32, followed by
PackBits literal/repeat records. `bwr_video_unpack` enforces the decoded bound,
rejects reserved code 128, verifies CRC and validates the contained clip. An
integrator may embed their local asset and pass its unpacked RAM allocation to
`video_accept`. This optional media must remain outside public source. The
benchmark tool's `--builtin` skips upload only when the reported `loaded` frame
count and `clip_crc32` payload checksum match the local reference. Older status
adapters without `clip_crc32` must be updated before this option can be used.

## Optical checks

`tools/analyze_checker.py` counts contrast reversals in the synthetic benchmark.
`tools/analyze_video.py` compares sampled camera images with the local reference
clip after perspective correction and a 1.2-pixel Gaussian blur. This reduces
sensitivity to camera sampling of the ordered dither. It reports the best frame
index and correlation; it does not certify every pixel or full pigment settling.
Neither analyzer exports video frames to the public repository.

## Low-resolution light-grid variant

Use `--style light-grid --grid 32x20` when preparing a local clip:

```sh
python tools/prepare_video.py /path/to/local-video.mpg artifacts/light-grid.bwrv \
  --style light-grid --grid 32x20 --fps 1.5
```

The option takes inspiration from Jim Campbell's low-resolution moving-image
work and the relationship between visible points and recognizable motion,
described in his [Whitney interview](https://whitney.org/media/39112). It first
low-pass filters the image, samples 32 × 20 brightness values, then varies the
area of white paper apertures in a dark field. Dark moving figures emerge as
missing apertures. Eight area levels and modest temporal smoothing reduce
flicker; they do not increase the display's physical refresh rate.

The grid is bounded to 4–80 columns and 4–52 rows. The default converter remains
`--style dither`. Both styles produce the same 240 × 156 BWRVID1 format and work
with the existing Video upload mode; a new firmware renderer is unnecessary.
The public tree includes no music-video frames or artwork by Campbell.
