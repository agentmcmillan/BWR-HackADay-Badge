# Ghost Dance: ink as memory

This mode uses the screen's native 960 × 624 app area. It can run detailed poses
continuously, or close the panel session and hold each pose before moving on.
The ordinary
partial waveform erases the previous pose imperfectly, leaving a physical
brown/pink afterimage. No digital history is baked into the clip. The panel's
residue becomes part of the drawing.

A native clip contains at most 36 poses. Use `--hold 0` for continuous playback:
there is no artificial hold or shutdown between poses. The existing driver
still enforces its session limit, and the app closes the session after the final
pose or cancellation. Actual cadence depends on the panel and SPI profile.
Zero-hold clips require the updated native decoder and app (private integration
0.8.38); older held-pose firmware rejects them.

Without `--hold 0`, the converter default holds each completed pose
for four seconds after refresh and shutdown; controller setup and the next pull
add more time. This is deliberately paced artwork, not a maximum-refresh test.
The final pose stays visible while the app remains idle. A replays without a
blank warmup, MENU + resets using the existing monochrome wipe, and B returns
Home. The wipe may retain some pigment residue. No waveform, voltage or BUSY
rules are changed.

## Prepare local poses

```sh
python tools/prepare_ink_memory.py /path/to/local-video.mpg artifacts/ghost.bwrink \
  --start 1 --poses 12 --source-step 0.6 --hold 0
python tools/video_benchmark.py /dev/cu.usbmodem101 artifacts/ghost.bwrink \
  --firmware-version 0.8.38 --clocks 4 --leave-ink --review
```

The converter retains native image detail and uses a fixed fine stipple instead
of the low-resolution dot grid. Input and output stay local. It can optionally
isolate the person with `--subject-model /path/to/humanseg.onnx`. The supported
[OpenCV Zoo PPHumanSeg model](https://github.com/opencv/opencv_zoo/tree/main/models/human_segmentation_pphumanseg)
is Apache-2.0 licensed by its authors; no model weights are distributed here.
The expected 2023mar ONNX SHA-256 is
`552d8a984054e59b5d773d24b9b12022b22046ceb2bbc4c9aaeaceb36a9ddf24`.
The local model performs segmentation only; it does not generate or replace the
person. Original source video and derived images are not part of the public repo.

## Format and lifecycle

`BWRINK1\0` uses the same 32-byte little-endian envelope as BWRVID1: width 960,
height 624, frame count 1–36, hold duration either 0 (continuous) or
1,000–30,000 milliseconds, payload CRC-32,
flags 0 and reserved 0. Each frame is 74,880 bytes, row-major MSB-first, 1=black.
The first 48 status rows are never stored or overwritten by the clip. The parser
checks the exact format, dimensions, bounds, size and CRC before taking ownership.

Legacy BWRVID1 remains 240 × 156 with 4× expansion and unpaced playback. It is
unchanged. Native mode has no result redraw over the artwork. Holds poll stop/
Home requests at bounded intervals without holding a mutex, and long holds occur
with the panel session closed. A successful `--leave-ink` run closes serial while
leaving the app idle on the last pose; error paths still request safe exit unless
the controller has reported a fail-stop.

The controller includes any configured stable hold time in elapsed duration.
Runs with nonzero holds are deliberately paced artwork; zero-hold runs measure
continuous playback. Status exposes `cancelled` for the last result, so
interrupting
the final pose's hold is not reported as a completed run just because every pose
has already been drawn. Built-in identity covers the whole file CRC as well as
the payload CRC, including native hold metadata. A camera that exits during a
trial makes that capture an error rather than valid benchmark evidence.

## Live check

Firmware 0.8.37 completed a four-pose native run at 4 MHz with four physical pulls,
four session reopens and four-second holds after shutdown. Total elapsed time
was 29.486 seconds. The camera showed detailed new poses alongside faint residual
traces, and the last image remained after serial disconnected. No controller
fault was reported. This is a paced artwork check, not a refresh-speed claim.
Raw recordings, the model and user-supplied Rick video remain local. The complete
native extension received independent review and regression tests before push.

A subsequent nine-pose comparison used the same native payload at 4 MHz:

| Playback | Hold after each pose | Session opens | Physical pulls | Elapsed |
| --- | ---: | ---: | ---: | ---: |
| Held poses, 0.8.37 | 5 seconds | 9 | 9 | 75.083 seconds |
| Continuous, 0.8.38 | 0 | 1 | 9 | 8.761 seconds |

The continuous sequence took 8.57 times less time, including initial controller
setup. Its whole-run rate was 1.03 physical refreshes per second. This is a short
sequence comparison, not a new sustained maximum-rate claim. Both completed
without cancellation or a reported controller fault. The continuous check used
serial telemetry; its visual ghosting was not camera-verified. The final pose
was left in place, and the panel session closed at completion.
