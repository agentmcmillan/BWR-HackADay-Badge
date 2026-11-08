// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {
    BWR_VIDEO_HEADER = 32,
    BWR_VIDEO_FRAME = 4680,
    BWR_VIDEO_MAX_FRAMES = 600,
    BWR_VIDEO_MAX_BYTES = BWR_VIDEO_HEADER + BWR_VIDEO_FRAME * BWR_VIDEO_MAX_FRAMES
};
typedef struct {
    const uint8_t *frames;
    unsigned count, fps_milli;
    // Native: zero means continuous; otherwise hold 1000–30000 ms after shutdown.
    unsigned frame_bytes, hold_ms;
    bool native_ink;
} bwr_video;
uint32_t bwr_video_crc32(const uint8_t *bytes, size_t size);
bool bwr_video_parse(const uint8_t *bytes, size_t size, bwr_video *video);
bool bwr_video_draw(const bwr_video *video, unsigned frame, uint8_t *canvas);

// Optional PackBits envelope for a private built-in clip. Returns decoded bytes or zero.
size_t bwr_video_unpack(const uint8_t *packed, size_t size, uint8_t *output, size_t capacity);
