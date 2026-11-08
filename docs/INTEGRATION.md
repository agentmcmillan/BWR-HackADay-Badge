# Firmware integration

This is an additions/component repository, not a complete flashable badge OS.
Obtain a host firmware with permission to use it, or implement a host yourself.
The original 2026 badge hardware uses two controllers and a 960 × 672 framebuffer.
The top 48 rows belong to the host's button/status bar.

## Portable component

Add this repository's `components` directory to `EXTRA_COMPONENT_DIRS` before
ESP-IDF's project include. The component builds the compositor, shape model,
statistics and FreeRTOS-protected remote mailbox. Keep `vendor/` alongside the
component directory because it supplies the bitmap font.

`shapes_init`, `shapes_step` and `shapes_render` implement deterministic models
from a supplied seed. Use `shapes_set_tag` to change the native white-area
geometry. Spirograph reprojects the same curve and phase instead of reseeding.
The optional legacy rectangular obstacle is off by default.

`game_tag_compose_named` fits a complete logical game canvas into the white
band with preserved proportions and forward-mapped strokes. The input is not
modified. Native Spirograph/DVD views use the full white-band width and must
already obey its vertical bounds. Names are limited to 12 printable ASCII
characters; unsupported bytes become question marks. A missing name uses
`YOUR NAME`.

## App and panel port

Implement the declarations in `examples/esp-idf/bwr_badge_port.h` in the host.
Add the app adapter source files to the host's UI task. Do not draw from the USB
command task. The 1-bit canvas is MSB-first; set bits mean black.

`driver_hooks.inc` contains only the original hook additions. It is a porting
reference to include in the same translation unit as a compatible host driver,
not a standalone driver. Its referenced host state/functions are:

- A panel mutex, `fb`, `part_shown`, `panel_red_image`, `film_has_red`,
  `film_clean_white`, `panel_image_valid`, overlay/footer flags, and `home_enabled`.
- `draw_bar`, `epd_partial`, `epd_sleep/reset/load_planes/start/pull/close`,
  `cfg_set`, `ui_led_activity`, and the host's `ms`/`btn_flush`.
- A button queue `btnq` and `in_app` for the dedicated Home launch event.

Before invoking a game, call `ui_game_mode(true)`; after it returns, call
`ui_game_mode(false)`. Route game frames to `game_tag_present_locked(false,false)`
while holding the panel lock. Pass `true` for a forced color commit. Non-game
frames keep the host's normal path. Preserve the host's pigment persistence
marker before the first red write; only record a completed image after BUSY
completion. A game-over wipe must preserve the selected tag setting.

Route an actual A+B chord to `ui_game_tag_toggle` at a frame boundary. Preserve
single-button gameplay, and do not consume late combinations such as a held
thrust followed by fire. Fixed-rate local game loops wait through
`ui_game_frozen()` and rebase their tick clock afterward. Keep Pong/link
heartbeats serviced even while display-related local physics are paused.

## Refresh instrumentation

Keep the host's power sequence, waveform, BUSY timeout handling and shutdown
policy unchanged. The reference integration uses a fresh partial session after
60 pulls. Selecting a clock closes the previous session first.

In the SPI transmit helper, set `spi_transaction_t.override_freq_hz` only when
`panel_rate_pending` is true. After a successful transfer, query
`spi_device_get_actual_freq`, clear the pending flag, and count the transmitted
bytes/transactions when `panel_measure` is set. Restore the 4 MHz request when
leaving the lab; the next transaction commits that divider.

In each partial refresh, snapshot counters and time these phases independently:
frame diff, session open/reopen, RAM/window writes, and pull/BUSY completion.
Publish one `refresh_sample` only after completion. An unchanged scene yields
but performs no pull; mark it `changed=false` with zero transfer/pull counts.
Do not describe a cached API call rate as screen FPS.

## USB control

Use the authenticated stop-and-wait protocol in [USB_BENCHMARK_PROTOCOL.md](USB_BENCHMARK_PROTOCOL.md).
Call the mailbox only after validating the session and bounded numeric fields.
The USB handler may queue a request, snapshot status, or request cancellation;
it must never call panel functions directly.

`ui_refresh_lab_open` queues the dedicated `BTN_REFRESH_LAB` event from Home.
The Home task resolves that event to the Refresh app; it is not a general remote
button/code execution API. Wait until Home has completed its initialization
and input flush before submitting the launch request.

Allowed requests are five patterns, the 4/5/8/12/20 MHz clock list and 1–60 frames.
8/12/20 MHz are experimental overclocks for this older panel family. A normal
exit restores the default clock and shortcut behavior. A controller BUSY fault
stays a fail-stop; do not add automatic reset/retry on a fault.
