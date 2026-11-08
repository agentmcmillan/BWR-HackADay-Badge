# TCON and OTP characterization

This record covers one 960 × 672 Pervasive Displays E2969JS0B1 panel driven by
two cascaded IST7136 chip-on-glass devices. The timing-controller function is
embedded in those devices; there is no separate TCON package on the badge.

The experiment answers a narrow question: can the documented OTP-derived
controller settings shorten the electrical pull? It does not identify every
undocumented bit, certify lifetime, or turn the panel into a conventional video
display. Register names below come from the manufacturer application sequence
and the Hackaday badge team's driver reference. No public IST7136 bitfield
manual was found.

The source baseline is pinned to upstream commit
[`9c55a516`](https://github.com/Hack-a-Day/2026-Billboard_Badge/tree/9c55a516632eddfc2fe774e393407c0765e8cb13):
the badge team's
[`itc97-driver-reference.pdf`](https://github.com/Hack-a-Day/2026-Billboard_Badge/blob/9c55a516632eddfc2fe774e393407c0765e8cb13/documentation/itc97-driver-reference.pdf),
[`epd.py`](https://github.com/Hack-a-Day/2026-Billboard_Badge/blob/9c55a516632eddfc2fe774e393407c0765e8cb13/firmware/badge/lib/epd.py)
and
[`rendered_luts.py`](https://github.com/Hack-a-Day/2026-Billboard_Badge/blob/9c55a516632eddfc2fe774e393407c0765e8cb13/firmware/badge/data/rendered_luts.py).

### Vendor-documentation scope

The exact public software selector for this badge is
[`eScreen_EPD_969_JS_0B`](https://github.com/PervasiveDisplays/PDLS_EXT3_Basic_Global/blob/main/src/hV_List_Screens.h):
9.69-inch, `J` (black/white/red) film and controller `0B`. The vendor's
[film table](https://docs.pervasivedisplays.com/knowledge/Software/Volume2/Chapter1/Section6.html#film)
classifies `J` as normal-update film; `P` and `K` are the embedded-fast-update
films.

The current Wide Large documentation instead describes a newer `0x96`/`K`
generation. It is useful as a reference for OTP organization, old/new image
planes and the trigger sequence, but it is not an electrical specification for
this `J/0B` panel. In particular, its 20 MHz write limit must not be relabeled
as this panel's supported limit. The archived matching large-panel material
specifies 5 MHz writes, so the measured 20 MHz badge path is a deliberate 4x
interface overclock.

## Factory OTP read

GPIO47 is the panel's bidirectional three-wire data signal. GPIO45, named MISO
by the stock software, is not connected to the panel. The B1 read sequence is:

1. End any active panel session and perform the normal reset.
2. Send the mandatory `0x70 <- 0x71, 0x36` booster-current configuration to
   both COGs. Do not start the booster or issue a refresh.
3. Select only the master COG and send bare index `0xB9` at 50 kHz.
4. Keep its chip select asserted, turn GPIO47 around, and read one dummy byte
   followed by 128 OTP bytes. The first lab firmware requested 88; firmware
   0.8.58 reproduced the vendor's complete matching `0B` read.
5. Return reset and chip-select pins to the passive post-session state.

Selecting both COGs for step 3 would make two outputs drive the shared data
line. On ESP32-S3, transmit and receive must be separate half-duplex
transactions under the same asserted chip select; the target rejects a combined
TX/RX half-duplex descriptor.

The captured dummy was `0xFF`. All three B1 signatures passed: byte `0x16` was
`0x3B`, byte `0x0D` was `0x3B`, and byte `0x28` had bit 7 set. The complete
128-byte result and its SHA-256 are published in
[`data/tcon-otp-b1-2026-11-08.json`](../data/tcon-otp-b1-2026-11-08.json).
Its first 88 bytes match the earlier capture and SHA-256 exactly. Bytes 88--103
and 118--127 are `0xFF`; bytes 104--117 decode as `TA2969JS0B10LC`. The newly
captured tail is identification/reserved space, not an additional timing table
or fast-mode selector. OTP is panel-specific, so reproduce the read instead of
assuming this record fits another panel. The decoded fields are:

| OTP field | Register | Captured bytes |
| --- | --- | --- |
| TCON | `0x60` | `25` |
| DRFW | `0x90` | `00 3B 00 A8` |
| DCTL | `0x01` | `09` |
| closing value | `0x02` | `00` |
| raw RAM_RW | `0x12` | `00 00 10` |
| raw DUW | `0x13` | `00 3B 00 00 9F 02` |
| STV | `0x61` | `01` |
| MS_SYNC | `0xD8` | `80` |
| BVSS | `0xD6` | `00` |

The factory DC/DC area contains four eight-byte ramp records. It is useful for
startup conformance, but it does not control the duration of a pull in an
already-open animation session.

## Bounded experiment contract

Every register timing trial used the fixed `0x71,0x36` current limit, the same
short differential waveform, one pull, a 5-second BUSY timeout and mandatory
soft shutdown. TCON was `0x25` except where the TCON matrix explicitly varied
it. A centered band was inverted so every accepted request caused a physical
transition. The initial register matrix
conservatively limited row counts to multiples of 16 from 16 through 624 body
rows. A later held-session scan tested 1, 2, 4, 8 and 16 rows. The host stopped
on the first missing reply or panel fault and never reset or retried
automatically.

The first result from a process includes closing the pre-existing UI session in
its total time. `pull_us`, measured from trigger through BUSY completion, is the
comparison metric. Session open/close time is intentionally excluded from that
metric.

Compatible private firmware exposed six fixed verbs:

```text
otp_read
tcon_trial TCON OSC ROWS       # OSC=-1 omits register 0x03
dctl_trial DCTL ROWS           # DCTL=-1 omits register 0x01
drfw_trial END ROWS            # END restricted to 0xA8 or 0xA9
scan_hold ROWS PULLS            # ROWS=1..624, PULLS=1..120
scan_pingpong ROWS PULLS         # preloads two states, then bare triggers
scan_pingpong_edge ROWS PULLS    # same data and waveform, finer BUSY timing
```

These are not arbitrary register-write commands. The complete firmware is not
published here because the upstream badge firmware did not carry a project
license when reviewed. The independently licensed host runner is included.

## Results

Aggregate data is in
[`data/tcon-register-trials-2026-11-08.csv`](../data/tcon-register-trials-2026-11-08.csv).

### TCON and oscillator

The factory TCON byte is `0x25`. A 624-row pull at `0x25` took 543,090 us;
`0x00` took 543,115 us, a 25 us difference. One-bit changes around `0x25`
produced no speedup. Setting bits 1, 3, 4, 6 or 7 was slower in the sampled
128-row trials; clearing bits 0, 2 or 5 was equivalent within run noise.

The speculative oscillator write `0x03 <- 0x00,0x11` also produced no measured
advantage. The B1 application sequence omits it, so the optimized integration
now omits it too.

### DCTL

Factory DCTL is `0x09`; the measured fast-driver baseline omitted register
`0x01`. An initial 128-row screen measured omission, `0x00`, `0x01`, `0x08`
and `0x09` once each at 146,825, 145,436, 145,301, 144,878 and 145,203 us.
`0x01` was not repeated, so it supports only the absence of a gross effect—not
a repeatability claim. A separate interleaved follow-up measured omission,
`0x00`, `0x08` and `0x09` three times each; those follow-up samples form the
four 128-row `dctl` aggregates in the CSV, while the five initial samples are
preserved as `dctl_screen`. No repeated setting separated from run noise. In
full-height A/B trials, omission averaged 542,938 us and factory `0x09`
averaged 543,162 us. The 224 us difference is inside observed jitter and in the
wrong direction for a speed claim.

### DRFW

Factory DRFW ends in `0xA8`; the fast partial driver uses `0xA9`. An interleaved
128-row repeat averaged 145,613 us for `A8` and 145,502 us for `A9`. Full-height
means were 542,641 and 542,582.5 us respectively. Neither difference is
practical or repeatable. Automated camera access was unavailable for the final
coverage check, so `A8` is not adopted without a seam and bottom-edge review.

### Gate-row scaling

The retained `A9` profile was then measured at 16, 32, 64, 128, 256, 384 and
624 rows. Each size ran three times in ascending, descending and ascending order.
No controller fault occurred. A least-squares fit of the seven means is:

```text
pull_us = 43,625.803 + 800.333809 * rows
R^2 = 0.999998075
```

The minimum initially tested 16-row window averaged 56,786 us, a pull-only
ceiling of 17.610 Hz.
The 624-row body averaged 543,020 us, or 1.842 Hz. These are electrical pull
rates, not complete application frame rates; drawing, changed-row detection,
RAM transfer and session management still add time. The near-perfect linearity
shows that the current fast waveform has about 43.6 ms fixed overhead and costs
about 0.800 ms for each selected gate row.

### Sub-16-row held session

Firmware 0.8.48 then removed the procedural 16-row alignment restriction and
kept one powered controller session open. It alternated a centered, full-width
band at 20 MHz with the same TCON `0x25`, DRFW `0xA9`, booster-current limit and
minimum nonzero transition LUT. Windows of 16, 8, 4, 2 and 1 rows each completed
eight pulls without a controller fault. The one-row profile then completed the
maximum bounded 120-pull burst without a reopen or fault.

The 120 measured partial-refresh calls took 6,812,503 us: **17.615 physical
updates/sec within the held session**. BUSY/pull time averaged 43,920.5 us, a
**22.768 Hz electrical phase**; the rest of each measured call was full-frame
changed-pixel scanning, a small RAM write and bookkeeping. Frame generation and
session setup/teardown were outside this timer, so this is not complete
application throughput. The fastest individual measured call was 55,355 us.
Aggregate telemetry is in
[`data/tcon-held-scan-2026-11-08.csv`](../data/tcon-held-scan-2026-11-08.csv).

This proves that 16-row alignment was a test constraint, not a hardware gate
minimum. It does not prove every pixel of a one-row line reached full optical
contrast; the final automated camera path was unavailable. For normal animation,
one-row updates are useful only when the moving content actually fits that row.

### Preloaded trigger-only cadence

The legacy vendor path defines `0x10` as the black plane and `0x11` as the red
plane; it does not document them as the newer generation's old/new fast-update
pair. Our driver experimentally treats the available RAM states as a
differential two-state path and assumes they swap after a pull. Firmware 0.8.49
tested that custom strategy:
prime the inverse band into the second RAM plane once, set the gate window once,
then issue only the two-byte `0x15` command for each subsequent frame. There is
no per-frame drawing, changed-pixel scan or RAM data transfer in the timed loop.

The controller completed every requested BUSY cycle without a fault. Two
independent one-row, 120-trigger runs took 5,218,764 and 5,195,017 us, an
aggregate **23.046 trigger cycles/sec**. The first run's fastest and slowest
cycles were 42,618 and 45,230 us. A single 120-trigger row sweep measured:

| Rows | Trigger cycles/sec |
| ---: | ---: |
| 1 | 22.994 and 23.099 |
| 2 | 22.206 |
| 4 | 21.295 |
| 8 | 20.387 |
| 16 | 17.984 |

This is 30.8% faster than the verified 17.615 Hz one-row path that redraws and
rewrites every frame. The telemetry is published in
[`data/tcon-pingpong-2026-11-08.csv`](../data/tcon-pingpong-2026-11-08.csv).
It proves bounded trigger acceptance, BUSY completion and sustained command
cadence.

A final 16-row run supplied the missing optical check. The controller completed
120 triggers in 6,667,570 us (**17.998 triggers/sec**). In a fixed 30 fps camera
recording, the active interval occupied frames 100 through 299: 200 frames, or
6.667 seconds. The narrow band was measured relative to the background strips
immediately above and below it. After removing cubic drift, its spectrum had a
dominant 9.0 Hz two-state cycle, corresponding to **18.0 visible reversals/sec**
and exactly 120 implied reversals in the interval. The spectral peak was more
than 98 times the median background magnitude. This independently confirms that
each trigger in the 16-row run produced the expected visible state change.

The private recording is not published because it includes the physical test
area. The analysis is reproducible with `tools/analyze_pingpong.py`, and its
synthetic contract test checks the frequency-to-reversal conversion. The
23.046 Hz one-row result remains a controller-cycle ceiling: a one-pixel-tall
line was below reliable camera resolution, so that exact geometry does not have
a separate contrast measurement.

### BUSY completion edge

The original pull helper slept for at least 5 ms, then sampled master BUSY with
an RTOS-tick delay between reads. On this build that delay can span roughly
1–2 ms, so the host notices a completed waveform late. Firmware 0.8.50 added a
bounded comparison that leaves the RAM priming, `0x15` trigger, gate window and
waveform unchanged. The edge path instead verifies that BUSY asserted low,
yields through the first 30 ms of the known waveform, and then samples for the
ready edge every 25 us.

| Rows | RTOS wait | Edge wait | Gain |
| ---: | ---: | ---: | ---: |
| 1 | 22.717 triggers/sec | **24.019 triggers/sec** | 5.73% |
| 16 | 17.744 triggers/sec | **18.632 triggers/sec** | 5.01% |

Across both 120-trigger edge runs, every command
asserted BUSY within 135 us and every BUSY cycle completed. The one-row edge
cycles ranged from 41,592 to 41,685 us; this low spread locates the remaining
floor inside the controller waveform and gate scan, not host scheduling. A
separate eight-frame normal partial-update smoke test also completed after the
edge wait was adopted in that path.

The raw electrical results are in
[`data/tcon-edge-wait-2026-11-08.csv`](../data/tcon-edge-wait-2026-11-08.csv).
The 24.019 Hz one-row result remains a controller-cycle ceiling because one row
is below reliable camera resolution. A separate 0.8.50 16-row run provided an
optical check at the faster edge cadence. The controller completed 120 triggers
in 6,440,927 us (**18.631 triggers/sec**). In the 30 fps recording, the active
interval occupied 193 frames, or 6.433 seconds. Its dominant 9.326 Hz state
cycle corresponds to **18.653 visible reversals/sec** and exactly 120 implied
reversals. The spectral peak was 60.16 times the median background magnitude.
The small rate difference is the camera's frame-time resolution; device and
video counts match exactly.

## Conclusion

None of TCON, oscillator, DCTL or the tested DRFW endpoint is a hidden refresh
multiplier. TCON `0x25` and DRFW `0xA9` remain the measured baseline; the B1
oscillator write is omitted. DCTL may be written as the factory value for
conformance, but the experiment does not support a speed claim.

The successful speed work is elsewhere: experimentally overclocked 20 MHz bulk
SPI, byte-oriented raster operations, overlapped DMA preparation, narrow
changed-row gate scans, a held controller session, completion-edge timing and a
deliberately short transition waveform. The measured row law makes the key
optimization explicit: keep
animation inside the smallest
possible dirty-row window and avoid reopening the controller. For a repeating
two-state effect, preloading both controller RAM states removes all per-frame
RAM traffic and reaches 23.046 trigger cycles/sec at one row. The same mechanism
reaches 24.019 controller cycles/sec with completion-edge timing.
The 16-row run reached 18.631 triggers/sec and was optically confirmed at
18.653 visible reversals/sec. See
[refresh benchmarks](BENCHMARKS.md) and the [refresh pipeline](REFRESH_PIPELINE.md).
The remaining roughly 543 ms full-height pull is controller scan/waveform time,
not host transfer time.

## Reproduce

Install pyserial, connect compatible bounded-test firmware, and run one suite at
a time while the badge is idle:

```sh
python3 -m pip install pyserial
python3 tools/tcon_lab.py /dev/cu.usbmodem101 --version 0.8.45 --suite otp
python3 tools/tcon_lab.py /dev/cu.usbmodem101 --version 0.8.43 --suite tcon
python3 tools/tcon_lab.py /dev/cu.usbmodem101 --version 0.8.46 --suite dctl
python3 tools/tcon_lab.py /dev/cu.usbmodem101 --version 0.8.47 --suite drfw
python3 tools/tcon_lab.py /dev/cu.usbmodem101 --version 0.8.47 --suite scan
python3 tools/tcon_lab.py /dev/cu.usbmodem101 --version 0.8.48 --suite hold
python3 tools/tcon_lab.py /dev/cu.usbmodem101 --version 0.8.49 --suite pingpong
python3 tools/tcon_lab.py /dev/cu.usbmodem101 --version 0.8.50 --suite edge
```

To reproduce the successful optical analysis on a locally held recording:

```sh
python3 -m pip install -r requirements-dev.txt
python3 tools/analyze_pingpong.py badge-crop.mp4 \
  --band 190:399:510:19 --above 190:384:510:12 --below 190:421:510:12 \
  --start-frame 100 --end-frame 300 --expected-triggers 120
```

The 0.8.50 recording used its own fixed framing boxes and active interval:

```sh
python3 tools/analyze_pingpong.py badge-crop.mp4 \
  --band 50:129:600:24 --above 50:112:600:12 --below 50:158:600:12 \
  --start-frame 60 --end-frame 253 --expected-triggers 120
```

The runner writes raw results and serial logs under ignored `artifacts/`. Verify
the exact badge, firmware hash, panel model and power/current limits before a
hardware run. Do not automatically retry a missing reply or BUSY fault. Inspect
the panel after geometry changes; telemetry alone cannot prove pixel coverage.

The next-stage distinction between decodable OTP/LUT data, internal COG behavior
and raw-panel hardware is documented in the
[TCON reverse-engineering plan](TCON_REVERSE_ENGINEERING.md).
