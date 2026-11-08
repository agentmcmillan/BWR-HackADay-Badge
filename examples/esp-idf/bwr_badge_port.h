// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game_tag.h"
// Interface to implement in a host badge firmware. No upstream implementation is included.
enum { BWR_BTN_PLUS = 0, BWR_BTN_MINUS = 1, BWR_BTN_A = 2, BWR_BTN_B = 3 };
extern uint8_t fb[SHAPE_BYTES];
void box(int x, int y, int width, int height, bool ink);
void outline(int x, int y, int width, int height, int thickness);
void text(int x, int y, int scale, const char *label, bool ink);
void ui_frame(int pulls);
// Inclusive physical panel-row refresh used when the renderer proves all
// changes are contained in one range.
void ui_frame_rows(int y0, int y1);
void ui_close(void);
void ui_wipe(void);
void ui_keys(const char *plus, const char *minus, const char *a, const char *b);
int btn_get(int timeout_ms);
void ui_refresh_shortcut(bool enabled);
void ui_protect_overlay(const uint8_t *mask);
void ui_protect_footer(bool enabled);
void ui_game_native_view(bool enabled);
void ui_game_tag_toggle(void);
bool ui_game_tag_enabled(void);
