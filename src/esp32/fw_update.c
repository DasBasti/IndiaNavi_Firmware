/*
 * Firmware update, shared by the HTTP upload server and Bluetooth
 *
 * Writes the image to the inactive OTA partition and checks it. The device
 * boots it after the next restart. The progress is drawn on the display for
 * updates that take a while.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ble_protocol.h"
#include "gui.h"
#include "navi/fw_update.h"

static const char* TAG = "FW";

/* the progress on the display changes every 10 percent, but an e-ink refresh takes long */
#define PROGRESS_STEPS 10
#define PROGRESS_MIN_INTERVAL_US (30LL * 1000 * 1000)
/* an update that is still running after this time shows its progress, whatever the estimate said */
#define PROGRESS_SHOW_AFTER_US ((int64_t)BLEP_OTA_PROGRESS_SCREEN_S * 1000 * 1000)

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static fw_progress_t progress;
static const esp_partition_t* update_partition;
static esp_ota_handle_t update_handle;
static int64_t started_us;
static int64_t drawn_us;
static uint8_t drawn_step;

static void set_state(fw_state_t state)
{
    taskENTER_CRITICAL(&lock);
    progress.state = state;
    progress.result_shown = false;
    taskEXIT_CRITICAL(&lock);
}

static void show_progress(void)
{
    drawn_us = esp_timer_get_time();
    trigger_rendering();
}

/**
 * Start an update with an image of size bytes.
 *
 * @param show_progress draw the progress on the display from the start
 */
fw_result_t fw_update_begin(uint32_t size, bool show_progress_now)
{
    taskENTER_CRITICAL(&lock);
    bool busy = progress.state == FW_ACTIVE;
    if (!busy) {
        progress.state = FW_ACTIVE;
        progress.bytes_total = size;
        progress.bytes_done = 0;
        progress.visible = false;
        progress.result_shown = false;
    }
    taskEXIT_CRITICAL(&lock);
    if (busy)
        return FW_ERR_BUSY;

    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        set_state(FW_IDLE);
        return FW_ERR_FLASH;
    }
    if (size == 0 || size > update_partition->size) {
        set_state(FW_IDLE);
        return FW_ERR_SIZE;
    }

    // erase each sector when it is written instead of the whole image at
    // once, so the flash is not blocked for seconds
    if (esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle) != ESP_OK) {
        set_state(FW_IDLE);
        return FW_ERR_FLASH;
    }
    ESP_LOGI(TAG, "firmware update: %u bytes to %s", (unsigned)size, update_partition->label);

    started_us = esp_timer_get_time();
    drawn_step = 0;
    if (show_progress_now) {
        progress.visible = true;
        show_progress();
    }
    return FW_OK;
}

/**
 * Write the next part of the image
 */
fw_result_t fw_update_write(const void* data, size_t len)
{
    if (progress.state != FW_ACTIVE)
        return FW_ERR_BUSY;
    if (progress.bytes_done + len > progress.bytes_total) {
        fw_update_abort(true);
        return FW_ERR_SIZE;
    }
    if (esp_ota_write(update_handle, data, len) != ESP_OK) {
        fw_update_abort(true);
        return FW_ERR_FLASH;
    }
    progress.bytes_done += len;

    int64_t now = esp_timer_get_time();
    if (!progress.visible && now - started_us > PROGRESS_SHOW_AFTER_US) {
        progress.visible = true;
        show_progress();
    } else if (progress.visible) {
        uint8_t step = (uint64_t)progress.bytes_done * PROGRESS_STEPS / progress.bytes_total;
        if (step != drawn_step && now - drawn_us >= PROGRESS_MIN_INTERVAL_US) {
            drawn_step = step;
            show_progress();
        }
    }

    // every flash write stalls the other core, let its idle task run
    // so the task watchdog does not trigger
    vTaskDelay(1);
    return FW_OK;
}

/**
 * All bytes are written: check the image and select it for the next boot.
 *
 * The display keeps the last progress until the device restarts or the next update of the screen.
 */
fw_result_t fw_update_finish(void)
{
    if (progress.state != FW_ACTIVE)
        return FW_ERR_BUSY;
    if (progress.bytes_done != progress.bytes_total) {
        fw_update_abort(true);
        return FW_ERR_SIZE;
    }

    // checks the image (magic byte, checksum, hash)
    if (esp_ota_end(update_handle) != ESP_OK) {
        update_handle = 0;
        set_state(progress.visible ? FW_ERROR : FW_IDLE);
        if (progress.visible)
            trigger_rendering();
        return FW_ERR_INVALID;
    }
    update_handle = 0;
    if (esp_ota_set_boot_partition(update_partition) != ESP_OK) {
        set_state(progress.visible ? FW_ERROR : FW_IDLE);
        if (progress.visible)
            trigger_rendering();
        return FW_ERR_FLASH;
    }

    ESP_LOGI(TAG, "firmware stored, boots after restart");
    set_state(FW_IDLE);
    return FW_OK;
}

/**
 * Stop the update. The old firmware stays.
 *
 * @param failed show the error on the display once, if the progress was shown
 */
void fw_update_abort(bool failed)
{
    if (progress.state != FW_ACTIVE)
        return;
    if (update_handle) {
        esp_ota_abort(update_handle);
        update_handle = 0;
    }
    bool visible = progress.visible;
    set_state(failed && visible ? FW_ERROR : FW_IDLE);
    ESP_LOGW(TAG, "firmware update stopped after %u of %u bytes", (unsigned)progress.bytes_done, (unsigned)progress.bytes_total);
    // remove the progress box or show the error
    if (visible)
        trigger_rendering();
}

bool fw_update_active(void)
{
    return progress.state == FW_ACTIVE;
}

fw_progress_t fw_update_get_progress(void)
{
    fw_progress_t p;
    taskENTER_CRITICAL(&lock);
    p = progress;
    taskEXIT_CRITICAL(&lock);
    return p;
}

/**
 * The GUI committed a refresh that showed the error. It is only shown once.
 */
void fw_update_result_shown(void)
{
    taskENTER_CRITICAL(&lock);
    if (progress.state == FW_ERROR)
        progress.result_shown = true;
    taskEXIT_CRITICAL(&lock);
}
