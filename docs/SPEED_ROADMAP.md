# Display speed roadmap

This roadmap starts from the measurements on one E2969JS0B1 panel with two
IST7136 chip-on-glass controllers. It separates changes that can improve visible
motion from register experiments that have already reached a dead end.

## Baseline and constraint

The current short differential waveform follows an approximately linear gate
cost:

```text
pull_us = 43,626 + 800.33 * selected_rows
```

The completion-edge path has already reached 24.019 controller triggers/second
for one row. A 16-row two-state band reached 18.631 triggers/second and 18.653
camera-observed reversals/second. A full-height 624-row pull remains about
543 ms before drawing and RAM transfer. TCON `0x25`, DCTL, the oscillator write
and DRFW endpoints `0xA8`/`0xA9` did not produce a practical speed difference.

That makes selected gate height and waveform length the dominant levers. SPI
clock matters mainly when a large RAM area must be transferred.

## Ordered experiments

### 1. Add a rolling-band video mode

Update only one horizontal band per pull and advance through the screen on
successive pulls. Start with 64, 32 and 16 rows. At the measured gate law, their
pull-only ceilings are roughly 10.5, 14.4 and 17.7 updates/second; the verified
16-row implementation has already slightly exceeded that fit.

This intentionally trades whole-frame simultaneity for fluid local motion and
leans into the panel's persistence. Record both band cadence and the time for a
complete top-to-bottom sweep. Keep ordinary full-frame playback as the quality
comparison.

Pass gate: every requested band produces a visible transition, no seam loses a
row, both COG halves stay aligned, and measured cadence improves by at least 2x
over full-height playback.

### 2. Encode for e-paper persistence

Generate a second video asset with a stable background, no frame-to-frame dither
noise, a luminance deadband and a motion-weighted region of interest. Ordered
dither that changes otherwise static pixels expands the dirty-row union and
forces an unnecessarily tall scan.

Store per-frame changed-row metadata with the asset. This removes a full-plane
diff from the narrow-band hot path and gives the scheduler the intended dirty
geometry without rediscovering it on the ESP32.

Pass gate: recognizable motion with fewer dirty rows and a lower mean call time;
compare against the same source frames and clock.

### 3. Schedule disjoint dirty clusters

The controller accepts one continuous gate interval per trigger. When changes
exist in separated regions, choose between one union pull and several cluster
pulls using the measured cost model:

```text
cost(cluster set) = sum(43,626 + 800.33 * cluster_height)
```

Small gaps should remain joined because every extra trigger pays the 43.6 ms
fixed cost. Large quiet gaps can be skipped. Limit deferred rows by age so no
part of the image starves.

Pass gate: the scheduler predicts the faster choice on recorded frame traces and
device timing agrees within 10%.

### 4. Test RAM writes during BUSY

The present path waits for a pull to finish before preparing the next inactive
RAM plane. Build a bounded trial that starts a known narrow pull, attempts a
small write to the inactive plane while BUSY is asserted, then verifies the next
two optical states. Test one, 16 and 64 rows with a strict timeout and normal
soft shutdown.

If the controller accepts concurrent inactive-plane writes, drawing and SPI
transfer can overlap the electrical waveform. If it ignores or corrupts the
write, retain the serialized path. This can remove host work but cannot beat the
waveform's electrical floor.

Pass gate: exact trigger count, correct alternating images, no BUSY fault and a
repeatable reduction in complete-call time.

### 5. Characterize higher SPI clocks

Run 20, 26.7 and 40 MHz first, then 80 MHz only if the lower profiles remain
clean. Use large deterministic windows, because narrow updates are pull-bound
and cannot expose much transfer improvement. Check both COG halves, the center
seam and the bottom rows with a camera; a completed SPI transaction alone does
not prove correct pixels.

The matching archived large-panel documentation specifies 5 MHz writes. The
existing 20 MHz profile is already a measured 4x interface overclock; 26.7,
40 and 80 MHz are progressively more aggressive overclocks, not steps toward a
vendor-rated target. The newer generation's 20 MHz rating does not apply to
this `969_JS_0B` panel.

Pass gate: zero visible corruption across repeated patterns and a measured
end-to-end gain. Keep 4 MHz as the recovery/default profile and label every
higher setting experimental.

### 6. Map the shortest legal waveform

The current table uses the controller's 20-frame nonzero floor: 19 driven
frames followed by one grounded release frame. A zero timing/count has already
hung BUSY and is not a candidate. The next bounded matrix should change one LUT
timing/repeat/phase field at a time, beginning with one-row and 16-row patterns,
while keeping balanced VDH/VDL transitions and a final ground phase.

The goal is to determine whether a different legal encoding reduces the
approximately 43.6 ms fixed term or the 0.800 ms/row slope. Stop a profile on
the first missing BUSY edge, persistent all-screen tint, lost transition or
unexpected current.

Pass gate: camera-confirmed transitions and at least a 10% reduction in the
electrical pull, repeated in both directions. Contrast loss must be reported
alongside speed.

## Priority

Implement rolling-band playback first. It uses behavior already verified on the
panel and can move local image regions at roughly 10–19 updates/second. Stable
video encoding and dirty metadata multiply that benefit. BUSY-time RAM writes
are the most promising controller experiment. Higher SPI clocks are useful for
large transfers but cannot remove the full-height scan. More TCON/DCTL/DRFW bit
searches should wait unless new IST7136 documentation identifies a specific
field.

Direct source/gate clock control would require bypassing the integrated COG
controller or modifying the panel interconnect; those signals are not exposed
through the badge's SPI interface. That is a hardware-rework project, not a
firmware mode.
