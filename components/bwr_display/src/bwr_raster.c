// SPDX-License-Identifier: GPL-3.0-or-later
#include "bwr_raster.h"
#include <string.h>

void bwr_fill_rect(uint8_t *pixels, int x, int y, int width, int height, bool ink)
{
    if (width <= 0 || height <= 0)
        return;
    int64_t right = (int64_t)x + width, bottom = (int64_t)y + height;
    if (x >= 960 || y >= 672 || right <= 0 || bottom <= 0)
        return;
    int left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    int end = right > 960 ? 960 : (int)right;
    int row_end = bottom > 672 ? 672 : (int)bottom;
    int first = left / 8, last = (end - 1) / 8;
    uint8_t first_mask = (uint8_t)(0xffu >> (left & 7));
    uint8_t last_mask = (uint8_t)(0xffu << (7 - ((end - 1) & 7)));
    for (int row = top; row < row_end; row++) {
        uint8_t *dst = pixels + row * 120;
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

void bwr_changed_box(const uint8_t *a, const uint8_t *b, unsigned half, int bounds[4])
{
    bounds[0] = 60;
    bounds[1] = -1;
    bounds[2] = 672;
    bounds[3] = -1;
    if (half > 1)
        return;
    for (int row = 0; row < 672; row++) {
        const uint8_t *p = a + row * 120 + half * 60;
        const uint8_t *q = b + row * 120 + half * 60;
        if (!memcmp(p, q, 60))
            continue;
        // Only differences outside the current horizontal extent can grow it.
        for (int col = 0; col < bounds[0]; col++)
            if (p[col] != q[col]) {
                bounds[0] = col;
                break;
            }
        for (int col = 59; col > bounds[1]; col--)
            if (p[col] != q[col]) {
                bounds[1] = col;
                break;
            }
        if (bounds[2] == 672)
            bounds[2] = row;
        bounds[3] = row;
    }
}

// Compare address differences rather than adding lengths, which could wrap.
static bool overlaps(const void *a, size_t a_size, const void *b, size_t b_size)
{
    uintptr_t left = (uintptr_t)a, right = (uintptr_t)b;
    return left <= right ? right - left < a_size : left - right < b_size;
}

bool bwr_stream_window(const uint8_t *source, unsigned half, const int bounds[4], uint8_t *first,
                       uint8_t *second, size_t capacity, bool reverse_bits,
                       const bwr_stream_ops *ops)
{
    if (!source || !bounds || !first || !second || first == second || !capacity || !ops ||
        !ops->start || !ops->finish || half > 1 || bounds[0] < 0 || bounds[1] >= 60 ||
        bounds[2] < 0 || bounds[3] >= 672 || bounds[1] < bounds[0] || bounds[3] < bounds[2])
        return false;
    if (overlaps(first, capacity, second, capacity) ||
        overlaps(source, 960u * 672u / 8u, first, capacity) ||
        overlaps(source, 960u * 672u / 8u, second, capacity))
        return false;
    uint8_t *buffers[2] = {first, second};
    size_t used = 0;
    unsigned slot = 0;
    bool pending = false;
    for (int row = bounds[2]; row <= bounds[3]; row++) {
        const uint8_t *pixels = source + row * 120 + half * 60;
        for (int col = bounds[0]; col <= bounds[1]; col++) {
            uint8_t value = pixels[col];
            if (reverse_bits) {
                value = value >> 4 | value << 4;
                value = (value & 0xcc) >> 2 | (value & 0x33) << 2;
                value = (value & 0xaa) >> 1 | (value & 0x55) << 1;
            }
            buffers[slot][used++] = value;
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

void bwr_changed_boxes(const uint8_t *a, const uint8_t *b, int bounds[2][4])
{
    for (unsigned half = 0; half < 2; half++) {
        bounds[half][0] = 60;
        bounds[half][1] = -1;
        bounds[half][2] = 672;
        bounds[half][3] = -1;
    }
    for (int row = 0; row < 672; row++)
        for (unsigned half = 0; half < 2; half++) {
            const uint8_t *p = a + row * 120 + half * 60, *q = b + row * 120 + half * 60;
            int *box = bounds[half];
            if (!memcmp(p, q, 60))
                continue;
            for (int col = 0; col < box[0]; col++)
                if (p[col] != q[col]) {
                    box[0] = col;
                    break;
                }
            for (int col = 59; col > box[1]; col--)
                if (p[col] != q[col]) {
                    box[1] = col;
                    break;
                }
            if (box[2] == 672)
                box[2] = row;
            box[3] = row;
        }
}
