// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_savers.h"
#include "bwr_badge_port.h"
#include "esp_attr.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <stdio.h>
static EXT_RAM_BSS_ATTR saver_shapes shape;
int app_shapes_run(unsigned kind, bool gallery)
{
    ui_game_native_view(true);
    shapes_init(&shape, kind, esp_random());
    shape.name_obstacle = false; // the shared tag compositor owns the playfield
    bool paused = false, dirty = true;
    int button = -1;
    for (;;) {
        if (button < 0)
            button = btn_get(paused ? 200 : 0);
        bool toggle = gallery ? button == BWR_BTN_A : button == BWR_BTN_PLUS;
        if (toggle) {
            ui_game_tag_toggle();
            dirty = true;
        } else if (button == BWR_BTN_A || button == BWR_BTN_B || (gallery && button >= 0))
            break;
        else if (button == BWR_BTN_MINUS) {
            paused = !paused;
            dirty = true;
        }
        button = -1;
        if (shape.tag != ui_game_tag_enabled()) {
            shapes_set_tag(&shape, ui_game_tag_enabled());
            dirty = true;
        }
        if (paused && !dirty)
            continue;
        int64_t start = esp_timer_get_time();
        if (!paused)
            shapes_step(&shape);
        shapes_render(&shape, fb);
        char title[80];
        if (kind == SHAPE_DVD)
            snprintf(title, sizeof title, "DVD | %u bounces%s", shape.bounces,
                     paused ? " | PAUSED" : "");
        else
            snprintf(title, sizeof title, "Spirograph | gears %d:%d | pen %d%s", shape.outer,
                     shape.inner, shape.pen, paused ? " | PAUSED" : "");
        text(16, SHAPE_TOP + 8, 2, title, true);
        const char *tag_label = ui_game_tag_enabled() ? "Tag off" : "Tag on";
        ui_keys(gallery ? "Prev mode" : tag_label,
                gallery  ? "Next mode"
                : paused ? "Resume"
                         : "Pause",
                gallery ? tag_label : "Home", "Home");
        ui_frame(1);
        dirty = false;
        if (paused) {
            ui_close();
            continue;
        }
        int64_t elapsed = (esp_timer_get_time() - start) / 1000;
        button = btn_get(elapsed < 600 ? 600 - (int)elapsed : 0);
    }
    ui_close();
    ui_game_native_view(false);
    return button;
}
void app_spirograph(void)
{
    app_shapes_run(SHAPE_SPIROGRAPH, false);
}
void app_dvd(void)
{
    app_shapes_run(SHAPE_DVD, false);
}
