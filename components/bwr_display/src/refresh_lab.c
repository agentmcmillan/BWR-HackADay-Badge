// SPDX-License-Identifier: GPL-3.0-or-later
#include "refresh_lab.h"
void refresh_result_add(refresh_result *r, const refresh_sample *s, uint64_t wall, uint64_t render)
{
    r->calls++;
    r->wall_us += wall;
    r->render_us += render;
    r->diff_us += s->diff_us;
    r->open_us += s->open_us;
    r->write_us += s->write_us;
    r->pull_us += s->pull_us;
    r->bytes += s->bytes;
    r->transactions += s->transactions;
    r->actual_khz = s->actual_khz;
    if (!s->changed)
        return;
    r->pulls++;
    if (!r->best_us || wall < r->best_us)
        r->best_us = wall;
    if (wall > r->worst_us)
        r->worst_us = wall;
    if (s->reopened)
        r->reopens++;
    else {
        r->steady++;
        r->steady_us += wall;
    }
}
const char *refresh_pattern_name(unsigned p)
{
    static const char *names[] = {"Moving block", "Tall sweep", "Checkerboard", "Black / white",
                                  "Unchanged scene"};
    return names[p < LAB_PATTERN_COUNT ? p : 0];
}
