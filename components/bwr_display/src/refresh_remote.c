// SPDX-License-Identifier: GPL-3.0-or-later
#include "refresh_remote.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static bool active, pending, running, stop, exit_app;
static refresh_request requested;
static refresh_result last;
static unsigned last_pattern, last_mhz, sequence;
static uint64_t last_elapsed;
void refresh_remote_active(bool on)
{
    taskENTER_CRITICAL(&mux);
    active = on;
    pending = running = stop = exit_app = false;
    taskEXIT_CRITICAL(&mux);
}
bool refresh_remote_is_active(void)
{
    taskENTER_CRITICAL(&mux);
    bool on = active;
    taskEXIT_CRITICAL(&mux);
    return on;
}
bool refresh_remote_submit(unsigned pattern, unsigned mhz, unsigned frames)
{
    if (pattern >= LAB_PATTERN_COUNT || !frames || frames > LAB_FRAMES ||
        (mhz != 4 && mhz != 5 && mhz != 8 && mhz != 12 && mhz != 20))
        return false;
    taskENTER_CRITICAL(&mux);
    bool ok = active && !pending && !running && !stop && !exit_app;
    if (ok) {
        requested = (refresh_request){pattern, mhz, frames};
        pending = true;
    }
    taskEXIT_CRITICAL(&mux);
    return ok;
}
bool refresh_remote_take(refresh_request *request)
{
    taskENTER_CRITICAL(&mux);
    bool ok = active && pending;
    if (ok) {
        *request = requested;
        pending = false;
        running = true;
    }
    taskEXIT_CRITICAL(&mux);
    return ok;
}
void refresh_remote_running(bool on)
{
    taskENTER_CRITICAL(&mux);
    running = on;
    taskEXIT_CRITICAL(&mux);
}
void refresh_remote_finish(unsigned pattern, unsigned mhz, const refresh_result *r,
                           uint64_t elapsed)
{
    taskENTER_CRITICAL(&mux);
    last = *r;
    last_pattern = pattern;
    last_mhz = mhz;
    last_elapsed = elapsed;
    sequence++;
    running = false;
    taskEXIT_CRITICAL(&mux);
}
void refresh_remote_stop(bool leave)
{
    taskENTER_CRITICAL(&mux);
    pending = false;
    stop = true;
    exit_app |= leave;
    taskEXIT_CRITICAL(&mux);
}
bool refresh_remote_control(bool *leave)
{
    taskENTER_CRITICAL(&mux);
    bool requested_stop = stop;
    *leave = exit_app;
    stop = exit_app = false;
    taskEXIT_CRITICAL(&mux);
    return requested_stop;
}
void refresh_remote_status(char *out, size_t size)
{
    taskENTER_CRITICAL(&mux);
    bool a = active, p = pending, r = running;
    refresh_result v = last;
    unsigned seq = sequence, pattern = last_pattern, mhz = last_mhz;
    uint64_t elapsed = last_elapsed;
    taskEXIT_CRITICAL(&mux);
    snprintf(out, size,
             "{\"active\":%s,\"pending\":%s,\"running\":%s,\"sequence\":%u,\"pattern\":%u,\"mhz\":%"
             "u,\"actual_khz\":%lu,\"calls\":%u,\"pulls\":%u,\"elapsed_us\":%llu,\"render_us\":%"
             "llu,\"diff_us\":%llu,\"open_us\":%llu,\"write_us\":%llu,\"pull_us\":%llu,\"bytes\":%"
             "llu,\"transactions\":%llu,\"reopens\":%u,\"best_us\":%llu,\"worst_us\":%llu,"
             "\"steady\":%u,\"steady_us\":%llu}",
             a ? "true" : "false", p ? "true" : "false", r ? "true" : "false", seq, pattern, mhz,
             (unsigned long)v.actual_khz, v.calls, v.pulls, (unsigned long long)elapsed,
             (unsigned long long)v.render_us, (unsigned long long)v.diff_us,
             (unsigned long long)v.open_us, (unsigned long long)v.write_us,
             (unsigned long long)v.pull_us, (unsigned long long)v.bytes,
             (unsigned long long)v.transactions, v.reopens, (unsigned long long)v.best_us,
             (unsigned long long)v.worst_us, v.steady, (unsigned long long)v.steady_us);
}
