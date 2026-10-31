/*
 * Settings of the display that the app changes over Bluetooth
 *
 * What the map screen shows, the color of the track and how often the screen is updated. They are
 * kept in NVS, so they survive a restart.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <esp_log.h>
#include <nvs.h>

#include "gui.h"
#include "navi/display_settings.h"

#define NVS_NAMESPACE "display"
#define NVS_KEY_FLAGS "flags"
#define NVS_KEY_INTERVAL "interval"
#define NVS_KEY_TRACK_COLOR "track_color"
#define NVS_KEY_ZOOM_NEAR "zoom_near"
#define NVS_KEY_ZOOM_FAR "zoom_far"

static const char* TAG = "SETTINGS";

/* track and height graph are shown by default, like before this setting existed */
static volatile uint8_t flags = BLEP_SETTING_SHOW_TRACK | BLEP_SETTING_SHOW_HEIGHT_GRAPH;
static volatile uint8_t track_color = BLEP_TRACK_COLOR_DEFAULT;
static volatile uint16_t update_interval_s = BLEP_UPDATE_INTERVAL_DEFAULT_S;
static volatile uint8_t zoom_near = BLEP_ZOOM_NEAR_DEFAULT;
static volatile uint8_t zoom_far = BLEP_ZOOM_FAR_DEFAULT;

/**
 * Load the settings from NVS. NVS has to be initialized.
 */
void display_settings_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return; // nothing saved yet

    uint8_t saved_flags;
    uint8_t saved_color;
    uint16_t saved_interval;
    if (nvs_get_u8(nvs, NVS_KEY_FLAGS, &saved_flags) == ESP_OK)
        flags = saved_flags & BLEP_SETTING_FLAGS_MASK;
    if (nvs_get_u8(nvs, NVS_KEY_TRACK_COLOR, &saved_color) == ESP_OK && saved_color <= BLEP_TRACK_COLOR_MAX)
        track_color = saved_color;
    if (nvs_get_u16(nvs, NVS_KEY_INTERVAL, &saved_interval) == ESP_OK)
        update_interval_s = blep_clamp_update_interval(saved_interval);
    uint8_t saved_near;
    uint8_t saved_far;
    if (nvs_get_u8(nvs, NVS_KEY_ZOOM_NEAR, &saved_near) == ESP_OK && nvs_get_u8(nvs, NVS_KEY_ZOOM_FAR, &saved_far) == ESP_OK
        && blep_zoom_valid(saved_near, saved_far)) {
        zoom_near = saved_near;
        zoom_far = saved_far;
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "flags %u, track color %u, update every %u s", (unsigned)flags, (unsigned)track_color,
        (unsigned)update_interval_s);
}

bool display_settings_show_track(void)
{
    return flags & BLEP_SETTING_SHOW_TRACK;
}

bool display_settings_show_height_graph(void)
{
    return flags & BLEP_SETTING_SHOW_HEIGHT_GRAPH;
}

/** Color of the track on the map */
color_t display_settings_track_color(void)
{
    return (color_t)track_color;
}

/** Seconds between two automatic updates of the screen */
uint16_t display_settings_update_interval(void)
{
    return update_interval_s;
}

/** The closer zoom level of the map screen, shown first */
uint8_t display_settings_zoom_near(void)
{
    return zoom_near;
}

/** The wider zoom level of the map screen, shown after the button */
uint8_t display_settings_zoom_far(void)
{
    return zoom_far;
}

blep_zoom_t display_settings_zoom_get(void)
{
    blep_zoom_t zoom = { .near_level = zoom_near, .far_level = zoom_far };
    return zoom;
}

/**
 * Take over new zoom levels, store them and redraw the screen. The vector maps are drawn at any level, so the map
 * screen can use them right away. The tile images of the SD card have to exist for both levels.
 *
 * @return false if the levels are not valid
 */
bool display_settings_zoom_set(const blep_zoom_t* zoom)
{
    if (!blep_zoom_valid(zoom->near_level, zoom->far_level))
        return false;
    if (zoom->near_level == zoom_near && zoom->far_level == zoom_far)
        return true;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, NVS_KEY_ZOOM_NEAR, zoom->near_level);
        if (err == ESP_OK)
            err = nvs_set_u8(nvs, NVS_KEY_ZOOM_FAR, zoom->far_level);
        if (err == ESP_OK)
            err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "can not store zoom levels: %s", esp_err_to_name(err));

    // the new levels are used even if they could not be stored, until the next restart
    zoom_near = zoom->near_level;
    zoom_far = zoom->far_level;
    ESP_LOGI(TAG, "zoom levels %u and %u", (unsigned)zoom_near, (unsigned)zoom_far);
    trigger_rendering();
    return true;
}

blep_settings_t display_settings_get(void)
{
    blep_settings_t settings = { .flags = flags, .track_color = track_color, .update_interval_s = update_interval_s };
    return settings;
}

/**
 * Take over new settings, store them and redraw the screen if they changed.
 *
 * @return false if the settings are not valid or can not be stored
 */
bool display_settings_set(const blep_settings_t* settings)
{
    if ((settings->flags & ~BLEP_SETTING_FLAGS_MASK)
        || settings->update_interval_s < BLEP_UPDATE_INTERVAL_MIN_S
        || settings->update_interval_s > BLEP_UPDATE_INTERVAL_MAX_S
        || settings->track_color > BLEP_TRACK_COLOR_MAX)
        return false;

    bool layout_changed = settings->flags != flags || settings->track_color != track_color;
    bool changed = layout_changed || settings->update_interval_s != update_interval_s;
    if (!changed)
        return true;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, NVS_KEY_FLAGS, settings->flags);
        if (err == ESP_OK)
            err = nvs_set_u16(nvs, NVS_KEY_INTERVAL, settings->update_interval_s);
        if (err == ESP_OK)
            err = nvs_set_u8(nvs, NVS_KEY_TRACK_COLOR, settings->track_color);
        if (err == ESP_OK)
            err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "can not store settings: %s", esp_err_to_name(err));

    // the new values are used even if they could not be stored, until the next restart
    flags = settings->flags;
    track_color = settings->track_color;
    update_interval_s = settings->update_interval_s;
    ESP_LOGI(TAG, "flags %u, track color %u, update every %u s", (unsigned)flags, (unsigned)track_color,
        (unsigned)update_interval_s);

    // what the map screen shows changed, draw it again. A new interval
    // only applies to the updates after this one.
    if (layout_changed)
        trigger_rendering();
    return true;
}
