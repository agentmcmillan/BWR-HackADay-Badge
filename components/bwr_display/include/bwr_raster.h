// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Raster helpers for the badge's monochrome canvas.
//
// Canvas layout: 960 x 672 pixels, 1 bit per pixel, MSB-first (bit 7 of byte 0
// is the top-left pixel), 120 bytes per row. The panel is driven by two
// controllers, each owning one 480-pixel (60-byte) half of every row.
enum {
    BWR_CANVAS_W = 960,
    BWR_CANVAS_H = 672,
    BWR_ROW_BYTES = BWR_CANVAS_W / 8,
    BWR_HALF_BYTES = BWR_ROW_BYTES / 2,
    BWR_CANVAS_BYTES = BWR_ROW_BYTES * BWR_CANVAS_H
};

// A changed-region box. Columns are byte offsets within one controller half
// (0..59); rows are pixel rows (0..671). All four bounds are inclusive.
enum { BWR_BOX_COL0, BWR_BOX_COL1, BWR_BOX_ROW0, BWR_BOX_ROW1 };

// Set (ink=true) or clear every pixel of a rectangle; parts off the canvas are clipped.
void bwr_fill_rect(uint8_t *pixels, int x, int y, int width, int height, bool ink);

// Smallest box covering every byte that differs between canvases a and b in the
// selected half (0=left, 1=right). An empty result has bounds[COL1] < bounds[COL0].
// Inputs are never modified.
void bwr_changed_box(const uint8_t *a, const uint8_t *b, unsigned half, int bounds[4]);

// Stream the window `bounds` of one half of `source` to the panel in chunks of up
// to `capacity` bytes, alternating between the `first` and `second` buffers.
// Bytes are bit-reversed when `reverse_bits` is set. Returns false (without
// calling either callback) for invalid arguments.
//
// start() retains the buffer until finish(). Both callbacks must complete
// normally. Two distinct DMA-capable buffers let preparation overlap the
// preceding transfer. The buffers must not overlap each other or the source frame;
// overlapping ranges are rejected before any callback is invoked.
typedef struct {
    void (*start)(void *context, const uint8_t *bytes, size_t count);
    void (*finish)(void *context);
    void *context;
} bwr_stream_ops;
bool bwr_stream_window(const uint8_t *source, unsigned half, const int bounds[4], uint8_t *first,
                       uint8_t *second, size_t capacity, bool reverse_bits,
                       const bwr_stream_ops *ops);

// Same as bwr_changed_box for both halves at once (bounds[half]), scanning in row
// order so each row's cache lines are read only once.
void bwr_changed_boxes(const uint8_t *a, const uint8_t *b, int bounds[2][4]);
