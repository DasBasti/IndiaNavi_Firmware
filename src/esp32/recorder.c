/*
 * Track recording
 *
 * The app starts and stops a recording over Bluetooth. A recording is the GPX
 * file TRACKS/XXXXXXXX.GPX on the SD card, the name is the start time as hex
 * number (it has to fit into an 8.3 name). The GPS task writes the points, see
 * gps.c. The running recording is kept in NVS, so it goes on after a restart
 * until the app stops it.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <esp_log.h>
#include <ff.h>
#include <freertos/FreeRTOS.h>
#include <nvs.h>

#include "navi/ble.h"
#include "navi/recorder.h"
#include "navi/sd.h"

#define NVS_NAMESPACE "recorder"
#define NVS_KEY_ID "id"

static const char* TAG = "RECORDER";

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t active_id;      /// start time of the running recording, 0 if none
static uint32_t generation = 1; /// changes with every start and stop, the GPS task opens the file again then
static uint32_t file_size;
static uint32_t last_point;

static bool store_id(uint32_t id)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, NVS_KEY_ID, id);
        if (err == ESP_OK)
            err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "can not store the recording: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

/**
 * Continue the recording that ran before the restart. NVS has to be initialized.
 */
void recorder_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return; // never recorded
    uint32_t id = 0;
    if (nvs_get_u32(nvs, NVS_KEY_ID, &id) == ESP_OK && id) {
        active_id = id;
        ESP_LOGI(TAG, "recording %08lX goes on", (unsigned long)id);
    }
    nvs_close(nvs);
}

/**
 * FatFs path of a recording, "//TRACKS/XXXXXXXX.GPX"
 */
void recorder_path(char* out, size_t size, uint32_t id)
{
    char name[BLEP_RECORDING_NAME_LEN + 1];
    blep_recording_file_name(name, id);
    snprintf(out, size, "//" BLEP_RECORDING_FOLDER "/%s", name);
}

static bool recording_exists(uint32_t id)
{
    char path[RECORDER_PATH_LEN];
    recorder_path(path, sizeof(path), id);
    if (!sd_lock())
        return false;
    FILINFO info;
    bool exists = f_stat(path, &info) == FR_OK;
    sd_unlock();
    return exists;
}

static void changed(void)
{
    taskENTER_CRITICAL(&lock);
    generation++;
    taskEXIT_CRITICAL(&lock);
    ble_if_recording_changed();
}

/**
 * Start a new recording. It needs the time, the app sets it when it connects.
 */
recorder_result_t recorder_start(void)
{
    time_t now = time(NULL);
    if (now < BLEP_TIME_MIN)
        return RECORDER_ERR_TIME;
    if (active_id)
        return RECORDER_ERR_STATE;

    // stopped and started again in the same second
    uint32_t id = (uint32_t)now;
    while (recording_exists(id))
        id++;

    // without NVS the recording still runs, until the next restart
    store_id(id);
    taskENTER_CRITICAL(&lock);
    active_id = id;
    file_size = 0;
    last_point = 0;
    taskEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "recording %08lX started", (unsigned long)id);
    changed();
    return RECORDER_OK;
}

recorder_result_t recorder_stop(void)
{
    uint32_t id = active_id;
    if (!id)
        return RECORDER_ERR_STATE;
    store_id(0);
    taskENTER_CRITICAL(&lock);
    active_id = 0;
    file_size = 0;
    last_point = 0;
    taskEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "recording %08lX stopped", (unsigned long)id);
    changed();
    return RECORDER_OK;
}

/**
 * Delete a recording. The running one has to be stopped first.
 */
recorder_result_t recorder_delete(uint32_t id)
{
    if (id == 0 || id == active_id)
        return RECORDER_ERR_STATE;
    char path[RECORDER_PATH_LEN];
    recorder_path(path, sizeof(path), id);
    if (!sd_lock())
        return RECORDER_ERR_SD;
    FRESULT res = f_unlink(path);
    sd_unlock();
    if (res == FR_NO_FILE || res == FR_NO_PATH)
        return RECORDER_ERR_NOT_FOUND;
    if (res != FR_OK) {
        ESP_LOGE(TAG, "delete %s: %d", path, res);
        return RECORDER_ERR_SD;
    }
    ESP_LOGI(TAG, "recording %08lX deleted", (unsigned long)id);
    return RECORDER_OK;
}

blep_recording_status_t recorder_status(void)
{
    taskENTER_CRITICAL(&lock);
    blep_recording_status_t status = {
        .recording = active_id != 0,
        .id = active_id,
        .size = file_size,
        .last_point = last_point,
    };
    taskEXIT_CRITICAL(&lock);
    return status;
}

/**
 * For the GPS task: the running recording (0 if none) and the generation, which
 * changes when a recording starts or stops.
 */
uint32_t recorder_active(uint32_t* current_generation)
{
    taskENTER_CRITICAL(&lock);
    uint32_t id = active_id;
    *current_generation = generation;
    taskEXIT_CRITICAL(&lock);
    return id;
}

/**
 * For the GPS task: a point of the recording id is on the card
 */
void recorder_point_written(uint32_t id, uint32_t size, uint32_t time)
{
    taskENTER_CRITICAL(&lock);
    bool current = id == active_id;
    if (current) {
        file_size = size;
        last_point = time;
    }
    taskEXIT_CRITICAL(&lock);
    if (current)
        ble_if_recording_changed();
}

/**
 * Up to max recordings from index first on, in the order of the folder. total is the number of all recordings.
 * The size of the running recording is the one the GPS task wrote, the folder is only updated when it closes the file.
 *
 * @return number of entries, or -1 if the SD card can not be read
 */
int recorder_list(uint16_t first, blep_recording_entry_t* entries, size_t max, uint16_t* total)
{
    *total = 0;
    if (!sd_lock())
        return -1;
    FF_DIR dir;
    FRESULT res = f_opendir(&dir, "//" BLEP_RECORDING_FOLDER);
    if (res == FR_NO_PATH || res == FR_NO_FILE) {
        sd_unlock();
        return 0; // nothing recorded yet
    }
    if (res != FR_OK) {
        sd_unlock();
        return -1;
    }

    blep_recording_status_t status = recorder_status();
    size_t count = 0;
    uint32_t index = 0;
    FILINFO info;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
        uint32_t id;
        if ((info.fattrib & AM_DIR) || !blep_recording_id_from_name(info.fname, &id))
            continue;
        if (index >= first && count < max) {
            entries[count].id = id;
            entries[count].size = id == status.id && status.size > info.fsize ? status.size : (uint32_t)info.fsize;
            count++;
        }
        if (index < UINT16_MAX)
            index++;
    }
    f_closedir(&dir);
    sd_unlock();
    *total = (uint16_t)index;
    return (int)count;
}
