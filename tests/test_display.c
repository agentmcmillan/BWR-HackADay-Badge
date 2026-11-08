// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_tag.h"
#include "refresh_lab.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static saver_shapes shape;
static uint8_t source[SHAPE_BYTES], saved[SHAPE_BYTES], ink[SHAPE_BYTES], red[SHAPE_BYTES];
static bool tag_divider(int y)
{
    return (y >= SHAPE_TOP + TAG_BAR_H && y < SHAPE_TOP + TAG_BAR_H + 4) ||
           (y >= SHAPE_H - TAG_BAR_H - 4 && y < SHAPE_H - TAG_BAR_H);
}
static unsigned ink_pixels(const uint8_t *pixels)
{
    unsigned total = 0;
    for (unsigned i = 0; i < SHAPE_BYTES; i++)
        total += (unsigned)__builtin_popcount((unsigned)pixels[i]);
    return total;
}
int main(void)
{
    shapes_init(&shape, SHAPE_SPIROGRAPH, 1);
    shapes_step(&shape);
    shapes_render(&shape, source);
    unsigned spiro_ink = ink_pixels(source);
    shapes_init(&shape, SHAPE_DVD, 1);
    shapes_render(&shape, source);
    unsigned dvd_ink = ink_pixels(source);
    assert(spiro_ink >= 700 && dvd_ink >= 5000);
    for (unsigned kind = 0; kind < 2; kind++)
        for (unsigned seed = 1; seed <= 32; seed++) {
            shapes_init(&shape, kind, seed);
            for (unsigned frame = 0; frame < 1000; frame++) {
                shapes_step(&shape);
                assert(shape.x >= SHAPE_LEFT && shape.x < SHAPE_RIGHT);
                assert(shape.y >= SHAPE_Y && shape.y < SHAPE_BOTTOM);
                if (kind == SHAPE_DVD)
                    assert(shape.x + DVD_W <= SHAPE_RIGHT && shape.y + DVD_H <= SHAPE_BOTTOM);
            }
            shapes_init(&shape, kind, seed);
            for (int frame = 0; frame < 20; frame++)
                shapes_step(&shape);
            float phase = shape.phase;
            uint32_t rng = shape.rng;
            unsigned generation = shape.frame;
            shapes_render(&shape, saved);
            shapes_set_tag(&shape, true);
            assert(shape.phase == phase && shape.rng == rng && shape.frame == generation);
            for (unsigned frame = 0; frame < 300; frame++) {
                shapes_step(&shape);
                shapes_render(&shape, source);
                for (int y = SHAPE_Y; y < SHAPE_BOTTOM; y++)
                    if (y < TAG_PLAY_TOP || y >= TAG_PLAY_BOTTOM)
                        for (int x = 0; x < SHAPE_W / 8; x++)
                            assert(!source[y * (SHAPE_W / 8) + x]);
            }
        }
    // Preserve every source stroke without ever writing into the original canvas.
    memset(source, 255, sizeof source);
    memcpy(saved, source, sizeof source);
    game_tag_compose_named(source, ink, red, true, false, "Ada");
    assert(!memcmp(source, saved, sizeof source));
    for (int y = 0; y < SHAPE_H; y++)
        for (int x = 0; x < SHAPE_W; x++) {
            unsigned at = y * (SHAPE_W / 8) + x / 8, bit = 128u >> (x & 7);
            assert(!(ink[at] & red[at]));
            if (ink[at] & bit)
                assert(tag_divider(y) ||
                       (x >= GAME_VIEW_X && x < GAME_VIEW_X + GAME_VIEW_W &&
                        y >= TAG_PLAY_TOP && y < TAG_PLAY_BOTTOM));
            if (y >= TAG_PLAY_TOP && y < TAG_PLAY_BOTTOM && x >= GAME_VIEW_X &&
                x < GAME_VIEW_X + GAME_VIEW_W)
                assert(ink[at] & bit);
        }
    // A single scaled source pixel becomes a 2x2 high-contrast cell, while the
    // stationary dividers remain solid and outside the moving playfield.
    memset(source, 0, sizeof source);
    int source_x = 100, source_y = 200;
    source[source_y * (SHAPE_W / 8) + source_x / 8] = (uint8_t)(128u >> (source_x & 7));
    game_tag_compose_named(source, ink, red, true, false, "Ada");
    int scaled_x = GAME_VIEW_X + source_x * GAME_VIEW_H / (SHAPE_H - SHAPE_TOP);
    int scaled_y = TAG_PLAY_TOP + (source_y - SHAPE_TOP) * GAME_VIEW_H / (SHAPE_H - SHAPE_TOP);
    for (int y = scaled_y; y < scaled_y + 2; y++)
        for (int x = scaled_x; x < scaled_x + 2; x++)
            assert(ink[y * (SHAPE_W / 8) + x / 8] & (128u >> (x & 7)));
    for (int y = 0; y < SHAPE_H; y++)
        if (tag_divider(y))
            for (int x = 0; x < SHAPE_W / 8; x++)
                assert(ink[y * (SHAPE_W / 8) + x] == 0xff);
    game_tag_compose(source, ink, red, false, false);
    assert(!memcmp(source, ink, sizeof ink));
    for (unsigned i = 0; i < sizeof red; i++)
        assert(!red[i]);
    // Labels are bounded and non-ASCII bytes cannot index past the bitmap font.
    const char *names[] = {"", "Grace", "A very long display name", "\xff\xfe\x80", NULL};
    for (unsigned n = 0; n < sizeof names / sizeof names[0]; n++)
        shapes_tag_render_named(ink, red, NULL, names[n]);
    refresh_result r = {0};
    refresh_sample sample = {.changed = true, .pull_us = 588000, .actual_khz = 4000};
    refresh_result_add(&r, &sample, 620000, 8000);
    sample.reopened = true;
    refresh_result_add(&r, &sample, 3200000, 8000);
    sample = (refresh_sample){0};
    refresh_result_add(&r, &sample, 2000, 100);
    assert(r.calls == 3 && r.pulls == 2 && r.reopens == 1 && r.steady == 1 && r.best_us == 620000 &&
           r.worst_us == 3200000);
    puts("PASS models, white-area bounds, named tags, source preservation and refresh statistics");
}
