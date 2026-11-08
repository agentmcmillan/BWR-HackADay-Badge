// SPDX-License-Identifier: GPL-3.0-or-later
#include "bwr_band_scheduler.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t desired[BWR_PANEL_BYTES], previous[BWR_PANEL_BYTES];
static uint8_t visible[BWR_PANEL_BYTES], next_state[BWR_PANEL_BYTES];

static void ink_rows(uint8_t *frame, int y0, int y1, uint8_t value)
{
    for (int y = y0; y <= y1; y++)
        memset(frame + y * BWR_PANEL_ROW_BYTES, value, BWR_PANEL_ROW_BYTES);
}

int main(void)
{
    bwr_band_scheduler scheduler;
    bwr_band_config config = {
        .band_rows = 16,
        .max_age = 3,
        .motion_weight = 3,
        .fixed_us = 43626,
        .row_us = 800,
    };
    assert(bwr_band_scheduler_init(&scheduler, &config));
    assert(!bwr_band_scheduler_init(NULL, &config));
    bwr_band_config bad = config;
    bad.band_rows = 0;
    assert(!bwr_band_scheduler_init(&scheduler, &bad));

    bwr_band_plan plan;
    assert(!bwr_band_scheduler_next(&scheduler, desired, visible, previous, &plan));
    ink_rows(desired, 32, 47, 0xff);
    ink_rows(desired, 640, 671, 0xff);
    memcpy(previous, desired, sizeof previous);
    ink_rows(previous, 32, 47, 0x00); // current motion in band 2
    assert(bwr_band_scheduler_next(&scheduler, desired, visible, previous, &plan));
    assert(plan.y0 == 32 && plan.y1 == 47 && plan.motion_bits == 16u * 960u);
    assert(plan.total_error_bits == 48u * 960u && plan.remaining_error_bits == 32u * 960u);
    assert(plan.predicted_us == 43626u + 16u * 800u && !plan.forced_by_age);
    assert(bwr_band_apply(desired, visible, &plan, next_state));
    memcpy(visible, next_state, sizeof visible);
    assert(!memcmp(visible + 32 * 120, desired + 32 * 120, 16 * 120));
    assert(memcmp(visible + 640 * 120, desired + 640 * 120, 32 * 120));

    // The shortened final band is selected in two calls and converges exactly.
    assert(bwr_band_scheduler_next(&scheduler, desired, visible, desired, &plan));
    assert(plan.y0 == 640 && plan.y1 == 655 && plan.predicted_us == 56426);
    assert(bwr_band_apply(desired, visible, &plan, next_state));
    memcpy(visible, next_state, sizeof visible);
    assert(bwr_band_scheduler_next(&scheduler, desired, visible, desired, &plan));
    assert(plan.y0 == 656 && plan.y1 == 671);
    assert(bwr_band_apply(desired, visible, &plan, next_state));
    memcpy(visible, next_state, sizeof visible);
    assert(!memcmp(visible, desired, sizeof visible));
    assert(!bwr_band_scheduler_next(&scheduler, desired, visible, desired, &plan));

    // A perpetually high-error hot band cannot starve an older small change.
    memset(desired, 0, sizeof desired);
    memset(previous, 0, sizeof previous);
    memset(visible, 0, sizeof visible);
    bwr_band_scheduler_reset(&scheduler);
    ink_rows(desired, 0, 15, 0xff);
    desired[10 * 120] = 1;
    desired[400 * 120] = 1;
    bool forced_cold = false;
    for (int call = 0; call < 6; call++) {
        // Re-introduce the hot-band error after every simulated commit.
        memset(visible, 0, 16 * 120);
        assert(bwr_band_scheduler_next(&scheduler, desired, visible, previous, &plan));
        if (plan.y0 == 400) {
            assert(plan.forced_by_age);
            forced_cold = true;
            break;
        }
        assert(plan.y0 == 0);
    }
    assert(forced_cold);

    // Deterministic tie breaking picks the lower band.
    memset(desired, 0, sizeof desired);
    memset(visible, 0, sizeof visible);
    bwr_band_scheduler_reset(&scheduler);
    desired[100 * 120] = desired[200 * 120] = 1;
    assert(bwr_band_scheduler_next(&scheduler, desired, visible, NULL, &plan));
    assert(plan.y0 == 96);
    assert(!bwr_band_apply(desired, visible, &(bwr_band_plan){0}, next_state));
    assert(!bwr_band_apply(desired, visible, &plan, desired));

    puts("PASS adaptive band scheduler: motion priority, bounded starvation, exact convergence, "
         "short final band, deterministic ties and measured cost metadata");
}
