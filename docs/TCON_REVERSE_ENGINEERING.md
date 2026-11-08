# TCON reverse-engineering plan

The E2969JS0B1 is not a raw TFT panel connected to a separate timing-controller
package. Its two IST7136 chip-on-glass (COG) devices contain the SPI command
decoder, image RAM, waveform sequencer, source drivers and gate timing. The
badge can configure and observe that machinery, but it cannot download an
ordinary firmware image from it.

This changes what “decompile the TCON” can mean. We can decode the external data
structures and infer the internal state machine from controlled experiments. We
do not currently have machine code to disassemble.

## What is available

### Factory OTP

The master COG returns one dummy byte followed by OTP data through the
documented B1 `0xB9` opener. The complete 128-byte record and checksum are in
[`data/tcon-otp-b1-2026-11-08.json`](../data/tcon-otp-b1-2026-11-08.json).
Known fields include TCON, DCTL, DRFW, RAM and display windows, STV, two unnamed
settings and four DC/DC ramp records.

Pervasive Displays' exact legacy `eScreen_EPD_969_JS_0B` path reads 128 bytes,
which firmware 0.8.58 reproduced at 50 kHz with the booster off. The original
88-byte prefix matched the earlier capture and SHA-256 exactly. In the new tail,
bytes 88--103 and 118--127 are `0xFF`; bytes 104--117 contain the ASCII panel
identity `TA2969JS0B10LC`. The tail exposes no additional timing fields or
fast-mode selector.

OTP is configuration, not executable code. It describes how the generic COG is
initialized for this panel and how its booster ramps. The values are
panel-specific and must not be assumed to fit another unit.

The panel code is `969_JS_0B`: `J` is a normal-update black/white/red film in
the vendor's
[screen constants](https://docs.pervasivedisplays.com/knowledge/Software/Volume2/Chapter1/Section6.html#film).
The newer `P` and `K` paths provide embedded fast update. Their old/new-plane
ordering is a useful design reference, but their temperature-index and factory
waveform rules must not be copied onto this `J/0B` panel by command number.
For the matching legacy J-film path, `0x10` is the black plane and `0x11` the
red plane. The repository's differential two-state use is experimentally
verified custom behavior, not the vendor's factory fast-update mode.

The vendor's OTP map makes that distinction testable: offset `0x1E` is the
flags byte, with `0xFE` identifying embedded fast update. The captured panel
contains `0xFF` there. Changing a RAM register cannot rewrite fused OTP, and no
public `J/0B` driver exposes a mode bit that turns this normal-update film into
the `P` or `K` fast path. The public matching driver always refreshes with
`0x15 <- 0x3C`; both its `UPDATE_FAST` and `UPDATE_GLOBAL` API cases ultimately
call the same panel flush, while temperature gating restricts embedded fast
update to `P` and `K` film.

The same matching driver does expose one useful model-specific distinction:
`eScreen_EPD_969_JS_0B` shuts down with `0x09 <- 0x7F`, `0x05 <- 0x3D`, `0x09
<- 0x7E`, a 15 ms wait and `0x09 <- 0x00`. The older `08` path uses `0x7D` and
200 ms. This improves session turnover and restores the documented electrical
sequence, but it cannot reduce the internal scan time of a refresh.

The vendor has suspended general partial-update support because unchanged
pixels can still degrade and panel life can be reduced. The experiments here
intentionally continue beyond that supported envelope, so every partial or
rolling-band result must be described as experimental rather than a production
lifetime claim. See the vendor's
[update-method summary](https://docs.pervasivedisplays.com/knowledge/Technology/updating-the-display.html).

### Waveform data

Register `0xB0` accepts 400 bytes on this B1 COG: five pixel-state tables, ten
groups per table and eight bytes per group. A group is:

```text
level, on, off, phase-2, on, off, phase-2, repeat
```

The level byte selects GND, VDH, VDL or VDHR for four phases. The remaining
bytes are timing and repetition fields. This is microcode-like data in the
sense that it schedules rails, but it is not an instruction set and contains no
branches, addresses or program text to disassemble.

The upstream
[`itc97-driver-reference.pdf`](https://github.com/Hack-a-Day/2026-Billboard_Badge/blob/9c55a516632eddfc2fe774e393407c0765e8cb13/documentation/itc97-driver-reference.pdf)
already decodes this layout from the badge driver. The current partial waveform
uses the minimum known nonzero timing in all ten groups: 19 driven frames plus a
grounded release frame. Zero is not an empty timing value; tested zero-count
forms leave BUSY asserted and require a controller reset.

### Observable behavior

The host can record:

- every SPI index and payload on SDA/SCL/DC/CS;
- master and slave BUSY timing;
- the master/slave cascade nets FSYNC, LNSYNC and CLK with passive probes;
- NGDRV and the external booster rails with correctly rated probes;
- supply current, panel temperature and optical transitions.

The measured gate law is currently:

```text
pull_us = 43,625.803 + 800.333809 * selected_rows
```

The nearly exact linear fit is evidence of a fixed per-trigger sequencer cost
plus a per-gate-row scan cost. Passive FSYNC/LNSYNC/CLK capture can test that
model directly.

## What is not available

- No public IST7136 register bitfield or internal architecture manual has been
  found.
- There is no evidenced general register-read command. The bidirectional SDA
  path is proven for OTP, not for arbitrary register readback.
- The COG command decoder, scan sequencer and reset logic are not present in the
  ESP32 firmware or external flash.
- FSYNC, LNSYNC and CLK are cascade connections between the COGs. They are not a
  raw framebuffer bus, and driving them without first isolating the active COG
  outputs would create contention.
- Replacing the integrated scan engine with ESP32 bit-banging would require
  panel-interconnect surgery or different source/gate hardware. It is not a
  driver-mode switch.

Accordingly, the first objective is a behavioral model with verified field
meanings—not an unsupported claim that the silicon firmware was dumped.

## Experiment rules

Every active trial should retain these controls:

1. Change one field or command byte from a recorded baseline.
2. Use the mandatory B1 booster configuration `0x70 <- 0x71,0x36`.
3. Start with one row, one pull and a deterministic black/white transition.
4. Require BUSY to assert and complete within a bounded deadline.
5. Soft-shut down normally after a successful result.
6. Do not automatically reset or retry a missing BUSY edge.
7. Interleave a known baseline before and after every candidate.
8. Advance from 1 to 8 pulls, then to 120 only after electrical and optical
   review.
9. Record exact firmware/image hashes, register sequence, temperature, current,
   trigger-to-BUSY assertion, BUSY duration and camera evidence.

Stop immediately on a missing BUSY assertion, a BUSY timeout, an unexpected
current step, rail sag/overshoot, a lost COG half, a new seam or row-coverage
error, persistent full-screen tint, or a transition that telemetry reports but
the camera does not see. A panel fault is an electrical investigation, not
permission to reboot repeatedly into the same profile.

## Ranked experiment matrix

### 1. Passively capture the internal cadence

Probe master BUSY, FSYNC, LNSYNC, cascade CLK and NGDRV during the existing
1/2/4/8/16/64/624-row profiles. Correlate line pulses and clocks with the
43.6 ms intercept and 0.800 ms/row slope. Start with high-impedance passive
observation; do not drive a cascade net.

This has no direct speed gain, but it is the highest-information experiment. It
can establish whether the fixed term is waveform-group setup, an internal frame
count or booster behavior, and whether the row slope is literally a repeatable
line cadence. Use a logic analyzer fast enough for the observed cascade clock
and an oscilloscope for NGDRV/rails; the existing 30 fps camera is adequate for
slow profiles, but 120 fps or faster is preferable above 15 visible reversals/s.

### 2. Map B1 waveform timing arbitration

The current table gives every pixel table and VCOM the same minimum nonzero
timing. Build a fixed candidate matrix that changes exactly one group field:

- raise one `on`, `off`, phase-2 or repeat field from 1 to 2;
- apply that change separately to paper, ink, unused table 3 and VCOM;
- repeat in group 0, a middle group and group 9;
- compare all-GND levels against balanced VDH/VDL levels while timing stays
  fixed.

This reveals whether group duration is selected from a particular table, the
maximum across tables, VCOM, or another internal rule. It also distinguishes
timing from electrical dose. Do not include zero counts: zero has already
failed as a disable encoding.

Only after the arbitration rule is reproduced should a shorter legal encoding
be attempted. A useful result must reduce either the fixed term or row slope by
at least 10%, preserve both transition directions, retain a final ground phase
and pass camera review. Because every known count is already at its minimum
nonzero value, the probability of a large win is lower than for rolling-band
rendering.

### 3. Test bounded register readback

With the booster off and only the master COG selected, try the same
turnaround/read shape used by OTP on known, otherwise harmless indices first:
TCON `0x60`, DCTL `0x01`, DRFW `0x90`, RAM pointer `0x12` and DUW `0x13`.
Read a fixed small length at 50 kHz and compare repeated captures against an
open-bus `0xFF` control. Never select both COGs during a read.

If every result is pull-up data, stop; do not sweep all 256 indices. If a known
field returns a stable expected value, reproduce it after changing that field
and only then expand a small whitelist. A genuine read path would materially
improve state-machine mapping, but it is not yet evidenced.

### 4. Overlap inactive-plane writes with BUSY

Start a known narrow pull, wait for BUSY assertion, then attempt a bounded write
to the plane expected to be inactive. After BUSY completes, trigger two known
states and verify both optically. Test one, 16 and 64 rows.

This is the highest-payoff command-sequencer experiment for application speed:
if accepted, RAM transfer and rendering can overlap the electrical pull. It
cannot make the waveform itself shorter. At 20 MHz it can at most remove the
remaining write/host portion of a call; full-height playback remains dominated
by the roughly 543 ms pull.

### 5. Characterize the `0x15` trigger payload

`0x15 <- 0x3C` is the only proven trigger. Before trying alternatives, capture
its SPI, BUSY and cascade signature precisely. Then use a small, reviewed
one-bit matrix around `0x3C`, one pull per boot/session, with a one-row all-GND
waveform followed by a balanced optical trial only for candidates that complete
normally.

This has lower confidence and higher unknown-mode risk than LUT mapping. Do not
perform a blind 0–255 sweep. A candidate is useful only if it produces a
repeatable shorter BUSY interval and a visible bidirectional transition rather
than merely skipping part of the update.

### 6. Separate transfer overclocking from TCON work

The matching archived large-panel material specifies 5 MHz writes. The newer
Wide Large generation permits 20 MHz writes and 2.5 MHz reads, but uses a
different controller and film. Our existing 20 MHz path is therefore a measured
4x interface overclock for this panel, not a vendor-supported ceiling. Higher
SPI clocks can only reduce RAM-write time; they do not change the internal gate
scan. Use deterministic full windows to expose transfer errors, inspect both
halves and the center seam, and retain 4 MHz as the recovery profile. This is
host-interface characterization, not TCON decompilation.

### 7. Consider cascade injection only on sacrificial hardware

After passive capture has identified FSYNC/LNSYNC/CLK direction and voltage,
hardware isolation could test whether an external source can alter line cadence.
That requires cutting or lifting cascade connections so the COG and test source
never drive the same net. It also requires rail/current instrumentation and a
recovery path independent of the modified badge.

Even successful cascade injection would not expose a raw pixel bus: image data
still resides in the two COG RAMs, and the integrated source/gate drivers retain
their analog timing requirements. This is a separate board-rework project and
should not be attempted on the only working badge.

## Instrumentation and records

Use synchronized evidence wherever possible:

- logic capture of DC, SDA, SCL, both CS lines, both BUSY lines and any probed
  cascade nets;
- oscilloscope/current capture of VCC_EPD, NGDRV and the external booster,
  using probes rated for the panel rails;
- fixed-exposure optical capture with ROIs over both COG halves, the seam and
  the active row band;
- device telemetry for bytes, transactions, actual SPI clock, BUSY assertion,
  BUSY completion and session reopen count;
- an immutable manifest containing firmware, result, trace and analysis hashes.

The host runner should accept only named profiles compiled into the test
firmware. It should not expose arbitrary register writes, arbitrary LUT bytes or
unbounded pull counts over USB.

## Decision points

The current evidence already closes several branches: TCON `0x60`, the
speculative oscillator write, DCTL and DRFW `0xA8`/`0xA9` are not refresh-rate
multipliers. Repeating those matrices without a new hypothesis is unlikely to
help.

The next sequence should therefore be:

1. finish rolling-band/adaptive rendering;
2. passively capture internal cascade cadence;
3. map LUT timing arbitration;
4. test BUSY-time inactive-plane writes;
5. attempt a whitelisted register readback;
6. consider trigger-payload variants only if the earlier captures provide a
   specific hypothesis.

This order gives the software path the measured 10–19 local updates/s first,
then uses the TCON work to attack the remaining electrical floor with evidence
rather than a random register search.
