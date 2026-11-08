// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_video.h"
#include "bwr_badge_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "refresh_lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SemaphoreHandle_t lock;
static uint8_t *clip_bytes;
static bwr_video clip;
static uint32_t clip_crc32, clip_file_crc32;
static bool active, pending, running, stop, leave, cancelled;
static unsigned requested_mhz = 4, requested_frames, sequence, position;
static uint64_t elapsed_us;
static refresh_result last;
void video_init(void)
{
    lock = xSemaphoreCreateMutex();
    configASSERT(lock);
}
bool video_accept(uint8_t *bytes, size_t size)
{
    bwr_video next;
    if (!bwr_video_parse(bytes, size, &next))
        return false;
    uint32_t file_crc = bwr_video_crc32(bytes, size);
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = !active && !pending && !running;
    if (ok) {
        free(clip_bytes);
        clip_bytes = bytes;
        clip = next;
        clip_file_crc32 = file_crc;
        clip_crc32 = (uint32_t)bytes[20] | (uint32_t)bytes[21] << 8 | (uint32_t)bytes[22] << 16 |
                     (uint32_t)bytes[23] << 24;
        position = 0;
        cancelled = false;
        last = (refresh_result){0};
        elapsed_us = 0;
    }
    xSemaphoreGive(lock);
    return ok;
}
bool video_active(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool value = active;
    xSemaphoreGive(lock);
    return value;
}
bool video_request(unsigned mhz, unsigned frames)
{
    if (mhz != 4 && mhz != 5 && mhz != 8 && mhz != 12 && mhz != 20)
        return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    unsigned max_frames = clip.native_ink && clip.hold_ms == 0 ? BWR_VIDEO_MAX_FRAMES : clip.count;
    bool ok = active && !running && !pending && !stop && !leave && clip.frames && frames &&
              frames <= max_frames;
    if (ok) {
        requested_mhz = mhz;
        requested_frames = frames;
        pending = true;
    }
    xSemaphoreGive(lock);
    return ok;
}
void video_stop(bool exit_app)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    pending = false;
    stop = true;
    leave |= exit_app;
    xSemaphoreGive(lock);
}
void video_status(char *out, size_t capacity)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(out, capacity,
             "{\"active\":%s,\"pending\":%s,\"running\":%s,\"loaded\":%u,"
             "\"cancelled\":%s,\"clip_crc32\":%lu,\"clip_file_crc32\":%lu,\"native_ink\":%s,\"hold_"
             "ms\":%u,"
             "\"frame\":%u,\"sequence\":%u,\"mhz\":%u,\"calls\":%u,\"pulls\":%u,"
             "\"elapsed_us\":%llu,\"render_us\":%llu,\"write_us\":%llu,\"pull_"
             "us\":%llu,\"reopens\":%u}",
             active ? "true" : "false", pending ? "true" : "false", running ? "true" : "false",
             clip.count, cancelled ? "true" : "false", (unsigned long)clip_crc32,
             (unsigned long)clip_file_crc32, clip.native_ink ? "true" : "false", clip.hold_ms,
             position, sequence, requested_mhz, last.calls, last.pulls,
             (unsigned long long)elapsed_us, (unsigned long long)last.render_us,
             (unsigned long long)last.write_us, (unsigned long long)last.pull_us, last.reopens);
    xSemaphoreGive(lock);
}
static void title(const char *message)
{
    memset(fb, 0, sizeof fb);
    text(32, SHAPE_TOP + 40, 4, "VIDEO BENCHMARK", true);
    text(32, SHAPE_TOP + 120, 1, message, true);
    text(32, SHAPE_TOP + 176, 2, "USB upload: 240 x 156, monochrome, up to 600 frames", true);
    text(32, SHAPE_TOP + 224, 2, "4x expansion. Playback runs at the panel limit.", true);
    text(32, SHAPE_TOP + 272, 2, "A loops/pauses. +/- change speed while playing.", true);
    ui_keys("4 MHz", "20 MHz", "Loop", "Home");
    ui_frame(1);
    ui_close();
}
void app_video(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    active = true;
    pending = running = stop = leave = false;
    xSemaphoreGive(lock);
    ui_refresh_shortcut(false);
    ui_protect_overlay(NULL);
    ui_protect_footer(false);
    if (clip.native_ink) {
        ui_keys("Fresh sheet", NULL, "Dance again", "Home");
        ui_close();
    } else
        title(clip.frames ? "Clip loaded. Choose a clock or press A to loop."
                          : "Return Home, then upload a .bwrv clip over USB.");
    unsigned mhz = clip.native_ink ? 20 : 4;
    bool fresh_sheet = false, local_loop = false;
    for (;;) {
        int button = btn_get(100);
        if (button == BWR_BTN_B)
            break;
        if (clip.native_ink && button == BWR_BTN_PLUS) {
            ui_wipe();
            memset(fb, 0, sizeof fb);
            ui_frame(1);
            ui_close();
            continue;
        }
        if (clip.native_ink && button == BWR_BTN_MINUS)
            continue;
        if (button == BWR_BTN_PLUS)
            mhz = 4;
        if (button == BWR_BTN_MINUS)
            mhz = 20;
        if (button == BWR_BTN_A || button == BWR_BTN_PLUS || button == BWR_BTN_MINUS) {
            unsigned frames = clip.native_ink && clip.hold_ms == 0 ? BWR_VIDEO_MAX_FRAMES : clip.count;
            if (video_request(mhz, frames))
                local_loop = !clip.native_ink;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        bool exit_now = leave, start = pending;
        unsigned target = requested_frames;
        if (!start && !exit_now)
            stop = false;
        if (start) {
            mhz = requested_mhz;
            pending = false;
            running = true;
            stop = false;
            position = 0;
            cancelled = false;
        }
        xSemaphoreGive(lock);
        if (exit_now)
            break;
        if (!start)
            continue;
        bool continuous = local_loop;
        local_loop = false;
        refresh_result result = {0};
        bool run_cancelled = false;
        if (!ui_refresh_lab_begin(mhz)) {
            ui_refresh_lab_end();
            xSemaphoreTake(lock, portMAX_DELAY);
            last = (refresh_result){0};
            elapsed_us = 0;
            sequence++;
            running = false;
            exit_now = leave;
            xSemaphoreGive(lock);
            if (exit_now)
                break;
            if (!clip.native_ink)
                title("Could not select the requested clock. No video run started.");
            continue;
        }
        if (!clip.native_ink) {
            ui_keys("4 MHz", "20 MHz", "Pause", "Home");
            memset(fb, 0, sizeof fb);
            ui_frame(1); // legacy blank warmup excluded
        } else
            ui_close();
        uint64_t begin = (uint64_t)esp_timer_get_time();
        for (unsigned frame = 0; continuous || frame < target; frame++) {
            xSemaphoreTake(lock, portMAX_DELAY);
            bool halted = stop || leave;
            xSemaphoreGive(lock);
            int key = btn_get(0);
            if (clip.native_ink && key == BWR_BTN_PLUS) {
                fresh_sheet = true;
                run_cancelled = true;
                video_stop(false);
                break;
            }
            if (!clip.native_ink && (key == BWR_BTN_PLUS || key == BWR_BTN_MINUS)) {
                unsigned selected = key == BWR_BTN_PLUS ? 4 : 20;
                if (selected != mhz) {
                    ui_refresh_lab_end();
                    mhz = selected;
                    xSemaphoreTake(lock, portMAX_DELAY);
                    requested_mhz = mhz;
                    xSemaphoreGive(lock);
                    if (!ui_refresh_lab_begin(mhz)) {
                        run_cancelled = true;
                        video_stop(false);
                        break;
                    }
                }
            }
            if (key == BWR_BTN_A || key == BWR_BTN_B || halted) {
                run_cancelled = true;
                if (key == BWR_BTN_B)
                    video_stop(true);
                break;
            }
            uint64_t t0 = esp_timer_get_time();
            bwr_video_draw(&clip, (clip.native_ink || continuous) ? frame % clip.count : frame, fb);
            uint64_t drawn = esp_timer_get_time();
            ui_frame(1);
            uint64_t end = esp_timer_get_time();
            refresh_sample sample;
            ui_refresh_lab_sample(&sample);
            refresh_result_add(&result, &sample, end - t0, drawn - t0);
            xSemaphoreTake(lock, portMAX_DELAY);
            position = continuous ? frame % clip.count + 1 : frame + 1;
            xSemaphoreGive(lock);
            if (clip.native_ink && clip.hold_ms) {
                ui_close(); // pigment remains; shut the booster down during the long hold
                uint64_t until = (uint64_t)esp_timer_get_time() + (uint64_t)clip.hold_ms * 1000;
                while ((uint64_t)esp_timer_get_time() < until) {
                    int64_t remaining = (int64_t)until - esp_timer_get_time();
                    if (remaining <= 0)
                        break;
                    int wait_ms = (int)((remaining + 999) / 1000);
                    int key = btn_get(wait_ms > 100 ? 100 : wait_ms);
                    if (key == BWR_BTN_PLUS) {
                        fresh_sheet = true;
                        run_cancelled = true;
                        video_stop(false);
                        break;
                    }
                    if (key == BWR_BTN_A || key == BWR_BTN_B) {
                        run_cancelled = true;
                        video_stop(key == BWR_BTN_B);
                        break;
                    }
                    xSemaphoreTake(lock, portMAX_DELAY);
                    bool halted = stop || leave;
                    xSemaphoreGive(lock);
                    if (halted) {
                        run_cancelled = true;
                        break;
                    }
                }
            }
        }
        uint64_t elapsed = (uint64_t)esp_timer_get_time() - begin;
        ui_close();
        ui_refresh_lab_end();
        xSemaphoreTake(lock, portMAX_DELAY);
        last = result;
        cancelled = run_cancelled;
        elapsed_us = elapsed;
        sequence++;
        running = false;
        stop = false;
        exit_now = leave;
        xSemaphoreGive(lock);
        if (exit_now)
            break;
        char line[96];
        snprintf(line, sizeof line, "%u frames / %.2f sec = %.2f fps, %u physical pulls",
                 result.calls, elapsed / 1000000., elapsed ? result.calls * 1000000. / elapsed : 0.,
                 result.pulls);
        if (fresh_sheet) {
            ui_wipe();
            memset(fb, 0, sizeof fb);
            ui_frame(1);
            ui_close();
            fresh_sheet = false;
        }
        if (!clip.native_ink)
            title(continuous ? "Loop paused. Choose a clock or press A to loop again." : line);
    }
    ui_refresh_lab_end();
    ui_close();
    ui_refresh_shortcut(true);
    xSemaphoreTake(lock, portMAX_DELAY);
    active = pending = running = stop = leave = false;
    xSemaphoreGive(lock);
}
