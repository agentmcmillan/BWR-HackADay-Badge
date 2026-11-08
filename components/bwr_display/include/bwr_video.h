// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Two clip formats share one 32-byte little-endian header (see docs/VIDEO.md and
// docs/INK_MEMORY.md):
//   BWRVID1 - 240 x 156 2-bit-pair frames, expanded 4x to fill the playfield.
//   BWRINK1 - native 960 x 624 frames ("poses") with an optional hold after each.
// Frames are drawn below the 48-pixel status bar of the 960 x 672 canvas.
enum {
    BWR_VIDEO_HEADER = 32,
    BWR_VIDEO_FRAME = 4680,
    BWR_VIDEO_MAX_FRAMES = 600,
    BWR_VIDEO_MAX_BYTES = BWR_VIDEO_HEADER + BWR_VIDEO_FRAME * BWR_VIDEO_MAX_FRAMES
};
// A parsed clip. `frames` points into the caller's buffer; nothing is copied.
typedef struct {
    const uint8_t *frames; // first frame, directly after the header
    unsigned count;        // number of frames
    unsigned fps_milli;    // BWRVID1 only: source frame rate x 1000 (1500 = 1.5 fps)
    unsigned frame_bytes;  // bytes per frame (4680 or 74880)
    // BWRINK1 only: zero means continuous; otherwise hold 1000-30000 ms after shutdown.
    unsigned hold_ms;
    bool native_ink; // true for BWRINK1
} bwr_video;

// Standard CRC-32 (IEEE 802.3, as zlib.crc32).
uint32_t bwr_video_crc32(const uint8_t *bytes, size_t size);
// Validate magic, dimensions, limits, exact size and payload CRC. On success fills `video`.
bool bwr_video_parse(const uint8_t *bytes, size_t size, bwr_video *video);
// Draw one frame into a 960 x 672 canvas below the status bar. False if out of range.
bool bwr_video_draw(const bwr_video *video, unsigned frame, uint8_t *canvas);

// Optional PackBits envelope for a private built-in clip. Returns decoded bytes or zero.
size_t bwr_video_unpack(const uint8_t *packed, size_t size, uint8_t *output, size_t capacity);
