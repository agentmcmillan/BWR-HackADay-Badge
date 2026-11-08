// SPDX-License-Identifier: GPL-3.0-or-later
// Porting reference: these are the project's added hooks, not a panel driver.
// Include after the host driver declarations listed in docs/INTEGRATION.md.
#include "game_tag.h"
#include "refresh_remote.h"
#include <stdatomic.h>
static atomic_bool game_tag_active, game_tag_enabled;
static bool game_tag_valid, game_source_valid, game_tag_native;
static atomic_bool game_tag_busy;
static int64_t game_tag_cleaned_at;
static EXT_RAM_BSS_ATTR uint8_t game_logical[PLANE], game_ink[PLANE], game_red[PLANE];
static unsigned panel_spi_hz = 4000000, panel_actual_khz = 4000;
static bool panel_rate_pending, panel_measure;
static uint64_t panel_bytes, panel_transactions;
static refresh_sample panel_sample;
#define BTN_REFRESH_LAB 100
// Partial updates accumulate ghosting; force a full waveform at least this often.
#define GAME_TAG_FULL_REFRESH_US 180000000 // 3 minutes
static bool game_tag_present_locked(bool force, bool cached);
static bool game_tag_present_locked(bool force, bool cached)
{
    if (!cached) {
        memcpy(game_logical, fb, PLANE);
        game_source_valid = true;
    }
    if (!game_source_valid)
        return false;
    bool full = force || !game_tag_valid ||
                esp_timer_get_time() - game_tag_cleaned_at >= GAME_TAG_FULL_REFRESH_US;
    if (full) {
        atomic_store(&game_tag_busy, true);
        if (game_tag_enabled && !film_has_red && !cfg_set("panel_red", "1")) {
            atomic_store(&game_tag_busy, false);
            return false;
        }
        if (game_tag_enabled)
            film_has_red = true;
    }
    game_tag_compose_named(game_logical, game_ink, game_red, game_tag_enabled, game_tag_native,
                           "YOUR NAME");
    draw_bar();
    memcpy(game_ink, fb, TOP * (W / 8));
    ui_led_activity(true);
    film_clean_white = false;
    if (full) {
        epd_sleep();
        epd_reset();
        epd_load_planes(game_ink, game_red);
        epd_start(NULL);
        epd_pull(180000);
        epd_close();
        memcpy(part_shown, game_ink, PLANE);
        memcpy(panel_red_image, game_red, PLANE);
        game_tag_valid = true;
        game_tag_cleaned_at = esp_timer_get_time();
    } else
        epd_partial(game_ink);
    panel_image_valid = true;
    ui_led_activity(false);
    atomic_store(&game_tag_busy, false);
    return true;
}

void ui_game_mode(bool active)
{
    xSemaphoreTake(panel_lock, portMAX_DELAY);
    if (active != game_tag_active) {
        game_tag_active = active;
        game_tag_enabled = active;
        game_tag_valid = false;
        game_source_valid = false;
        game_tag_native = false;
        overlay_protected = footer_protected = false;
        if (active) {
            memset(game_logical, 0, PLANE);
            game_source_valid = true;
            game_tag_present_locked(true, true); // before physics/renderer tasks start
        }
    }
    xSemaphoreGive(panel_lock);
}

void ui_game_native_view(bool enabled)
{
    xSemaphoreTake(panel_lock, portMAX_DELAY);
    if (game_tag_native != enabled) {
        game_tag_native = enabled;
        game_source_valid = false;
    }
    xSemaphoreGive(panel_lock);
}

bool ui_game_mode_active(void)
{
    return game_tag_active;
}

bool ui_game_tag_enabled(void)
{
    return game_tag_active && game_tag_enabled;
}

bool ui_game_frozen(void)
{
    return atomic_load(&game_tag_busy);
}

bool ui_game_wait(void)
{
    bool waited = false;
    while (ui_game_frozen()) {
        waited = true;
        ms(20);
    }
    if (waited)
        btn_flush();
    return waited;
}

void ui_game_tag_toggle(void)
{
    xSemaphoreTake(panel_lock, portMAX_DELAY);
    if (game_tag_active && home_enabled) {
        game_tag_enabled = !game_tag_enabled;
        game_tag_valid = false;
        // Shape modes repaint their own geometry on the next frame, even paused.
        if (!game_tag_native)
            game_tag_present_locked(true, true);
    }
    xSemaphoreGive(panel_lock);
}

bool ui_refresh_lab_begin(unsigned mhz)
{
    if (mhz != 4 && mhz != 5 && mhz != 8 && mhz != 12 && mhz != 20)
        return false;
    xSemaphoreTake(panel_lock, portMAX_DELAY);
    epd_sleep(); // complete shutdown at the previous clock before changing it
    panel_spi_hz = mhz * 1000000u;
    panel_rate_pending = true;
    panel_measure = true;
    memset(&panel_sample, 0, sizeof panel_sample);
    xSemaphoreGive(panel_lock);
    return true;
}

void ui_refresh_lab_end(void)
{
    xSemaphoreTake(panel_lock, portMAX_DELAY);
    epd_sleep();
    panel_measure = false;
    panel_spi_hz = 4000000;
    panel_rate_pending = true;
    xSemaphoreGive(panel_lock);
}

void ui_refresh_lab_sample(refresh_sample *out)
{
    xSemaphoreTake(panel_lock, portMAX_DELAY);
    *out = panel_sample;
    xSemaphoreGive(panel_lock);
}

bool ui_refresh_lab_open(void)
{
    if (in_app)
        return refresh_remote_is_active();
    int event = BTN_REFRESH_LAB;
    return xQueueSend(btnq, &event, 0) == pdTRUE;
}
