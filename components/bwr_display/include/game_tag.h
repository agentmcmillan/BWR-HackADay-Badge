// SPDX-License-Identifier: GPL-3.0-or-later
// Shared, portable name-tag compositor. The source game canvas is never changed.
#pragma once
#include "saver_shapes.h"
enum {
    GAME_VIEW_H = TAG_PLAY_BOTTOM - TAG_PLAY_TOP,
    GAME_VIEW_W = SHAPE_W * GAME_VIEW_H / (SHAPE_H - SHAPE_TOP),
    GAME_VIEW_X = (SHAPE_W - GAME_VIEW_W) / 2
};
void game_tag_compose(const uint8_t source[SHAPE_BYTES], uint8_t black[SHAPE_BYTES],
                      uint8_t red[SHAPE_BYTES], bool enabled, bool native_view);

void game_tag_compose_named(const uint8_t source[SHAPE_BYTES], uint8_t black[SHAPE_BYTES],
                            uint8_t red[SHAPE_BYTES], bool enabled, bool native_view,
                            const char *name);
