// SPDX-License-Identifier: GPL-3.0-or-later
// Portable animation state: no panel, radio, timing or allocation.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SHAPE_W 960
#define SHAPE_H 672
#define SHAPE_TOP 48
#define SHAPE_BYTES (SHAPE_W * SHAPE_H / 8)
enum { SHAPE_SPIROGRAPH, SHAPE_DVD };
enum {
    DVD_W = 184,
    DVD_H = 88,
    SHAPE_LEFT = 16,
    SHAPE_RIGHT = 944,
    SHAPE_Y = 88,
    SHAPE_BOTTOM = 656,
    TAG_BAR_H = 128,
    TAG_PLAY_TOP = SHAPE_TOP + TAG_BAR_H + 8,
    TAG_PLAY_BOTTOM = SHAPE_H - TAG_BAR_H - 8,
    SHAPE_NAME_LEFT = 610,
    SHAPE_NAME_TOP = 590,
    SHAPE_NAME_RIGHT = 942,
    SHAPE_NAME_BOTTOM = 666
};
typedef struct {
    unsigned kind, frame, bounces, corners, name_hits, hold, turn_in, turns;
    uint32_t rng;
    bool tag, name_obstacle;
    int outer, inner, pen, x, y, vx, vy;
    float phase, finish;
    uint8_t trail[SHAPE_BYTES];
} saver_shapes;
void shapes_init(saver_shapes *s, unsigned kind, uint32_t seed);
void shapes_step(saver_shapes *s);
void shapes_render(const saver_shapes *s, uint8_t out[SHAPE_BYTES]);

void shapes_set_tag(saver_shapes *s, bool enabled);
void shapes_tag_render(uint8_t black[SHAPE_BYTES], uint8_t red[SHAPE_BYTES],
                       uint8_t *mask); // mask may be NULL

void shapes_tag_render_named(uint8_t black[SHAPE_BYTES], uint8_t red[SHAPE_BYTES], uint8_t *mask,
                             const char *name);
