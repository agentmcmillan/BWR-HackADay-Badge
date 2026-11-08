// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_tag.h"
#include <string.h>
void game_tag_compose_named(const uint8_t source[SHAPE_BYTES], uint8_t black[SHAPE_BYTES],
                            uint8_t red[SHAPE_BYTES], bool enabled, bool native_view,
                            const char *name)
{
    memset(red, 0, SHAPE_BYTES);
    if (!enabled) {
        memcpy(black, source, SHAPE_BYTES);
        return;
    }
    if (native_view) {
        memcpy(black, source, SHAPE_BYTES);
        shapes_tag_render_named(black, red, NULL, name);
        return;
    }
    memset(black, 0, SHAPE_BYTES);
    // Forward-map every ink pixel: thin bullets and grid lines survive scaling.
    // Use the same scale on both axes so objects retain their proportions.
    for (int y = SHAPE_TOP; y < SHAPE_H; y++) {
        int yy = TAG_PLAY_TOP + (y - SHAPE_TOP) * GAME_VIEW_H / (SHAPE_H - SHAPE_TOP);
        for (int x = 0; x < SHAPE_W; x++)
            if (source[y * (SHAPE_W / 8) + x / 8] & (128u >> (x & 7))) {
                int xx = GAME_VIEW_X + x * GAME_VIEW_H / (SHAPE_H - SHAPE_TOP);
                black[yy * (SHAPE_W / 8) + xx / 8] |= (uint8_t)(128u >> (xx & 7));
            }
    }
    shapes_tag_render_named(black, red, NULL, name);
}

void game_tag_compose(const uint8_t source[SHAPE_BYTES], uint8_t black[SHAPE_BYTES],
                      uint8_t red[SHAPE_BYTES], bool enabled, bool native_view)
{
    game_tag_compose_named(source, black, red, enabled, native_view, "YOUR NAME");
}
