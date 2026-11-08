// SPDX-License-Identifier: GPL-3.0-or-later
#include "bwr_badge_port.h"
#include "refresh_lab.h"
#include "refresh_remote.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
static EXT_RAM_BSS_ATTR uint8_t base[SHAPE_BYTES];
static unsigned planned_frames = LAB_FRAMES;
static const unsigned clocks[] = {4, 5, 8, 12, 20};
static EXT_RAM_BSS_ATTR uint64_t reference_us[LAB_PATTERN_COUNT];
enum { FIELD_Y = 168, FIELD_H = 352 };

static void scene(unsigned pattern, unsigned clock, bool running)
{
    memset(fb, 0, sizeof fb);
    text(32, SHAPE_TOP + 12, 3, "REFRESH LAB", true);
    char line[96];
    snprintf(line, sizeof line, "%s | %u MHz %s", refresh_pattern_name(pattern), clocks[clock],
             clocks[clock] > 5    ? "OVERCLOCK"
             : clocks[clock] == 5 ? "family spec"
                                  : "baseline");
    text(32, SHAPE_TOP + 58, 2, line, true);
    if (running)
        snprintf(line, sizeof line, "RUNNING: %u frames, no added delay. A stops.", planned_frames);
    else
        snprintf(line, sizeof line, "MENU + pattern | MENU - SPI | A run | B Home");
    text(32, SHAPE_H - 128, 2, line, true);
    text(32, SHAPE_H - 92, 2, "Results appear after the run, keeping timing clean.", true);
    text(32, SHAPE_H - 56, 1,
         "Watch for ghosting, missing pixels or corruption when comparing clocks.", true);
    memcpy(base, fb, SHAPE_BYTES);
}
static void pattern_draw(unsigned pattern, unsigned frame)
{
    memcpy(fb, base, SHAPE_BYTES);
    if (pattern == 0) {
        int x = 40 + (int)((frame * 37u) % 784u), y = FIELD_Y + 24 + (int)((frame * 19u) % 208u);
        box(x, y, 96, 96, true);
    } else if (pattern == 1) {
        int x = 32 + (int)((frame * 43u) % 848u);
        box(x, FIELD_Y, 48, FIELD_H, true);
    } else if (pattern == 2) {
        for (int y = 0; y < FIELD_H; y += 32)
            for (int x = 32; x < SHAPE_W - 32; x += 32)
                if (((unsigned)(x / 32 + y / 32) + frame) & 1u)
                    box(x, FIELD_Y + y, 32, 32, true);
    } else if (pattern == 3)
        box(32, FIELD_Y, SHAPE_W - 64, FIELD_H, (frame & 1u) != 0);
    else {
        outline(SHAPE_W / 2 - 64, FIELD_Y + 96, 128, 128, 4);
        text(SHAPE_W / 2 - 40, FIELD_Y + 148, 2, "CACHE", true);
    }
}
static void results(const refresh_result *r, uint64_t elapsed, unsigned pattern, unsigned clock)
{
    scene(pattern, clock, false);
    char line[128];
    double n = r->calls ? r->calls : 1, seconds = elapsed / 1000000.;
    snprintf(line, sizeof line, "%u calls | %u panel pulls | %.3f MHz actual", r->calls, r->pulls,
             r->actual_khz / 1000.);
    text(32, 176, 2, line, true);
    if (pattern == 4)
        snprintf(line, sizeof line, "Cache control: %.0f calls/sec (not display FPS)",
                 seconds ? r->calls / seconds : 0.);
    else
        snprintf(line, sizeof line, "End-to-end: %.2f Hz | %.1f ms/frame",
                 seconds ? r->pulls / seconds : 0., elapsed / n / 1000.);
    text(32, 216, 2, line, true);
    snprintf(line, sizeof line, "Steady %.1f ms | best %.1f | worst %.1f",
             r->steady ? r->steady_us / (double)r->steady / 1000. : 0., r->best_us / 1000.,
             r->worst_us / 1000.);
    text(32, 256, 2, line, true);
    snprintf(line, sizeof line, "Draw %.2f ms | diff %.2f ms", r->render_us / n / 1000.,
             r->diff_us / n / 1000.);
    text(32, 304, 2, line, true);
    snprintf(line, sizeof line, "Write %.2f ms | panel pull %.2f ms", r->write_us / n / 1000.,
             r->pulls ? r->pull_us / (double)r->pulls / 1000. : 0.);
    text(32, 344, 2, line, true);
    snprintf(line, sizeof line, "Session setup %.1f ms | %u reopens", r->open_us / n / 1000.,
             r->reopens);
    text(32, 384, 2, line, true);
    snprintf(line, sizeof line, "%.1f kB/frame | %.1f SPI transfers/frame", r->bytes / n / 1024.,
             r->transactions / n);
    text(32, 424, 2, line, true);
    if (pattern != 4 && r->calls == LAB_FRAMES) {
        if (!clock) {
            reference_us[pattern] = elapsed;
            snprintf(line, sizeof line, "4 MHz baseline saved. Select a faster SPI clock.");
        } else if (reference_us[pattern])
            snprintf(line, sizeof line, "vs 4 MHz baseline: %.2fx throughput",
                     reference_us[pattern] / (double)elapsed);
        else
            snprintf(line, sizeof line, "Run this pattern at 4 MHz to save a baseline.");
        text(32, 464, 2, line, true);
    }
    text(32, 512, 1, "Command/BUSY timing; judge visible settling and pixel integrity by eye.",
         true);
    box(32, SHAPE_H - 92, SHAPE_W - 64, 24, false);
    text(32, SHAPE_H - 92, 2, "A reruns. Baselines stay until you leave this app.", true);
    ui_frame(1);
    ui_close();
    refresh_remote_finish(pattern, clocks[clock], r, elapsed);
    ESP_LOGI("refresh_lab",
             "pattern=%u request=%uMHz actual=%luKHz calls=%u pulls=%u elapsed_us=%llu "
             "diff_us=%llu open_us=%llu write_us=%llu pull_us=%llu bytes=%llu tx=%llu reopens=%u",
             pattern, clocks[clock], (unsigned long)r->actual_khz, r->calls, r->pulls,
             (unsigned long long)elapsed, (unsigned long long)r->diff_us,
             (unsigned long long)r->open_us, (unsigned long long)r->write_us,
             (unsigned long long)r->pull_us, (unsigned long long)r->bytes,
             (unsigned long long)r->transactions, r->reopens);
}
void app_refresh_lab(void)
{
    unsigned pattern = 0, clock = 0, target = LAB_FRAMES;
    planned_frames = target;
    refresh_remote_active(true);
    memset(reference_us, 0, sizeof reference_us);
    bool running = false;
    refresh_result result = {0};
    uint64_t started = 0, elapsed = 0;
    ui_refresh_shortcut(false); // A+B must not inject an unmeasured full refresh
    ui_protect_overlay(NULL);
    ui_protect_footer(false);
    if (!ui_refresh_lab_begin(clocks[clock])) {
        refresh_remote_active(false);
        ui_refresh_lab_end();
        ui_close();
        ui_refresh_shortcut(true);
        return;
    }
    ui_keys("Pattern", "SPI clock", "Run / stop", "Home");
    scene(pattern, clock, false);
    pattern_draw(pattern, 0);
    ui_frame(1);
    ui_close();
    for (;;) {
        int button = btn_get(running ? 0 : 100);
        bool leave = false;
        if (refresh_remote_control(&leave)) {
            if (leave)
                break;
            if (running) {
                running = false;
                results(&result, elapsed, pattern, clock);
            }
        }
        refresh_request request;
        if (!running && refresh_remote_take(&request)) {
            pattern = request.pattern;
            target = request.frames;
            planned_frames = target;
            for (clock = 0; clocks[clock] != request.mhz; clock++) {
            }
            if (!ui_refresh_lab_begin(clocks[clock]))
                break;
            button = BWR_BTN_A;
        }
        if (button == BWR_BTN_B)
            break;
        if (button == BWR_BTN_PLUS || button == BWR_BTN_MINUS) {
            running = false;
            refresh_remote_running(false);
            if (button == BWR_BTN_PLUS)
                pattern = (pattern + 1) % LAB_PATTERN_COUNT;
            else {
                clock = (clock + 1) % 5;
                if (!ui_refresh_lab_begin(clocks[clock]))
                    break;
            }
            scene(pattern, clock, false);
            pattern_draw(pattern, 0);
            ui_frame(1);
            ui_close();
        } else if (button == BWR_BTN_A) {
            if (running) {
                running = false;
                results(&result, elapsed, pattern, clock);
                continue;
            }
            memset(&result, 0, sizeof result);
            scene(pattern, clock, true);
            pattern_draw(pattern, 0);
            ui_frame(1); // warm-up/setup are outside the timed burst
            started = (uint64_t)esp_timer_get_time();
            elapsed = 0;
            running = true;
            refresh_remote_running(true);
        }
        if (!running)
            continue;
        uint64_t begin = (uint64_t)esp_timer_get_time();
        pattern_draw(pattern, result.calls + 1);
        uint64_t drawn = (uint64_t)esp_timer_get_time();
        ui_frame(1);
        uint64_t end = (uint64_t)esp_timer_get_time();
        refresh_sample sample;
        ui_refresh_lab_sample(&sample);
        refresh_result_add(&result, &sample, end - begin, drawn - begin);
        elapsed = (uint64_t)esp_timer_get_time() - started;
        if (result.calls == target) {
            running = false;
            ui_close();
            results(&result, elapsed, pattern, clock);
        }
    }
    refresh_remote_active(false);
    ui_refresh_lab_end();
    ui_close();
    ui_refresh_shortcut(true);
}
