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
