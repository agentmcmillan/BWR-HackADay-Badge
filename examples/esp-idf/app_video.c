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
static bool active, pending, running, stop, leave;
static unsigned requested_mhz = 4, requested_frames, sequence, position;
static uint64_t elapsed_us;
static refresh_result last;
void video_init(void) {
  lock = xSemaphoreCreateMutex();
  configASSERT(lock);
}
bool video_accept(uint8_t *bytes, size_t size) {
  bwr_video next;
  if (!bwr_video_parse(bytes, size, &next))
    return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  bool ok = !active && !pending && !running;
  if (ok) {
    free(clip_bytes);
    clip_bytes = bytes;
    clip = next;
    position = 0;
    last = (refresh_result){0};
    elapsed_us = 0;
  }
  xSemaphoreGive(lock);
  return ok;
}
bool video_active(void) {
  xSemaphoreTake(lock, portMAX_DELAY);
  bool value = active;
  xSemaphoreGive(lock);
  return value;
}
bool video_request(unsigned mhz, unsigned frames) {
  if (mhz != 4 && mhz != 5 && mhz != 8 && mhz != 12 && mhz != 20)
    return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  bool ok = active && !running && !pending && !stop && !leave && clip.frames &&
            frames && frames <= clip.count;
  if (ok) {
    requested_mhz = mhz;
    requested_frames = frames;
    pending = true;
  }
  xSemaphoreGive(lock);
  return ok;
}
void video_stop(bool exit_app) {
  xSemaphoreTake(lock, portMAX_DELAY);
  pending = false;
  stop = true;
  leave = exit_app;
  xSemaphoreGive(lock);
}
void video_status(char *out, size_t capacity) {
  xSemaphoreTake(lock, portMAX_DELAY);
  snprintf(out, capacity,
           "{\"active\":%s,\"pending\":%s,\"running\":%s,\"loaded\":%u,"
           "\"frame\":%u,\"sequence\":%u,\"mhz\":%u,\"calls\":%u,\"pulls\":%u,"
           "\"elapsed_us\":%llu,\"render_us\":%llu,\"write_us\":%llu,\"pull_"
           "us\":%llu,\"reopens\":%u}",
           active ? "true" : "false", pending ? "true" : "false",
           running ? "true" : "false", clip.count, position, sequence,
           requested_mhz, last.calls, last.pulls,
           (unsigned long long)elapsed_us, (unsigned long long)last.render_us,
           (unsigned long long)last.write_us, (unsigned long long)last.pull_us,
           last.reopens);
  xSemaphoreGive(lock);
}
static void title(const char *message) {
  memset(fb, 0, sizeof fb);
  text(32, SHAPE_TOP + 40, 4, "VIDEO BENCHMARK", true);
  text(32, SHAPE_TOP + 120, 1, message, true);
  text(32, SHAPE_TOP + 176, 2, "USB upload: 240 x 156, monochrome, up to 600 frames",
       true);
  text(32, SHAPE_TOP + 224, 2, "4x expansion. No dropped frames or added pacing.",
       true);
  text(32, SHAPE_TOP + 272, 2, "Silent playback; panel ghosting remains visible.",
       true);
  ui_keys("4 MHz", "20 MHz", "Replay", "Home");
  ui_frame(1);
  ui_close();
}
void app_video(void) {
  xSemaphoreTake(lock, portMAX_DELAY);
  active = true;
  pending = running = stop = leave = false;
  xSemaphoreGive(lock);
  ui_refresh_shortcut(false);
  ui_protect_overlay(NULL);
  ui_protect_footer(false);
  title(clip.frames ? "Clip loaded. A replays at 4 MHz."
                    : "Return Home, then upload a .bwrv clip over USB.");
  unsigned mhz = 4;
  for (;;) {
    int button = btn_get(100);
    if (button == BWR_BTN_B)
      break;
    if (button == BWR_BTN_PLUS)
      mhz = 4;
    if (button == BWR_BTN_MINUS)
      mhz = 20;
    if (button == BWR_BTN_A || button == BWR_BTN_PLUS || button == BWR_BTN_MINUS)
      video_request(mhz, clip.count);
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
    }
    xSemaphoreGive(lock);
    if (exit_now)
      break;
    if (!start)
      continue;
    refresh_result result = {0};
    ui_refresh_lab_begin(mhz);
    memset(fb, 0, sizeof fb);
    ui_frame(1); // blank warmup excluded; every clip frame is timed
    uint64_t begin = (uint64_t)esp_timer_get_time();
    for (unsigned frame = 0; frame < target; frame++) {
      xSemaphoreTake(lock, portMAX_DELAY);
      bool halted = stop || leave;
      xSemaphoreGive(lock);
      int key = btn_get(0);
      if (key == BWR_BTN_A || key == BWR_BTN_B || halted) {
        if (key == BWR_BTN_B)
          video_stop(true);
        break;
      }
      uint64_t t0 = esp_timer_get_time();
      bwr_video_draw(&clip, frame, fb);
      uint64_t drawn = esp_timer_get_time();
      ui_frame(1);
      uint64_t end = esp_timer_get_time();
      refresh_sample sample;
      ui_refresh_lab_sample(&sample);
      refresh_result_add(&result, &sample, end - t0, drawn - t0);
      xSemaphoreTake(lock, portMAX_DELAY);
      position = frame + 1;
      xSemaphoreGive(lock);
    }
    uint64_t elapsed = (uint64_t)esp_timer_get_time() - begin;
    ui_close();
    ui_refresh_lab_end();
    xSemaphoreTake(lock, portMAX_DELAY);
    last = result;
    elapsed_us = elapsed;
    sequence++;
    running = false;
    stop = false;
    exit_now = leave;
    xSemaphoreGive(lock);
    if (exit_now)
      break;
    char line[96];
    snprintf(line, sizeof line,
             "%u frames / %.2f sec = %.2f fps, %u physical pulls", result.calls,
             elapsed / 1000000.,
             elapsed ? result.calls * 1000000. / elapsed : 0., result.pulls);
    title(line);
  }
  ui_refresh_lab_end();
  ui_close();
  ui_refresh_shortcut(true);
  xSemaphoreTake(lock, portMAX_DELAY);
  active = pending = running = stop = leave = false;
  xSemaphoreGive(lock);
}
