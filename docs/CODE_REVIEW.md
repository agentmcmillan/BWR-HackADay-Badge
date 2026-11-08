# Independent code review — November 2026

A separate review covered public baseline `f89eb25`, including portable C models
and codecs, ESP-IDF adapters, Python USB/camera tools, browser conversion/client
code, tests, dependency notices, documentation and publication scope. It did not
access the badge, camera or serial port, and did not review the omitted complete
badge OS or certify the panel's electrical operating limits.

The review produced these fixes:

- **P2 — built-in clip identity:** equal frame counts could select the wrong
  private clip for a benchmark. Status now includes the validated payload CRC-32;
  the host requires both frame count and checksum to match the local reference.
- **P2 — cancellation ordering:** a normal stop could overwrite an earlier exit
  request. Exit requests now stay latched in both benchmark mailboxes.
- **P2 — DMA buffer aliasing:** unequal but overlapping buffers could corrupt an
  in-flight transfer. The stream helper rejects overlap between the two buffers
  or with the source frame before invoking callbacks.
- **P2 — browser upload ownership:** concurrent image/settings/photo uploads could
  replace one another's firmware transaction. The browser client serializes
  uploads, and an old session's failure cannot send abort to a replacement session.
- **P2 — clock-selection failure:** app adapters ignored failed clock selection,
  potentially measuring a different profile. They now stop without starting the
  burst and restore normal cleanup behavior.
- **P3 — shared state and fail-stop handling:** cross-task game-state flags in the
  driver reference are atomic; a faulted host RPC object rejects further commands.
- **P3 — publication hygiene:** private PackBits clips and additional common video
  formats are ignored. The pinned vendor bundle matches its manifest SHA-256 and
  its notices remain unchanged. No credentials, personal device IDs, raw media,
  full firmware binaries or omitted upstream source were found in tracked files.

Regression tests exercise clip identity, terminal exit ordering, source/buffer
alias rejection, concurrent uploads, stale-session abort exclusion, failed video
clock selection and the actual app sources including their public port header.
Host adapter compilation uses minimal SDK declarations; it is not an ESP-IDF
hardware build. Sanitizer-backed C tests, Python checks and JavaScript tests are
part of validation. New preprocessing options must receive their own review and
tests before publication.

No unresolved release-blocking issue was found in the reviewed public subset.
Known limits remain documented: a complete firmware needs a separately permitted
host implementation, raw camera evidence stays local, and experimental refresh
profiles retain visible ghosting and unverified electrical/lifetime margins.

The subsequent light-grid preprocessing option was reviewed separately. Tests
cover default and maximum grid bounds, deterministic/source-immutable output,
zero-light cells at exactly aligned raster centers, temporal persistence and
converter integration with the existing file header, CRC and bit order. The
review corrected a zero-radius pinhole at aligned cell centers and rejects
non-integer grids or empty/oversized frame sequences. No artist imagery or music
video is included in that change.

The native ink-memory extension received a subsequent review of its exact-size
960 × 624 codec, 36-pose/hold limits, status-bar preservation, session shutdown,
last-image lifecycle and error paths. Follow-up fixes reject infinite source
start times, retain pure black/white stipple endpoints, and distinguish a
cancelled final hold from a completed paced run. Tests cover native/legacy host
parsing, whole-file timing identity, full-resolution pixel detail, converter
PackBits/CRC compatibility, camera-loss cleanup and successful leave-ink behavior.
No hardware validation is implied by those software checks.

The continuous native option permits only an exact zero hold or the existing
1,000–30,000 ms held range. Tests verify that zero-hold native frames share the
open session, add no blank/result redraw, and still close on completion or
frame-boundary Home/stop/Fresh-sheet actions. Converter and both parsers reject
nonzero sub-second holds. Session-limit enforcement remains in the display driver.

## Readability and stability pass

A later pass reviewed the whole public tree for failure handling and how easy it
is to read. Fixes:

- **Web Serial transport:** a single malformed base64 response used to tear down
  the whole connection. Now it fails only its own request. When the device
  disconnects, the writer is released and the port is closed. After a successful
  connection, a settings-load failure no longer looks like a port conflict. The
  fetch shim clamps badge-supplied status codes to a valid error range.
- **Host tools:** `record_refresh.py` recognizes `Panel BUSY timeout` as a panel
  fault. On an exception or Ctrl-C it records `status: "error"` instead of
  leaving `waiting`. `badge_rpc.py` and `auto_refresh.py` adopt the session only
  after the firmware version matches, so cleanup never commands unexpected
  firmware. `--help` now shows each tool's description.
- **Video adapter:** a cancelled native hold leaves the frame loop directly
  instead of relying on the next iteration's stop check.
- **Readability:** raster code uses named canvas constants and documents its
  box layout. The two changed-box scanners share one helper. The video header
  documents both clip formats and every field. Refresh Lab's clock lookup is
  bounded. The game-tag full-refresh interval is named. CI checks Black
  formatting.

The host tests still pass. Known, unchanged limits: the restore-status poll has
no deadline, and the Home-ready wait gives up quietly after 20 s.

