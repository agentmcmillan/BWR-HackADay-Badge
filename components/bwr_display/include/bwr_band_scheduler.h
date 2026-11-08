// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    BWR_PANEL_WIDTH = 960,
    BWR_PANEL_HEIGHT = 672,
    BWR_PANEL_ROW_BYTES = 120,
    BWR_PANEL_BYTES = BWR_PANEL_ROW_BYTES * BWR_PANEL_HEIGHT,
};

typedef struct {
    unsigned band_rows;     // fixed grid height; final band may be shorter
    unsigned max_age;       // force a dirty band after this many scheduler calls
    unsigned motion_weight; // extra score per bit changed in the desired scene
    uint32_t fixed_us;      // measured controller cost model
    uint32_t row_us;
} bwr_band_config;

typedef struct {
    bwr_band_config config;
    uint16_t age[BWR_PANEL_HEIGHT]; // one entry per possible band
    uint64_t calls;
} bwr_band_scheduler;

typedef struct {
    bool changed;
    bool forced_by_age;
    int y0;
    int y1; // inclusive
    unsigned band_index;
    uint32_t error_bits;
    uint32_t motion_bits;
    uint32_t total_error_bits;
    uint32_t remaining_error_bits;
    uint16_t age;
    uint64_t score;
    uint64_t predicted_us;
} bwr_band_plan;

bool bwr_band_scheduler_init(bwr_band_scheduler *scheduler, const bwr_band_config *config);
void bwr_band_scheduler_reset(bwr_band_scheduler *scheduler);

// Select one horizontal band. previous_desired may be NULL when motion history
// is unavailable. Calling this function advances band ages; a panel failure is
// expected to remain a fail-stop rather than retrying the returned plan.
bool bwr_band_scheduler_next(bwr_band_scheduler *scheduler, const uint8_t *desired,
                             const uint8_t *visible, const uint8_t *previous_desired,
                             bwr_band_plan *plan);

// Construct the next physical state: visible everywhere except the selected
// band, which is copied from desired. output may alias visible, but not desired.
bool bwr_band_apply(const uint8_t *desired, const uint8_t *visible,
                    const bwr_band_plan *plan, uint8_t *output);
