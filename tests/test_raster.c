// SPDX-License-Identifier: GPL-3.0-or-later
#include "bwr_raster.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t actual[80640], expected[80640], before[80640];
static unsigned state = 0x756341u;
static unsigned random_word(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    return state ^= state << 5;
}
static void reference_fill(int x, int y, int w, int h, bool ink)
{
    for (int row = 0; row < 672; row++)
        for (int col = 0; col < 960; col++)
            if (col >= x && (int64_t)col < (int64_t)x + w && row >= y &&
                (int64_t)row < (int64_t)y + h) {
                uint8_t mask = 128u >> (col & 7);
                if (ink)
                    expected[row * 120 + col / 8] |= mask;
                else
                    expected[row * 120 + col / 8] &= (uint8_t)~mask;
            }
}
static void check_fill(int x, int y, int w, int h, bool ink)
{
    reference_fill(x, y, w, h, ink);
    bwr_fill_rect(actual, x, y, w, h, ink);
    assert(!memcmp(actual, expected, sizeof actual));
}
static void reference_bounds(unsigned half, int out[4])
{
    out[0] = 60;
    out[1] = -1;
    out[2] = 672;
    out[3] = -1;
    for (int y = 0; y < 672; y++)
        for (int x = 0; x < 60; x++)
            if (actual[y * 120 + half * 60 + x] != before[y * 120 + half * 60 + x]) {
                if (x < out[0])
                    out[0] = x;
                if (x > out[1])
                    out[1] = x;
                if (y < out[2])
                    out[2] = y;
                if (y > out[3])
                    out[3] = y;
            }
}
static void reference_bounds_rows(unsigned half, int y0, int y1, int out[4])
{
    out[0] = 60;
    out[1] = -1;
    out[2] = 672;
    out[3] = -1;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < 60; x++)
            if (actual[y * 120 + half * 60 + x] != before[y * 120 + half * 60 + x]) {
                if (x < out[0])
                    out[0] = x;
                if (x > out[1])
                    out[1] = x;
                if (y < out[2])
                    out[2] = y;
                if (y > out[3])
                    out[3] = y;
            }
}

static uint8_t buffers[2][4096], pending_copy[4096], output[40320];
static const uint8_t *pending_bytes;
static size_t pending_count, output_size, transfers;
static void stream_start(void *context, const uint8_t *bytes, size_t count)
{
    assert(context == output && !pending_bytes && count && count <= 4096);
    pending_bytes = bytes;
    pending_count = count;
    memcpy(pending_copy, bytes, count);
    transfers++;
}
static void stream_finish(void *context)
{
    assert(context == output && pending_bytes);
    // The in-flight buffer must remain untouched while the next chunk is packed.
    assert(!memcmp(pending_bytes, pending_copy, pending_count));
    assert(output_size + pending_count <= sizeof output);
    memcpy(output + output_size, pending_bytes, pending_count);
    output_size += pending_count;
    pending_bytes = NULL;
}
static void check_stream(void)
{
    const bwr_stream_ops ops = {stream_start, stream_finish, output};
    for (unsigned half = 0; half < 2; half++)
        for (int reverse = 0; reverse < 2; reverse++)
            for (unsigned trial = 0; trial < 70; trial++) {
                int x = trial == 0 ? 0 : (int)(random_word() % 60),
                    y = trial == 0 ? 0 : (int)(random_word() % 672);
                int b[4] = {x, trial == 0 ? 59 : x + (int)(random_word() % (60 - x)), y,
                            trial == 0 ? 671 : y + (int)(random_word() % (672 - y))};
                size_t capacity = trial % 2 ? 4096 : 37;
                output_size = transfers = 0;
                assert(bwr_stream_window(actual, half, b, buffers[0], buffers[1], capacity, reverse,
                                         &ops));
                size_t i = 0;
                for (int row = b[2]; row <= b[3]; row++)
                    for (int col = b[0]; col <= b[1]; col++) {
                        uint8_t value = actual[row * 120 + half * 60 + col], expected = 0;
                        for (unsigned bit = 0; bit < 8; bit++)
                            expected |= ((value >> bit) & 1) << (7 - bit);
                        assert(output[i++] == (reverse ? expected : value));
                    }
                assert(i == output_size && !pending_bytes &&
                       transfers == (i + capacity - 1) / capacity);
            }
    int full[4] = {0, 59, 0, 671};
    size_t before_transfers = transfers;
    assert(!bwr_stream_window(actual, 0, full, buffers[0], buffers[0] + 1, 4096, true, &ops));
    assert(!bwr_stream_window(actual, 0, full, actual + 1, buffers[1], 4096, true, &ops));
    assert(transfers == before_transfers && !pending_bytes);
    int bad[4] = {0, 60, 0, 671};
    assert(!bwr_stream_window(actual, 0, bad, buffers[0], buffers[1], 4096, true, &ops));
}
int main(void)
{
    for (size_t i = 0; i < sizeof actual; i++)
        actual[i] = expected[i] = (uint8_t)random_word();
    for (int i = 0; i < 400; i++) {
        int x = (int)(random_word() % 1300) - 170, y = (int)(random_word() % 900) - 120;
        check_fill(x, y, (int)(random_word() % 1100) - 20, (int)(random_word() % 800) - 20,
                   random_word() & 1);
    }
    check_fill(INT_MIN, INT_MIN, INT_MAX, INT_MAX, true);
    check_fill(0, 0, INT_MAX, INT_MAX, true);
    check_fill(INT_MAX, INT_MAX, INT_MAX, INT_MAX, false);
    for (int y = 0; y < 672; y += 671)
        for (int x = 0; x < 960; x++)
            check_fill(x, y, 1, 1, (x & 1) != 0);
    for (int trial = 0; trial < 300; trial++) {
        memcpy(before, actual, sizeof actual);
        int edits = trial % 3 == 0 ? 0 : trial % 3 == 1 ? 1 : 20000;
        for (int i = 0; i < edits; i++)
            actual[random_word() % sizeof actual] ^= (uint8_t)random_word();
        int joint[2][4];
        bwr_changed_boxes(actual, before, joint);
        int y0 = (int)(random_word() % 672), y1 = y0 + (int)(random_word() % (672 - y0));
        int ranged[2][4];
        bwr_changed_boxes_rows(actual, before, y0, y1, ranged);
        for (unsigned half = 0; half < 2; half++) {
            int a[4], b[4], r[4];
            reference_bounds(half, a);
            reference_bounds_rows(half, y0, y1, r);
            bwr_changed_box(actual, before, half, b);
            assert(!memcmp(a, b, sizeof a));
            assert(!memcmp(a, joint[half], sizeof a));
            assert(!memcmp(r, ranged[half], sizeof r));
        }
    }
    check_stream();
    puts("PASS pipelined transfer lifetime/order and byte raster: pixel-equivalent clipped fills, "
         "extreme bounds, edge masks, "
         "sparse/dense/unchanged diff boxes");
}
