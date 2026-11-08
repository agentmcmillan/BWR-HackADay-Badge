# Adaptive refresh scheduler

`bwr_band_scheduler` is a portable policy layer for animation adapters. It does
not issue panel commands or modify the app's desired framebuffer. Given desired,
visible and optional previous-desired states, it selects one fixed-grid
horizontal band for the next physical update.

Selection combines current pixel error, desired-scene motion and band age. A
dirty band reaching `max_age` takes priority over newer work, so a scene that
stops changing converges exactly and continuously moving content cannot starve
an older region indefinitely. Ties resolve toward the top of the panel.

The returned plan includes its row interval, selected and remaining error bits,
motion bits, age, selection score and predicted controller time. For the measured
E2969JS0B1 panel, use a starting cost model of 43,626 us fixed plus 800 us per
gate row. `bwr_band_apply` constructs a staging framebuffer equal to the visible
state except for the selected rows copied from the desired state. The existing
changed-window driver can then derive exact COG byte boxes and issue one narrow
pull.

## Adapter contract

1. Keep separate desired, visible and staging buffers.
2. Call `bwr_band_scheduler_next` only when prepared to execute its plan.
3. Apply the plan to staging and refresh staging normally.
4. Only after successful BUSY completion, copy the selected staging rows into
   visible. A panel failure remains a fail-stop; do not retry a planned pull.
5. Preserve the previous desired scene only when motion weighting is wanted.
6. Before a static screen closes its controller session, drain plans until
   desired equals visible or use the ordinary synchronous full-state path.

The scheduler is intentionally opt-in. Applying one band to a screen that draws
only once would leave it incomplete. Continuous video and screensavers are the
first candidates; games with pause/win screens need an explicit drain before
they stop rendering. Menus, nametags, settings and other static screens should
retain the ordinary path until their lifecycle supplies that drain.
