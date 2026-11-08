// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_tag.h"
#include "refresh_lab.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static saver_shapes shape;
static uint8_t source[SHAPE_BYTES], saved[SHAPE_BYTES], ink[SHAPE_BYTES], red[SHAPE_BYTES];
int main(void)
{
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
                assert(x >= GAME_VIEW_X && x < GAME_VIEW_X + GAME_VIEW_W && y >= TAG_PLAY_TOP &&
                       y < TAG_PLAY_BOTTOM);
            if (y >= TAG_PLAY_TOP && y < TAG_PLAY_BOTTOM && x >= GAME_VIEW_X &&
                x < GAME_VIEW_X + GAME_VIEW_W)
                assert(ink[at] & bit);
        }
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
