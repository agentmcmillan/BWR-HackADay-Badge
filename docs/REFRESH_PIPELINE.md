# Refresh pipeline

The portable raster code changes the work around a partial refresh. It does not
change the pigment waveform, power rails or controller session limits.

1. `bwr_fill_rect` clips once, masks the two edge bytes and fills whole interior
   bytes. It produces the same MSB-first pixels as the original per-pixel loop.
2. `bwr_changed_boxes` visits both controller halves in row order. Equal rows use
   `memcmp`; differing rows only search outside the current horizontal extent.
   This preserves the smallest changed byte box without rescanning its interior.
   When a renderer already knows the only rows it changed,
   `bwr_changed_boxes_rows` applies the same exact search to that inclusive row
   range. The caller must not use the ranged form if an overlay or other drawing
   can change pixels outside the hint.
3. The host accounts for the controller's old/new RAM swap by updating the union
   of current and preceding changed boxes. That state rule remains essential.
4. `bwr_stream_window` bit-reverses and packs a window into two internal DMA
   buffers. `start` launches one; preparation continues into the other; `finish`
   joins before the in-flight buffer can be reused. The final transfer finishes
   before either CS or DC changes.
5. The ESP-IDF adapter acquires the SPI bus across index/data transactions. It
   uses polling start/end for overlapped bulk data, and drains the outstanding
   transfer before returning to ordinary polling commands.

The hardware integration now also programs the pull window to the union of rows
that visibly change on the two COGs. Both COGs receive the same vertical range
so their gate timing remains aligned. The older path uploaded a small RAM window
but restored the pull window to all 672 rows before triggering; limiting the
actual gate scan is what produced the large gain. Horizontal RAM windows still
reduce transfer bytes, but narrowing X did not shorten this panel's pull. The
preceding changed box remains part of the controller-RAM repair; only the current
visible-change union selects the physical pull rows.

The stream API does not own a bus, change a clock or issue panel commands. The
host must serialize panel operations and provide distinct buffers with valid DMA
alignment/lifetimes. The helper rejects buffers that overlap each other or the
source frame before invoking a callback. Buffers remain valid until `finish` returns. The ESP32-S3
integration uses two 4096-byte internal buffers, adding 4 KB over the preceding
single-buffer driver. The ESP-IDF [SPI master documentation](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-reference/peripherals/spi_master.html)
describes bus acquisition and separate polling start/end calls.

The latest host-side BUSY poll requests a 1 ms delay; the current `ms` helper adds
one RTOS tick, giving about 2 ms between checks at a 1 kHz tick rate. It keeps
the existing 5 ms trigger-delay request. Other startup/shutdown waits retain their
previous periods. It never treats a timeout as successful completion. This
reduces detection overshoot; it does not shorten the electrical waveform.

Sanitizer-backed tests compare clipped rectangles and changed boxes against
pixel/byte reference models. Stream tests retain an in-flight buffer and assert
that it is unchanged until completion, then compare every emitted byte for both
controllers, edge windows, full windows and short final chunks. Private adapter
tests exercise the actual GPIO/SPI glue against simulated DMA lifetime rules.
Only the hardware runs in [BENCHMARKS.md](BENCHMARKS.md) establish measured speed.

The archived panel family's 5 MHz SPI guidance still applies. Higher clock
profiles remain experiments. A successful video or checkerboard run does not
certify signal margins, long-term pigment balance or all operating temperatures.

## Related raw-panel implementations

[FastEPD at commit 60d4945](https://github.com/bitbank2/FastEPD/tree/60d49456654c20485487d927f368387b69012ac6)
reinforces several useful principles: keep panel power on across animation,
reduce waveform passes deliberately, write directly into the framebuffer, use
two DMA buffers, and convert current/previous pixels into differential drive
codes with lookup tables or ESP32-S3 SIMD. Its raw parallel-panel driver owns
the source and gate clocks, however, while this badge talks to an integrated
iTC97-class controller over SPI. FastEPD's bit-bang option is explicitly a
clear-only workaround for a parallel-peripheral clock glitch; it repeats a
solid pattern and cannot transmit image rows. It is therefore not a faster
replacement for this driver's 20 MHz SPI DMA path.

The portable lesson is session and charge-state management, not copying raw
timings. Firmware 0.8.42 keeps one differential session open for as many as 600
pulls and uses 19 of the controller's fixed 20 waveform slots for transition
drive, leaving the final slot at GND. The manufacturer booster-current safety
setting remains enabled. This is an aggressive longevity/ghosting experiment,
not a vendor-qualified mode.
