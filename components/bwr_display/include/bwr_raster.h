// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// MSB-first 960 x 672 monochrome canvas, 120 bytes per row.
void bwr_fill_rect(uint8_t *pixels, int x, int y, int width, int height, bool ink);
// Bounding box in byte columns of the selected half (0=left, 1=right).
// Empty result has x1 < x0. Inputs are never modified.
void bwr_changed_box(const uint8_t *a, const uint8_t *b, unsigned half, int bounds[4]);

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

// Scan both controller halves in row order, keeping shared cache lines hot.
void bwr_changed_boxes(const uint8_t *a, const uint8_t *b, int bounds[2][4]);
// Same scan, restricted to an inclusive row interval already proven by the
// caller to contain every change.
void bwr_changed_boxes_rows(const uint8_t *a, const uint8_t *b, int first_row,
                            int last_row, int bounds[2][4]);
