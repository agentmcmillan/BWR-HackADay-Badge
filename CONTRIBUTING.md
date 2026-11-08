# Contributing

Keep changes small enough to review. Describe the behavior being changed and
include the relevant host test command and result. Do not describe a simulated
or source-derived test as a physical panel measurement.

Format project C and headers with `.clang-format`; keep third-party source and
its notices unchanged. Format Python tools with Black. Prefer descriptive names,
explicit bounds and short functions. Comments should explain hardware constraints
or a decision that would otherwise be surprising.

Panel changes need a clear distinction between bus speed, waveform duration,
visible settling and ghosting. Keep the default clock and validated power/BUSY
handling separate from experimental profiles. Record the exact firmware version,
pattern, frame count and actual configured clock with any hardware result.

Do not commit personal Wi-Fi defaults, flash backups, uploaded photos, camera
recordings, generated binaries or device-specific logs. Use anonymized aggregate
results in documentation. Check `docs/LICENSING.md` before redistributing an
upstream-derived file whose license is unresolved.
