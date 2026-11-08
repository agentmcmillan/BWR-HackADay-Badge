# Ghost Dance: ink as memory

This mode uses the screen's native 960 × 624 app area. It holds a detailed pose,
closes the panel session and waits before moving to the next one. The ordinary
partial waveform erases the previous pose imperfectly, leaving a physical
brown/pink afterimage. No digital history is baked into the clip. The panel's
residue becomes part of the drawing.

A native clip contains at most 36 poses. The default holds each completed pose
for four seconds after refresh and shutdown; controller setup and the next pull
add more time. This is deliberately paced artwork, not a maximum-refresh test.
The final pose stays visible while the app remains idle. A replays without a
blank warmup, MENU + resets using the existing monochrome wipe, and B returns
Home. The wipe may retain some pigment residue. No waveform, voltage or BUSY
rules are changed.

## Prepare local poses

```sh
python tools/prepare_ink_memory.py /path/to/local-video.mpg artifacts/ghost.bwrink \
  --start 1 --poses 12 --source-step 0.6 --hold 4
python tools/video_benchmark.py /dev/cu.usbmodem101 artifacts/ghost.bwrink \
  --firmware-version 0.8.37 --clocks 4 --leave-ink --review
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

`BWRINK1\0` uses the same 32-byte little-endian envelope as BWRVID1: width960,
height624, frame count1–36, hold duration1000–30000 milliseconds, payload CRC-32,
flags0 and reserved0. Each frame is74880 bytes, row-major MSB-first, 1=black.
The first48 status rows are never stored or overwritten by the clip. The parser
checks the exact format, dimensions, bounds, size and CRC before taking ownership.

Legacy BWRVID1 remains240 × 156 with 4× expansion and unpaced playback. It is
unchanged. Native mode has no result redraw over the artwork. Holds poll stop/
Home requests at bounded intervals without holding a mutex, and long holds occur
with the panel session closed. A successful `--leave-ink` run closes serial while
leaving the app idle on the last pose; error paths still request safe exit unless
the controller has reported a fail-stop.
