# IST7136 command and register catalog

This is the complete catalog of controller indices known to this project at
release 1.0.0 for the E2969JS0B1 panel's two cascaded IST7136 chip-on-glass
(COG) devices. It combines the matching vendor sequence, the badge driver,
the decoded 128-byte factory OTP record and the bounded experiments described
in [TCON and OTP characterization](TCON_LAB.md).

An SPI index is called a command or register here according to its observed
use. The distinction is not documented for every index. This table is **not**
a complete `0x00`--`0xFF` silicon register map: no public IST7136 bitfield
manual or evidenced arbitrary-register read command is available. Unlisted
indices are unknown, not implicitly safe or writable.

## Known indices

| Index | Project name and observed role | Known payload or transfer | Target | Evidence and status |
| --- | --- | --- | --- | --- |
| `0x01` | DCTL | Factory `09`; omission and `00`, `01`, `08`, `09` tested | both | Decoded from OTP and measured. No repeatable refresh-rate gain. |
| `0x02` | Closing/VCOM setting | `00` | both | Decoded from OTP and present in the application sequence; individual bit meanings unknown. |
| `0x03` | Oscillator candidate | `00 11` in the experimental case | both | Speculative B1 write. No measured advantage over omission, so the retained path omits it. |
| `0x05` | Register-load and power sequencing | `7D`, then `00`, in the inherited start path; `3D` in the model-matched shutdown | both | Sequence-level meaning is established; individual bits are not decoded. |
| `0x09` | DC/DC and power-state sequencing | Observed values `1F`, `9F`, `7F`, `FF`, `7E`, `00` | both | Used only as part of the documented ramp and shutdown sequences. Individual bits are not decoded. |
| `0x10` | Black/image RAM plane | Window-sized pixel bytes after `0x12`/`0x13` | one COG at a time | Matching legacy J-film path calls this the black plane. Also used as one state in custom monochrome differential playback. |
| `0x11` | Red/image RAM plane | Window-sized pixel bytes after `0x12`/`0x13` | one COG at a time | Matching legacy J-film path calls this the red plane. Experimentally used as the second state in custom monochrome differential playback; it is not a documented old/new fast pair on this film. |
| `0x12` | RAM_RW pointer | Three bytes: horizontal pointer, low row byte, high row/control byte | one COG at a time | Decoded from OTP and validated by bounded window writes. |
| `0x13` | DUW data-update window | Six bytes: horizontal and vertical window limits | both or one COG | Decoded from OTP and validated for full and partial windows. |
| `0x15` | Display-update trigger | `3C` | both | Only proven trigger payload. Each accepted command is checked through BUSY assertion and completion. |
| `0x44` | OTP/temperature-load selector | `00`, then `06` in the inherited load sequence | one COG at a time | Observed application sequence; bit meanings are not publicly decoded. |
| `0x45` | OTP/temperature-load value | `80`, then `84` for the badge's 26 °C path | one COG at a time | Observed application sequence; treated as temperature input only in that sequence. |
| `0x51` | DC/DC soft-start ramp data | Two-byte ramp points, beginning with `50 01` | both | Used only with the `0x09` staged power sequence. |
| `0x60` | TCON | Factory `25`; `00`, `05`, `21`, `24`, `27`, `2D`, `35`, `65`, `A5` tested | both | Decoded from OTP and measured. `25` remains the baseline; tested alternatives did not reduce the scan floor. Bit meanings remain unknown. |
| `0x61` | STV direction | `01` | master only | Decoded from OTP and retained unchanged. |
| `0x70` | B1 booster-current configuration | `71 36` | both | Mandatory badge/B1 safety setting used before controller operation. It is not a speed control. |
| `0x90` | DRFW display-refresh window | Factory `00 3B 00 A8`; custom partial path ends in `A9` | both | Decoded from OTP and measured. `A8` versus `A9` did not materially change BUSY time; row limits do. |
| `0xA7` | Register/OTP load strobe | `10`, delay, then `00` | both or one COG | Used to commit the global and per-COG load steps. It is not used as a refresh trigger. |
| `0xB0` | Waveform table | 400 bytes: five state tables × ten groups × eight bytes | both | Layout is decoded and custom bounded waveforms are tested. Zero-count forms can leave BUSY stuck; minimum known nonzero timing remains the safe experimental floor. |
| `0xB9` | Factory OTP read opener | Bare index at 50 kHz, then one dummy byte and 128 data bytes | master only | Reproduced on hardware. Never select both COGs while reading the shared data line. |
| `0xD6` | BVSS | `00` | both | Decoded from OTP and retained unchanged. Bit meanings are unknown. |
| `0xD8` | MS_SYNC | `80` | both | Decoded from OTP and retained unchanged. Bit meanings are unknown. |

## Coverage boundary

The catalog covers every literal controller index used by the release's known
driver and lab paths. The full factory OTP image is preserved in
[`data/tcon-otp-b1-2026-11-08.json`](../data/tcon-otp-b1-2026-11-08.json),
including fields that are not understood well enough to assign bit names.
Measured value matrices and timing outcomes are in
[`data/tcon-register-trials-2026-11-08.csv`](../data/tcon-register-trials-2026-11-08.csv).

The following remain unknown:

- bit-level definitions for TCON, DCTL, DRFW and the power-sequence values;
- whether any non-OTP index supports reliable readback;
- the meaning and safety of unobserved indices;
- internal sequencer state, scan-engine microarchitecture and reset logic.

The project therefore does not sweep arbitrary addresses or publish guessed
names as facts. The next bounded experiments and stop conditions are recorded
in the [TCON reverse-engineering plan](TCON_REVERSE_ENGINEERING.md).
