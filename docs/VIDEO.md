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

A / Loop runs a classic clip continuously and changes to Pause during playback.
MENU + selects 4 MHz and MENU − selects the experimental 20 MHz profile. Either
clock button starts a stopped loop or changes the clock at the next frame
boundary while it is running. The controller session is shut down before the
SPI clock changes, then playback continues with the next clip frame. B returns
Home. USB `video_run` remains finite for repeatable benchmarks; USB stop/exit
takes effect at a frame boundary. Exiting restores the default 4 MHz request and
shortcut handling. The RAM clip remains available from Home until restart or
replacement; saved photos and flash partitions are not changed.

Zero-hold native clips use the experimental 20 MHz profile by default and loop
their distinct poses for a bounded 600-frame performance run. Held native clips
play their stored pose count once. The renderer uses `frame % clip.count` only
for native looping. Local classic playback also wraps by the stored frame count;
USB BWRVID1 requests remain finite. Cancellation and clock changes are checked
at every frame boundary.

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

### Adaptive band compilation

An adaptive style compiles source pictures into whole-frame states that differ
only inside one narrow horizontal band. The ordinary partial driver then limits
both RAM traffic and the physical gate scan without requiring a separate wire
format:

```sh
python tools/prepare_video.py input.mp4 artifacts/adaptive.bwrv \
  --fps 10 --style adaptive-dither --band-rows 8 --max-age 12 \
  --motion-weight 2
```

Each source row expands into four panel rows, so the default eight-row band is a
32-panel-row update. The scheduler ranks unresolved pixel error, current motion
and band age, strongly prioritizing overdue changed bands.
`--focus X:Y:W:H` gives a source-space region four times the error weight; this
is useful for a known face or subject but is not automatic object recognition.
`adaptive-light-grid` applies the same scheduling after the sparse light-grid
renderer.

`adaptive-poster` is the high-contrast option for fast, ghost-prone playback.
It applies a mild blur, one fixed grayscale threshold for the entire clip, and
removes only tiny disconnected ink flecks. It deliberately avoids ordered
dither and preserves white holes in faces and clothing, so narrow-band pulls
reinforce solid subject shapes instead of accumulating a field of microdots.
The default threshold is 112; use `--poster-threshold` for other source grading.

`--ink-release-frames 3` adds asymmetric temporal smoothing before adaptive
scheduling. New black pixels appear immediately, while a black pixel is erased
only after three consecutive source frames agree that it is paper. This avoids
gray/dither traffic, bridges one- or two-frame threshold chatter, and often
reduces dirty-row churn. It costs no ESP32 playback time because the filter runs
only in the offline converter. Cyclic history is used so the smoothing state
does not pulse at the clip boundary. Values from 2 to 4 are the useful range;
larger values create increasingly visible black motion trails.

Temporal holds are transparent: if a source step produces no new binary ink
state, the adaptive compiler emits no panel operation. It never substitutes an
all-white placeholder. White release is likewise local; after the hold count is
met, only the selected dirty band is erased. This keeps skipped source time from
costing a physical pull or flashing the whole subject away.

The last states return the image to white one band at a time. Because Video
blanks before playback, both the end-to-start wrap and every internal transition
remain within the requested band height. This adds closure operations and makes
the loop intentionally dissolve before restarting. The JSON result reports
source, closure and total operation counts plus mean/maximum changed rows.

Adaptive output prioritizes local motion rather than simultaneous complete
frames. Its header FPS records source sampling for provenance; playback remains
unpaced and runs at the panel's physical limit.

For persistent subjects, `--cyclic-base` uses a different loop contract. The
compiler measures ink occupancy over the whole clip and stores the stable base
as frame zero. Firmware draws that complete base once, outside timed playback.
Every later operation compares the simulated panel with the newest source target,
updates only the highest-value stale band, and replaces any unfinished older
target. Its error mask includes both the old and new locations of moving ink, so
hands and silhouette edges are erased as well as redrawn. Age and the optional
focus rectangle prevent low-motion regions from starving.

A cyclic-base stream with a nonempty base returns to that base one band at a
time, rather than inserting an all-white closure. In `BWRVID1`, header flag bit
0 marks this contract and the
reserved word carries the first looping frame index (currently 1). Unknown flags,
zero/out-of-range loop indices, and cyclic flags on native-ink clips are rejected.
Legacy zero-flag clips retain their existing white-closure behavior.

USB benchmarks may request up to 600 operations from a cyclic-base clip. The
loop wraps without reinstalling frame zero, so a 438-operation request is about
30 seconds at the measured 14.6 operations/second cadence. Legacy clips remain
bounded by their stored frame count.

### Row-band playback adapter

Adaptive clips can carry their locality through the complete render path.
`bwr_video_delta_rows` compares the compact source rows of the current and
previous operation (or an all-white baseline for the first operation), and
`bwr_video_draw_rows` expands only the inclusive changed range into the panel
canvas. A host adapter may then call its partial-refresh path with the matching
panel rows, `48 + first_row * 4` through
`48 + (last_row + 1) * 4 - 1`, inclusive.

The ranged refresh is an optimization hint, not a different image format. It is
safe only when the status bar and blank app canvas have already been committed,
no overlay is added outside the hinted rows, and the adapter still repairs the
controller's preceding RAM window. Ordinary full-frame rendering remains the
fallback for native clips and integrations without a ranged refresh API.
An empty delta is reported as `first_row > last_row`; callers can count that
operation as a cache hit without expanding or scanning the full canvas.

`bwr_video_draw_rows_smol` is a second presentation path for classic clips. It
duplicates every compact source pixel 2 x 2 into a centered 480 x 312 viewport
at panel coordinates x=240..719 and y=204..515. This is half the full video
width and height, or one quarter of its area. The matching partial-refresh hint
is `204 + first_row * 2` through `204 + (last_row + 1) * 2 - 1`. Both the source
detail and frame count stay unchanged; the speed opportunity comes from shorter
gate scans and smaller RAM windows. Native ink-memory clips are deliberately
rejected by this helper because their pixels are already panel-resolution.

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
