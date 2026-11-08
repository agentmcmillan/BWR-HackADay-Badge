// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
// Driver timings, not a measurement of visible pigment settling.
typedef struct {
    uint64_t total_us, diff_us, open_us, write_us, pull_us;
    uint32_t bytes, transactions, actual_khz;
    bool changed, reopened;
} refresh_sample;
bool ui_refresh_lab_begin(unsigned mhz);
void ui_refresh_lab_end(void);
void ui_refresh_lab_sample(refresh_sample *out);
void app_refresh_lab(void);

enum { LAB_PATTERN_COUNT = 5, LAB_FRAMES = 60 };
typedef struct {
    unsigned calls, pulls, reopens, steady;
    uint64_t wall_us, render_us, diff_us, open_us, write_us, pull_us, bytes, transactions;
    uint64_t best_us, worst_us, steady_us;
    uint32_t actual_khz;
} refresh_result;
void refresh_result_add(refresh_result *r, const refresh_sample *s, uint64_t wall, uint64_t render);
const char *refresh_pattern_name(unsigned pattern);
