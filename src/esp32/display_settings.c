/*
 * Settings of the display that the app changes over Bluetooth
 *
 * What the map screen shows and how often the screen is updated. They are
 * kept in NVS, so they survive a restart.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <esp_log.h>
#include <nvs.h>

#include "tasks.h"

#define NVS_NAMESPACE "display"
#define NVS_KEY_FLAGS "flags"
#define NVS_KEY_INTERVAL "interval"

static const char* TAG = "SETTINGS";

/* track and height graph are shown by default, like before this setting existed */
static volatile uint8_t flags = BLEP_SETTING_SHOW_TRACK | BLEP_SETTING_SHOW_HEIGHT_GRAPH;
static volatile uint16_t update_interval_s = BLEP_UPDATE_INTERVAL_DEFAULT_S;

/**
 * Load the settings from NVS. NVS has to be initialized.
 */
void display_settings_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return; // nothing saved yet

    uint8_t saved_flags;
    uint16_t saved_interval;
    if (nvs_get_u8(nvs, NVS_KEY_FLAGS, &saved_flags) == ESP_OK)
        flags = saved_flags & BLEP_SETTING_FLAGS_MASK;
    if (nvs_get_u16(nvs, NVS_KEY_INTERVAL, &saved_interval) == ESP_OK)
        update_interval_s = blep_clamp_update_interval(saved_interval);
    nvs_close(nvs);
    ESP_LOGI(TAG, "flags %u, update every %u s", (unsigned)flags, (unsigned)update_interval_s);
}

bool display_settings_show_track(void)
{
    return flags & BLEP_SETTING_SHOW_TRACK;
}

bool display_settings_show_height_graph(void)
{
    return flags & BLEP_SETTING_SHOW_HEIGHT_GRAPH;
}

/** Seconds between two automatic updates of the screen */
uint16_t display_settings_update_interval(void)
{
    return update_interval_s;
}

blep_settings_t display_settings_get(void)
{
    blep_settings_t settings = { .flags = flags, .update_interval_s = update_interval_s };
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
        || settings->update_interval_s > BLEP_UPDATE_INTERVAL_MAX_S)
        return false;

    bool layout_changed = settings->flags != flags;
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
            err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "can not store settings: %s", esp_err_to_name(err));

    // the new values are used even if they could not be stored, until the next restart
    flags = settings->flags;
    update_interval_s = settings->update_interval_s;
    ESP_LOGI(TAG, "flags %u, update every %u s", (unsigned)flags, (unsigned)update_interval_s);

    // what the map screen shows changed, draw it again. A new interval
    // only applies to the updates after this one.
    if (layout_changed)
        trigger_rendering();
    return true;
}
