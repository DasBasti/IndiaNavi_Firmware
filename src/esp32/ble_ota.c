/*
 * Firmware update over Bluetooth
 *
 * The phone starts an update with the size of the image and sends the image
 * in chunks without response. The host task of the Bluetooth stack only puts
 * the chunks into a buffer, a task of its own writes them to flash, because
 * erasing a sector takes up to 400 ms and must not block the stack. The
 * device reports how many bytes are in flash, the phone never sends more than
 * BLEP_OTA_WINDOW bytes beyond that. See IndiaNavi_App/docs/ble_api.md.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef ESP_S3

#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>

#include "ble_internal.h"
#include "tasks.h"

static const char* TAG = "BLE_OTA";

/* the chunks the phone sent but the task did not write to flash yet */
#define BUFFER_SIZE (2 * BLEP_OTA_WINDOW)
#define TASK_STACK_SIZE 4096
/* a partial chunk is written when nothing arrived for this long, the phone waits for the report */
#define FLUSH_AFTER_MS 200
/* the new firmware is selected, the phone has this long to ask for the restart */
#define RESTART_WAIT_MS (10 * 60 * 1000)
#define RESTART_DELAY_MS 500
#define ATT_ERR_INVALID_LENGTH 0x0d

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
    blep_ota_state_t state;
    blep_ota_error_t error;
    uint32_t total;
    volatile uint32_t flashed;  /// written by the task
    uint32_t received;          /// written by the host task
    int64_t last_data_us;
    StreamBufferHandle_t buffer;
    TaskHandle_t task;
    volatile bool abort_requested;
    volatile bool finish_requested;
    volatile bool restart_requested;
    volatile bool disconnected;
    volatile blep_ota_error_t failure; /// the reason of an abort that is not asked by the phone
} ota;

static bool session_active(void)
{
    return ota.state == BLEP_OTA_READY || ota.state == BLEP_OTA_RECEIVING || ota.state == BLEP_OTA_VERIFYING
        || ota.state == BLEP_OTA_DONE;
}

static void set_state(blep_ota_state_t state, blep_ota_error_t error)
{
    taskENTER_CRITICAL(&lock);
    ota.state = state;
    ota.error = error;
    taskEXIT_CRITICAL(&lock);
}

static void notify_status(void)
{
    uint8_t status[BLEP_OTA_STATUS_SIZE];
    ble_ota_status_read(status);
    ble_if_notify_ota_status(status);
}

void ble_ota_status_read(uint8_t status[BLEP_OTA_STATUS_SIZE])
{
    taskENTER_CRITICAL(&lock);
    blep_ota_state_t state = ota.state;
    blep_ota_error_t error = ota.error;
    uint32_t flashed = ota.flashed;
    taskEXIT_CRITICAL(&lock);
    blep_ota_status_encode(status, state, error, flashed);
}

/* Stop the update from the host task: the task cleans up, it owns the flash */
static void request_failure(blep_ota_error_t error)
{
    ota.failure = error;
    ota.abort_requested = true;
    if (ota.task)
        xTaskNotifyGive(ota.task);
}

static void wake_task(void)
{
    if (ota.task)
        xTaskNotifyGive(ota.task);
}

/* Not enough battery for a long update that stresses flash and radio */
static bool battery_ok(void)
{
    return is_charging || current_battery_level >= BLEP_OTA_MIN_BATTERY;
}

static void update_task(void* arg)
{
    uint8_t* chunk = malloc(BLEP_OTA_ACK_INTERVAL);
    size_t fill = 0;
    blep_ota_error_t end_error = BLEP_OTA_NO_ERROR;
    bool end_by_phone = false;

    if (!chunk) {
        end_error = BLEP_OTA_ERR_FLASH;
        goto fail;
    }

    for (;;) {
        if (ota.abort_requested) {
            end_by_phone = ota.failure == BLEP_OTA_NO_ERROR;
            end_error = end_by_phone ? BLEP_OTA_ERR_ABORTED : ota.failure;
            goto fail;
        }

        size_t got = xStreamBufferReceive(ota.buffer, chunk + fill, BLEP_OTA_ACK_INTERVAL - fill, pdMS_TO_TICKS(FLUSH_AFTER_MS));
        fill += got;

        if (fill == BLEP_OTA_ACK_INTERVAL || (got == 0 && fill > 0)) {
            fw_result_t result = fw_update_write(chunk, fill);
            if (result != FW_OK) {
                // the writer stopped the update
                end_error = result == FW_ERR_SIZE ? BLEP_OTA_ERR_SEQUENCE : BLEP_OTA_ERR_FLASH;
                set_state(BLEP_OTA_ERROR, end_error);
                notify_status();
                goto done;
            }
            taskENTER_CRITICAL(&lock);
            ota.flashed += fill;
            taskEXIT_CRITICAL(&lock);
            fill = 0;
            set_state(BLEP_OTA_RECEIVING, BLEP_OTA_NO_ERROR);
            notify_status();
        }

        // after a disconnect everything that arrived is in flash, the phone continues from there
        if (ota.disconnected && fill == 0 && xStreamBufferIsEmpty(ota.buffer)) {
            ota.received = ota.flashed;
            ota.disconnected = false;
        }

        if (ota.finish_requested && fill == 0 && xStreamBufferIsEmpty(ota.buffer)) {
            ota.finish_requested = false;
            if (ota.flashed != ota.total) {
                end_error = BLEP_OTA_ERR_SEQUENCE;
                goto fail;
            }
            set_state(BLEP_OTA_VERIFYING, BLEP_OTA_NO_ERROR);
            notify_status();
            fw_result_t result = fw_update_finish();
            if (result != FW_OK) {
                end_error = result == FW_ERR_INVALID ? BLEP_OTA_ERR_INVALID : BLEP_OTA_ERR_FLASH;
                set_state(BLEP_OTA_ERROR, end_error);
                notify_status();
                goto done;
            }
            set_state(BLEP_OTA_DONE, BLEP_OTA_NO_ERROR);
            notify_status();

            // the image is selected for the next boot, wait for the restart
            int64_t deadline = esp_timer_get_time() + (int64_t)RESTART_WAIT_MS * 1000;
            while (!ota.restart_requested && esp_timer_get_time() < deadline)
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            if (ota.restart_requested) {
                ESP_LOGI(TAG, "restart for the new firmware");
                vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
                esp_restart();
            }
            end_error = BLEP_OTA_NO_ERROR;
            set_state(BLEP_OTA_IDLE, BLEP_OTA_NO_ERROR);
            goto done;
        }

        // nothing arrives, the phone is gone
        if (esp_timer_get_time() - ota.last_data_us > (int64_t)BLEP_OTA_TIMEOUT_S * 1000 * 1000) {
            ESP_LOGW(TAG, "no data for %d s", BLEP_OTA_TIMEOUT_S);
            end_error = BLEP_OTA_ERR_TIMEOUT;
            goto fail;
        }
    }

fail:
    // phone stopped it or something went wrong. The old firmware stays.
    fw_update_abort(!end_by_phone);
    set_state(end_by_phone ? BLEP_OTA_IDLE : BLEP_OTA_ERROR, end_error);
    notify_status();

done:
    ble_if_request_fast_link(false);
    free(chunk);
    StreamBufferHandle_t buffer = ota.buffer;
    ota.buffer = NULL;
    if (buffer)
        vStreamBufferDelete(buffer);
    // last, ble_ota_stop() and the next start wait for it
    ota.task = NULL;
    vTaskDelete(NULL);
}

static int start(const uint8_t* data, uint32_t size)
{
    (void)data;
    // the task of the last update may still clean up
    if (session_active() || ota.task)
        return BLEP_ATT_ERR_VALUE_NOT_ALLOWED;

    blep_ota_error_t error = BLEP_OTA_NO_ERROR;
    if (ble_if_mtu() < BLEP_OTA_MIN_MTU)
        error = BLEP_OTA_ERR_MTU;
    else if (!battery_ok())
        error = BLEP_OTA_ERR_BATTERY;
    else if (upload_get_progress().state == UPLOAD_ACTIVE || fw_update_active())
        error = BLEP_OTA_ERR_BUSY;

    if (error == BLEP_OTA_NO_ERROR) {
        // the estimate decides if the progress is drawn from the start, see BLEP_OTA_PROGRESS_SCREEN_S
        switch (fw_update_begin(size, blep_ota_needs_progress_screen(size))) {
        case FW_OK:
            break;
        case FW_ERR_BUSY:
            error = BLEP_OTA_ERR_BUSY;
            break;
        case FW_ERR_SIZE:
            error = BLEP_OTA_ERR_SIZE;
            break;
        default:
            error = BLEP_OTA_ERR_FLASH;
            break;
        }
    }
    if (error == BLEP_OTA_NO_ERROR) {
        ota.buffer = xStreamBufferCreate(BUFFER_SIZE, 1);
        if (!ota.buffer) {
            fw_update_abort(false);
            error = BLEP_OTA_ERR_FLASH;
        }
    }
    if (error != BLEP_OTA_NO_ERROR) {
        ESP_LOGW(TAG, "update of %u bytes refused: %d", (unsigned)size, error);
        set_state(BLEP_OTA_ERROR, error);
        ota.flashed = 0;
        notify_status();
        return 0;
    }

    ota.total = size;
    ota.flashed = 0;
    ota.received = 0;
    ota.last_data_us = esp_timer_get_time();
    ota.abort_requested = false;
    ota.finish_requested = false;
    ota.restart_requested = false;
    ota.disconnected = false;
    ota.failure = BLEP_OTA_NO_ERROR;
    set_state(BLEP_OTA_READY, BLEP_OTA_NO_ERROR);

    if (xTaskCreate(update_task, "ble_ota", TASK_STACK_SIZE, NULL, 5, &ota.task) != pdPASS) {
        ota.task = NULL;
        fw_update_abort(false);
        vStreamBufferDelete(ota.buffer);
        ota.buffer = NULL;
        set_state(BLEP_OTA_ERROR, BLEP_OTA_ERR_FLASH);
        notify_status();
        return 0;
    }

    ESP_LOGI(TAG, "update of %u bytes started", (unsigned)size);
    ble_if_request_fast_link(true);
    notify_status();
    return 0;
}

/**
 * Write to the OTA control characteristic
 */
int ble_ota_control_write(const uint8_t* data, size_t len)
{
    uint8_t command;
    uint32_t size;
    blep_err_t err = blep_ota_command_decode(data, len, &command, &size);
    if (err == BLEP_ERR_LENGTH)
        return ATT_ERR_INVALID_LENGTH;
    if (err != BLEP_OK)
        return BLEP_ATT_ERR_VALUE_NOT_ALLOWED;

    switch (command) {
    case BLEP_OTA_CMD_START:
        return start(data, size);
    case BLEP_OTA_CMD_ABORT:
        if (session_active()) {
            ota.failure = BLEP_OTA_NO_ERROR;
            ota.abort_requested = true;
            wake_task();
        }
        return 0;
    case BLEP_OTA_CMD_FINISH:
        if (ota.state != BLEP_OTA_READY && ota.state != BLEP_OTA_RECEIVING)
            return BLEP_ATT_ERR_VALUE_NOT_ALLOWED;
        ota.finish_requested = true;
        wake_task();
        return 0;
    case BLEP_OTA_CMD_RESTART:
        if (ota.state != BLEP_OTA_DONE)
            return BLEP_ATT_ERR_VALUE_NOT_ALLOWED;
        ota.restart_requested = true;
        wake_task();
        return 0;
    }
    return BLEP_ATT_ERR_VALUE_NOT_ALLOWED;
}

/**
 * Write to the OTA data characteristic, called from the host task of the Bluetooth stack
 */
int ble_ota_data_write(const uint8_t* data, size_t len)
{
    // data after an error or without START is dropped, the phone sees the state
    if ((ota.state != BLEP_OTA_READY && ota.state != BLEP_OTA_RECEIVING) || !ota.buffer || ota.abort_requested)
        return 0;

    if (ota.received + len > ota.total) {
        request_failure(BLEP_OTA_ERR_SEQUENCE);
        return 0;
    }
    // the phone waits for the reports, if it does not the buffer is full
    if (xStreamBufferSend(ota.buffer, data, len, 0) != len) {
        request_failure(BLEP_OTA_ERR_OVERFLOW);
        return 0;
    }
    ota.received += len;
    ota.last_data_us = esp_timer_get_time();
    return 0;
}

/**
 * The phone left. The update waits for it until BLEP_OTA_TIMEOUT_S have passed.
 */
void ble_ota_disconnected(void)
{
    if (!session_active())
        return;
    ota.last_data_us = esp_timer_get_time();
    ota.disconnected = true;
    wake_task();
}

/**
 * Bluetooth is going off, nothing of the update is left
 */
void ble_ota_stop(void)
{
    if (!ota.task)
        return;
    ota.failure = BLEP_OTA_NO_ERROR;
    ota.abort_requested = true;
    wake_task();
    // the task cleans up and deletes itself
    for (int i = 0; i < 50 && ota.task; i++)
        vTaskDelay(pdMS_TO_TICKS(100));
}

#endif /* ESP_S3 */
