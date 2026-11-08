// SPDX-License-Identifier: GPL-3.0-or-later
#include "bwr_band_scheduler.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static unsigned popcount8(uint8_t value)
{
    value = (uint8_t)(value - ((value >> 1) & 0x55));
    value = (uint8_t)((value & 0x33) + ((value >> 2) & 0x33));
    return (value + (value >> 4)) & 0x0f;
}

bool bwr_band_scheduler_init(bwr_band_scheduler *scheduler, const bwr_band_config *config)
{
    if (!scheduler || !config || !config->band_rows || config->band_rows > BWR_PANEL_HEIGHT ||
        !config->max_age || config->max_age > UINT16_MAX || config->motion_weight > 1000 ||
        !config->row_us)
        return false;
    memset(scheduler, 0, sizeof *scheduler);
    scheduler->config = *config;
    return true;
}

void bwr_band_scheduler_reset(bwr_band_scheduler *scheduler)
{
    if (!scheduler)
        return;
    memset(scheduler->age, 0, sizeof scheduler->age);
    scheduler->calls = 0;
}

bool bwr_band_scheduler_next(bwr_band_scheduler *scheduler, const uint8_t *desired,
                             const uint8_t *visible, const uint8_t *previous_desired,
                             bwr_band_plan *plan)
{
    if (!scheduler || !desired || !visible || !plan || !scheduler->config.band_rows)
        return false;
    memset(plan, 0, sizeof *plan);
    unsigned rows = scheduler->config.band_rows;
    unsigned bands = (BWR_PANEL_HEIGHT + rows - 1) / rows;
    uint32_t errors[BWR_PANEL_HEIGHT] = {0};
    uint32_t motions[BWR_PANEL_HEIGHT] = {0};
    uint32_t total = 0;
    for (unsigned band = 0; band < bands; band++) {
        unsigned y0 = band * rows;
        unsigned y1 = y0 + rows < BWR_PANEL_HEIGHT ? y0 + rows : BWR_PANEL_HEIGHT;
        for (unsigned y = y0; y < y1; y++)
            for (unsigned x = 0; x < BWR_PANEL_ROW_BYTES; x++) {
                size_t offset = (size_t)y * BWR_PANEL_ROW_BYTES + x;
                errors[band] += popcount8(desired[offset] ^ visible[offset]);
                if (previous_desired)
                    motions[band] += popcount8(desired[offset] ^ previous_desired[offset]);
            }
        total += errors[band];
        if (!errors[band])
            scheduler->age[band] = 0;
        else if (scheduler->age[band] < UINT16_MAX)
            scheduler->age[band]++;
    }
    scheduler->calls++;
    if (!total)
        return false;

    unsigned chosen = 0;
    bool have = false, forced = false;
    uint64_t best_score = 0;
    uint16_t best_age = 0;
    for (unsigned band = 0; band < bands; band++) {
        if (!errors[band])
            continue;
        unsigned y0 = band * rows;
        unsigned height = y0 + rows < BWR_PANEL_HEIGHT ? rows : BWR_PANEL_HEIGHT - y0;
        uint64_t maximum_bits = (uint64_t)height * BWR_PANEL_WIDTH;
        uint64_t score = errors[band] +
                         (uint64_t)motions[band] * scheduler->config.motion_weight +
                         (uint64_t)scheduler->age[band] * maximum_bits /
                             scheduler->config.max_age;
        bool this_forced = scheduler->age[band] >= scheduler->config.max_age;
        bool better = !have || (this_forced && !forced) ||
                      (this_forced == forced && this_forced &&
                       (scheduler->age[band] > best_age ||
                        (scheduler->age[band] == best_age && score > best_score) ||
                        (scheduler->age[band] == best_age && score == best_score &&
                         band < chosen))) ||
                      (this_forced == forced && !this_forced &&
                       (score > best_score || (score == best_score && band < chosen)));
        if (better) {
            chosen = band;
            forced = this_forced;
            best_score = score;
            best_age = scheduler->age[band];
            have = true;
        }
    }
    if (!have)
        return false;
    unsigned y0 = chosen * rows;
    unsigned height = y0 + rows < BWR_PANEL_HEIGHT ? rows : BWR_PANEL_HEIGHT - y0;
    *plan = (bwr_band_plan){
        .changed = true,
        .forced_by_age = forced,
        .y0 = (int)y0,
        .y1 = (int)(y0 + height - 1),
        .band_index = chosen,
        .error_bits = errors[chosen],
        .motion_bits = motions[chosen],
        .total_error_bits = total,
        .remaining_error_bits = total - errors[chosen],
        .age = scheduler->age[chosen],
        .score = best_score,
        .predicted_us = scheduler->config.fixed_us +
                        (uint64_t)scheduler->config.row_us * height,
    };
    scheduler->age[chosen] = 0;
    return true;
}

bool bwr_band_apply(const uint8_t *desired, const uint8_t *visible,
                    const bwr_band_plan *plan, uint8_t *output)
{
    if (!desired || !visible || !plan || !output || output == desired || !plan->changed ||
        plan->y0 < 0 || plan->y1 < plan->y0 || plan->y1 >= BWR_PANEL_HEIGHT)
        return false;
    memmove(output, visible, BWR_PANEL_BYTES);
    size_t offset = (size_t)plan->y0 * BWR_PANEL_ROW_BYTES;
    size_t count = (size_t)(plan->y1 - plan->y0 + 1) * BWR_PANEL_ROW_BYTES;
    memcpy(output + offset, desired + offset, count);
    return true;
}
