// SPDX-License-Identifier: GPL-3.0-or-later
#include "saver_shapes.h"
#include "font8x8_basic.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t random_next(saver_shapes *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s->rng = x;
}
static void pixel(uint8_t *out, int x, int y)
{
    if (x >= SHAPE_LEFT && x < SHAPE_RIGHT && y >= SHAPE_Y && y < SHAPE_BOTTOM)
        out[y * (SHAPE_W / 8) + x / 8] |= (uint8_t)(128u >> (x & 7));
}
static void line(uint8_t *out, int x, int y, int xx, int yy)
{
    int dx = abs(xx - x), sx = x < xx ? 1 : -1, dy = -abs(yy - y), sy = y < yy ? 1 : -1,
        error = dx + dy;
    for (;;) {
        pixel(out, x, y);
        pixel(out, x + 1, y);
        pixel(out, x, y + 1);
        if (x == xx && y == yy)
            break;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += sx;
        }
        if (twice <= dx) {
            error += dx;
            y += sy;
        }
    }
}
static void point(const saver_shapes *s, float t, int *x, int *y)
{
    float r = (float)(s->outer - s->inner), ratio = r / s->inner;
    float radius = r + s->pen;
    *x = SHAPE_W / 2 + (int)lroundf((r * cosf(t) + s->pen * cosf(ratio * t)) * 440.f / radius);
    int top = s->tag ? TAG_PLAY_TOP : SHAPE_Y, bottom = s->tag ? TAG_PLAY_BOTTOM : SHAPE_BOTTOM;
    float height = s->tag ? (bottom - top) * .5f - 14.f : 270.f;
    *y = (top + bottom) / 2 +
         (int)lroundf((r * sinf(t) - s->pen * sinf(ratio * t)) * height / radius);
    // The curve rides over the name rather than disappearing behind it.
    if (s->name_obstacle && *x >= SHAPE_NAME_LEFT - 4 && *y >= SHAPE_NAME_TOP - 4)
        *y = SHAPE_NAME_TOP - 4;
}
static void new_curve(saver_shapes *s)
{
    memset(s->trail, 0, sizeof s->trail);
    s->outer = 7 + (int)(random_next(s) % 10);
    s->inner = 2 + (int)(random_next(s) % (unsigned)(s->outer - 3));
    s->pen = 2 + (int)(random_next(s) % 12);
    int a = s->outer, b = s->inner;
    while (b) {
        int remainder = a % b;
        a = b;
        b = remainder;
    }
    s->phase = 0;
    s->hold = 0;
    s->finish = 6.28318530718f * s->inner / a;
    point(s, 0, &s->x, &s->y);
}
static bool touches_name(const saver_shapes *s)
{
    return s->x + DVD_W > SHAPE_NAME_LEFT && s->x < SHAPE_NAME_RIGHT &&
           s->y + DVD_H > SHAPE_NAME_TOP && s->y < SHAPE_NAME_BOTTOM;
}
void shapes_init(saver_shapes *s, unsigned kind, uint32_t seed)
{
    memset(s, 0, sizeof *s);
    s->kind = kind == SHAPE_DVD ? SHAPE_DVD : SHAPE_SPIROGRAPH;
    s->rng = seed ? seed : 1;
    s->name_obstacle = false;
    if (s->kind == SHAPE_SPIROGRAPH)
        new_curve(s);
    else {
        s->x = SHAPE_LEFT + (int)(random_next(s) % (SHAPE_RIGHT - SHAPE_LEFT - DVD_W + 1));
        s->y = SHAPE_Y + (int)(random_next(s) % (SHAPE_BOTTOM - SHAPE_Y - DVD_H + 1));
        if (touches_name(s))
            s->y = SHAPE_NAME_TOP - DVD_H;
        s->turn_in = 24 + random_next(s) % 49;
        s->vx = 13 + (int)(random_next(s) % 9);
        if (random_next(s) & 1u)
            s->vx = -s->vx;
        s->vy = 9 + (int)(random_next(s) % 9);
        if (random_next(s) & 1u)
            s->vy = -s->vy;
    }
}
static void curve_segment(saver_shapes *s, float t)
{
    int x, y;
    point(s, t, &x, &y);
    int corner_x = SHAPE_NAME_LEFT - 4, corner_y = SHAPE_NAME_TOP - 4;
    bool around = s->name_obstacle &&
                  ((s->y > corner_y && x > corner_x) || (y > corner_y && s->x > corner_x));
    if (around) {
        line(s->trail, s->x, s->y, corner_x, corner_y);
        line(s->trail, corner_x, corner_y, x, y);
        s->name_hits++;
    } else
        line(s->trail, s->x, s->y, x, y);
    s->phase = t;
    s->x = x;
    s->y = y;
}
void shapes_set_tag(saver_shapes *s, bool enabled)
{
    if (s->tag == enabled)
        return;
    s->tag = enabled;
    if (s->kind == SHAPE_SPIROGRAPH) {
        // Reproject the same curve and progress into the new white playfield.
        float phase = s->phase;
        unsigned hits = s->name_hits;
        memset(s->trail, 0, sizeof s->trail);
        s->phase = 0;
        point(s, 0, &s->x, &s->y);
        while (s->phase < phase)
            curve_segment(s, fminf(s->phase + .012f, phase));
        s->name_hits = hits;
        return;
    }
    int top = enabled ? TAG_PLAY_TOP : SHAPE_Y;
    int bottom = (enabled ? TAG_PLAY_BOTTOM : SHAPE_BOTTOM) - DVD_H;
    if (s->y < top)
        s->y = top;
    if (s->y > bottom)
        s->y = bottom;
    if (!enabled && s->name_obstacle && touches_name(s))
        s->y = SHAPE_NAME_TOP - DVD_H;
    if (s->y == top)
        s->vy = abs(s->vy);
    if (s->y == bottom)
        s->vy = -abs(s->vy);
}
static bool bounce(int *p, int *velocity, int low, int high)
{
    *p += *velocity;
    if (*p <= low) {
        *p = 2 * low - *p;
        *velocity = abs(*velocity);
        return true;
    }
    if (*p >= high) {
        *p = 2 * high - *p;
        *velocity = -abs(*velocity);
        return true;
    }
    return false;
}
static void new_heading(saver_shapes *s)
{
    int old_x = abs(s->vx), old_y = abs(s->vy), vx, vy, a, b;
    do {
        vx = 16 + (int)(random_next(s) % 14);
        vy = 11 + (int)(random_next(s) % 15);
        a = vx;
        b = vy;
        while (b) {
            int remainder = a % b;
            a = b;
            b = remainder;
        }
    } while (a != 1 || vx * old_y == vy * old_x);
    // Preserve the reflected direction, but avoid repeating the same slope.
    s->vx = s->vx < 0 ? -vx : vx;
    s->vy = s->vy < 0 ? -vy : vy;
}
void shapes_step(saver_shapes *s)
{
    s->frame++;
    if (s->kind == SHAPE_DVD) {
        int old_x = s->x, old_y = s->y;
        bool x = bounce(&s->x, &s->vx, SHAPE_LEFT, SHAPE_RIGHT - DVD_W);
        bool y = bounce(&s->y, &s->vy, s->tag ? TAG_PLAY_TOP : SHAPE_Y,
                        (s->tag ? TAG_PLAY_BOTTOM : SHAPE_BOTTOM) - DVD_H);
        if (!s->tag && s->name_obstacle && touches_name(s)) {
            // Swept entry faces distinguish a side hit from a top hit.
            bool side = old_x + DVD_W <= SHAPE_NAME_LEFT;
            bool top = old_y + DVD_H <= SHAPE_NAME_TOP;
            if (side) {
                s->x = 2 * (SHAPE_NAME_LEFT - DVD_W) - s->x;
                s->vx = -abs(s->vx);
            }
            if (top) {
                s->y = 2 * (SHAPE_NAME_TOP - DVD_H) - s->y;
                s->vy = -abs(s->vy);
            }
            s->name_hits++;
            x = x || side;
            y = y || top;
        }
        if (x || y) {
            s->bounces++;
            new_heading(s);
        }
        if (s->turn_in && !--s->turn_in) {
            // Occasional course changes break repeated routes between walls.
            // Keep both signs: the logo still travels across the playfield.
            if (!x && !y) {
                new_heading(s);
                s->turns++;
            }
            s->turn_in = 24 + random_next(s) % 49;
        }
        if (x && y)
            s->corners++;
        return;
    }
    if (s->phase >= s->finish) {
        if (++s->hold >= 8)
            new_curve(s);
        return;
    }
    // Trace 3x farther per frame; retain short segments to keep tight loops smooth.
    for (unsigned i = 0; i < 72 && s->phase < s->finish; i++) {
        float t = fminf(s->phase + .012f, s->finish);
        curve_segment(s, t);
    }
}
void shapes_render(const saver_shapes *s, uint8_t out[SHAPE_BYTES])
{
    memset(out, 0, SHAPE_BYTES);
    if (s->kind == SHAPE_SPIROGRAPH) {
        memcpy(out, s->trail, SHAPE_BYTES);
        for (int y = -3; y <= 3; y++)
            for (int x = -3; x <= 3; x++)
                if (x * x + y * y <= 9)
                    pixel(out, s->x + x, s->y + y);
        return;
    }
    // Slanted DVD letters over a flattened disc, drawn directly in monochrome.
    const char *name = "DVD";
    for (int n = 0; n < 3; n++)
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if ((font8x8_basic[(unsigned char)name[n]][row] >> col) & 1u)
                    for (int yy = 0; yy < 6; yy++)
                        for (int xx = 0; xx < 6; xx++)
                            pixel(out, s->x + 12 + n * 48 + col * 6 + xx + (47 - row * 6 - yy) / 3,
                                  s->y + 4 + row * 6 + yy);
    for (int y = -10; y <= 10; y++)
        for (int x = -82; x <= 82; x++)
            if (x * x * 100 + y * y * 82 * 82 <= 82 * 82 * 100 &&
                !(x * x * 9 + y * y * 18 * 18 < 18 * 18 * 9))
                pixel(out, s->x + DVD_W / 2 + x, s->y + 72 + y);
}

static void white_text(uint8_t *red, int left, int top, int scale, const char *label)
{
    for (int n = 0; label[n]; n++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if ((font8x8_basic[(unsigned char)label[n]][y] >> x) & 1u)
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++) {
                            int xx = left + (n * 8 + x) * scale + dx, yy = top + y * scale + dy;
                            red[yy * (SHAPE_W / 8) + xx / 8] &= (uint8_t)~(128u >> (xx & 7));
                        }
}
void shapes_tag_render_named(uint8_t black[SHAPE_BYTES], uint8_t red[SHAPE_BYTES], uint8_t *mask,
                             const char *name)
{
    memset(red, 0, SHAPE_BYTES);
    if (mask)
        memset(mask, 0, SHAPE_BYTES);
    for (int y = SHAPE_TOP; y < SHAPE_H; y++) {
        if (y >= SHAPE_TOP + TAG_BAR_H && y < SHAPE_H - TAG_BAR_H)
            continue;
        // Protect the entire stationary bar, including white letter cutouts.
        memset(black + y * (SHAPE_W / 8), 0, SHAPE_W / 8);
        if (mask)
            memset(mask + y * (SHAPE_W / 8), 255, SHAPE_W / 8);
        for (int x = 0; x < SHAPE_W; x++) {
            int edge = x < 24 ? 24 - x : x >= SHAPE_W - 24 ? x - (SHAPE_W - 25) : 0;
            int corner = y < SHAPE_TOP + 24  ? SHAPE_TOP + 24 - y
                         : y >= SHAPE_H - 24 ? y - (SHAPE_H - 25)
                                             : 0;
            if (edge * edge + corner * corner <= 24 * 24)
                red[y * (SHAPE_W / 8) + x / 8] |= (uint8_t)(128u >> (x & 7));
        }
    }
    white_text(red, (SHAPE_W - 5 * 8 * 8) / 2, SHAPE_TOP + 8, 8, "HELLO");
    white_text(red, (SHAPE_W - 10 * 8 * 3) / 2, SHAPE_TOP + 88, 3, "my name is");
    char label[13];
    unsigned length = 0;
    if (!name || !*name)
        name = "YOUR NAME";
    while (name[length] && length < 12) {
        unsigned char c = (unsigned char)name[length];
        label[length] = (c >= 32 && c <= 126) ? (char)c : '?';
        length++;
    }
    label[length] = 0;
    int scale = (SHAPE_W - 64) / (8 * (int)length);
    if (scale > 10)
        scale = 10;
    white_text(red, (SHAPE_W - (int)length * 8 * scale) / 2,
               SHAPE_H - TAG_BAR_H + (TAG_BAR_H - 8 * scale) / 2, scale, label);
}

void shapes_tag_render(uint8_t black[SHAPE_BYTES], uint8_t red[SHAPE_BYTES], uint8_t *mask)
{
    shapes_tag_render_named(black, red, mask, "YOUR NAME");
}
