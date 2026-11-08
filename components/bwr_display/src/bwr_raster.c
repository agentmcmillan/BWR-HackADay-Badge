// SPDX-License-Identifier: GPL-3.0-or-later
#include "bwr_raster.h"
#include <string.h>

void bwr_fill_rect(uint8_t *pixels, int x, int y, int width, int height, bool ink)
{
    if (width <= 0 || height <= 0)
        return;
    // 64-bit edges: x + width must not overflow for callers passing extreme values.
    int64_t right = (int64_t)x + width, bottom = (int64_t)y + height;
    if (x >= BWR_CANVAS_W || y >= BWR_CANVAS_H || right <= 0 || bottom <= 0)
        return;
    int left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    int end = right > BWR_CANVAS_W ? BWR_CANVAS_W : (int)right;
    int row_end = bottom > BWR_CANVAS_H ? BWR_CANVAS_H : (int)bottom;
    int first = left / 8, last = (end - 1) / 8;
    uint8_t first_mask = (uint8_t)(0xffu >> (left & 7));
    uint8_t last_mask = (uint8_t)(0xffu << (7 - ((end - 1) & 7)));
    for (int row = top; row < row_end; row++) {
        uint8_t *dst = pixels + row * BWR_ROW_BYTES;
        if (first == last) {
            uint8_t mask = first_mask & last_mask;
            dst[first] = ink ? dst[first] | mask : dst[first] & (uint8_t)~mask;
        } else {
            dst[first] = ink ? dst[first] | first_mask : dst[first] & (uint8_t)~first_mask;
            if (last > first + 1)
                memset(dst + first + 1, ink ? 255 : 0, (size_t)(last - first - 1));
            dst[last] = ink ? dst[last] | last_mask : dst[last] & (uint8_t)~last_mask;
        }
    }
}

static void box_reset(int box[4])
{
    box[BWR_BOX_COL0] = BWR_HALF_BYTES;
    box[BWR_BOX_COL1] = -1;
    box[BWR_BOX_ROW0] = BWR_CANVAS_H;
    box[BWR_BOX_ROW1] = -1;
}

// Grow `box` to include the differences between one row-half of each canvas.
static inline void box_grow(int box[4], const uint8_t *p, const uint8_t *q, int row)
{
    if (!memcmp(p, q, BWR_HALF_BYTES))
        return;
    // Only differences outside the current horizontal extent can grow it.
    for (int col = 0; col < box[BWR_BOX_COL0]; col++)
        if (p[col] != q[col]) {
            box[BWR_BOX_COL0] = col;
            break;
        }
    for (int col = BWR_HALF_BYTES - 1; col > box[BWR_BOX_COL1]; col--)
        if (p[col] != q[col]) {
            box[BWR_BOX_COL1] = col;
            break;
        }
    if (box[BWR_BOX_ROW0] == BWR_CANVAS_H)
        box[BWR_BOX_ROW0] = row;
    box[BWR_BOX_ROW1] = row;
}

void bwr_changed_box(const uint8_t *a, const uint8_t *b, unsigned half, int bounds[4])
{
    box_reset(bounds);
    if (half > 1)
        return;
    for (int row = 0; row < BWR_CANVAS_H; row++) {
        size_t offset = (size_t)row * BWR_ROW_BYTES + half * BWR_HALF_BYTES;
        box_grow(bounds, a + offset, b + offset, row);
    }
}

void bwr_changed_boxes(const uint8_t *a, const uint8_t *b, int bounds[2][4])
{
    box_reset(bounds[0]);
    box_reset(bounds[1]);
    for (int row = 0; row < BWR_CANVAS_H; row++)
        for (unsigned half = 0; half < 2; half++) {
            size_t offset = (size_t)row * BWR_ROW_BYTES + half * BWR_HALF_BYTES;
            box_grow(bounds[half], a + offset, b + offset, row);
        }
}

// Compare address differences rather than adding lengths, which could wrap.
static bool overlaps(const void *a, size_t a_size, const void *b, size_t b_size)
{
    uintptr_t left = (uintptr_t)a, right = (uintptr_t)b;
    return left <= right ? right - left < a_size : left - right < b_size;
}

static uint8_t reverse_byte(uint8_t value)
{
    value = (uint8_t)(value >> 4 | value << 4);
    value = (uint8_t)((value & 0xcc) >> 2 | (value & 0x33) << 2);
    return (uint8_t)((value & 0xaa) >> 1 | (value & 0x55) << 1);
}

bool bwr_stream_window(const uint8_t *source, unsigned half, const int bounds[4], uint8_t *first,
                       uint8_t *second, size_t capacity, bool reverse_bits,
                       const bwr_stream_ops *ops)
{
    if (!source || !bounds || !first || !second || first == second || !capacity || !ops ||
        !ops->start || !ops->finish || half > 1)
        return false;
    int col0 = bounds[BWR_BOX_COL0], col1 = bounds[BWR_BOX_COL1];
    int row0 = bounds[BWR_BOX_ROW0], row1 = bounds[BWR_BOX_ROW1];
    if (col0 < 0 || col1 >= BWR_HALF_BYTES || row0 < 0 || row1 >= BWR_CANVAS_H || col1 < col0 ||
        row1 < row0)
        return false;
    if (overlaps(first, capacity, second, capacity) ||
        overlaps(source, BWR_CANVAS_BYTES, first, capacity) ||
        overlaps(source, BWR_CANVAS_BYTES, second, capacity))
        return false;

    // Double buffering: fill one buffer while the previous transfer is in flight,
    // and wait for that transfer (finish) only just before starting the next one.
    uint8_t *buffers[2] = {first, second};
    size_t used = 0;
    unsigned slot = 0;
    bool pending = false;
    for (int row = row0; row <= row1; row++) {
        const uint8_t *pixels = source + (size_t)row * BWR_ROW_BYTES + half * BWR_HALF_BYTES;
        for (int col = col0; col <= col1; col++) {
            buffers[slot][used++] = reverse_bits ? reverse_byte(pixels[col]) : pixels[col];
            if (used == capacity) {
                if (pending)
                    ops->finish(ops->context);
                ops->start(ops->context, buffers[slot], used);
                pending = true;
                slot ^= 1;
                used = 0;
            }
        }
    }
    if (used) {
        if (pending)
            ops->finish(ops->context);
        ops->start(ops->context, buffers[slot], used);
        pending = true;
    }
    if (pending)
        ops->finish(ops->context);
    return true;
}
