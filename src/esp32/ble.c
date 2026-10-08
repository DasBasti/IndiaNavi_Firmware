/*
 * Bluetooth LE interface of the IndiaNavi
 *
 * The device is a peripheral with one GATT service that lets the app set the
 * time, exchange positions, switch the WiFi access point, change display
 * settings and update the firmware. The values are described in
 * IndiaNavi_App/docs/ble_api.md, their encoding is in lib/ble_protocol.
 *
 * Only one phone can be paired. It has to enter the passkey that is shown on
 * the display. As long as it is connected nobody else can connect.
 * The WiFi password is never sent, the phone reads it from the QR code.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "tasks.h"

#ifndef ESP_S3
/*
 * Only the ESP32-S3 board has Bluetooth LE enabled. The other boards build
 * without it, the functions do nothing.
 */
void ble_if_start(void) { }
void ble_if_stop(void) { }
bool ble_if_is_running(void) { return false; }
bool ble_if_is_connected(void) { return false; }
int32_t ble_if_passkey(void) { return -1; }
void ble_if_wifi_status_changed(void) { }
#else

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include "ble_internal.h"
#include "tasks.h"

void ble_store_config_init(void);

static const char* TAG = "BLE";

#define NO_CONNECTION BLE_HS_CONN_HANDLE_NONE
/* a phone that is not paired yet can pair for this long after Bluetooth started */
#define PAIRING_WINDOW_US (120LL * 1000 * 1000)
#define PASSKEY_SHOWN_US (120LL * 1000 * 1000)
#define PREFERRED_MTU 247
#define POSITION_INTERVAL_US (5LL * 1000 * 1000)
#define WORKER_STACK_SIZE 3072
#define MAX_WRITE_SIZE 256
#define MAX_VALUE_SIZE 96
#define ADV_INTERVAL_MIN_MS 500
#define ADV_INTERVAL_MAX_MS 600

static volatile bool running;
static volatile bool advertising;                /// the stack advertises, a phone can find the device
static uint8_t own_addr_type;
static volatile uint16_t conn_handle = NO_CONNECTION;
static volatile int32_t passkey = -1;            /// shown on the display while a phone pairs
static volatile int64_t pairing_until_us;        /// a new phone can pair until then
static volatile int64_t passkey_until_us;        /// the passkey stays on the display until then
static char device_name[24];

/* the phone subscribed to the notifications of these characteristics */
static volatile bool subscribed_position;
static volatile bool subscribed_wifi;
static volatile bool subscribed_ota;

static uint16_t position_out_handle;
static uint16_t wifi_status_handle;
static uint16_t ota_control_handle;

static TaskHandle_t worker_task;
static SemaphoreHandle_t worker_done;
static volatile bool worker_stop;
static volatile bool wifi_status_dirty;

#define DECLARE_UUID(name, id) static const ble_uuid128_t name = BLE_UUID128_INIT(BLEP_UUID128_BYTES(id))
DECLARE_UUID(uuid_service, BLEP_UUID_SERVICE);
DECLARE_UUID(uuid_info, BLEP_UUID_INFO);
DECLARE_UUID(uuid_time, BLEP_UUID_TIME);
DECLARE_UUID(uuid_position_in, BLEP_UUID_POSITION_IN);
DECLARE_UUID(uuid_position_out, BLEP_UUID_POSITION_OUT);
DECLARE_UUID(uuid_wifi_control, BLEP_UUID_WIFI_CONTROL);
DECLARE_UUID(uuid_wifi_status, BLEP_UUID_WIFI_STATUS);
DECLARE_UUID(uuid_settings, BLEP_UUID_SETTINGS);
DECLARE_UUID(uuid_ota_control, BLEP_UUID_OTA_CONTROL);
DECLARE_UUID(uuid_ota_data, BLEP_UUID_OTA_DATA);
DECLARE_UUID(uuid_device_control, BLEP_UUID_DEVICE_CONTROL);

static int gap_event(struct ble_gap_event* event, void* arg);

/* ---- pairing ---- */

/*
 * A new phone can pair while no phone is paired yet, in the first minutes
 * after Bluetooth started and after the app asked the device to forget its phone.
 * Everybody still has to enter the passkey from the display.
 */
static bool pairing_allowed(void)
{
    if (esp_timer_get_time() < pairing_until_us)
        return true;
    int bonds = 0;
    return ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &bonds) == 0 && bonds == 0;
}

static void open_pairing_window(void)
{
    pairing_until_us = esp_timer_get_time() + PAIRING_WINDOW_US;
}

/*
 * The e-ink display needs about 17 s for a new image, the phone has 30 s to pair. So the passkey is
 * chosen when the phone connects, before it asks to pair, and kept for another try when the phone
 * was too slow.
 */
static void show_passkey(void)
{
    passkey_until_us = esp_timer_get_time() + PASSKEY_SHOWN_US;
    if (passkey < 0) {
        passkey = (int32_t)(esp_random() % 1000000);
        trigger_rendering(); // show the code on the display
    }
}

static void clear_passkey(void)
{
    if (passkey >= 0) {
        passkey = -1;
        trigger_rendering(); // remove the box from the display
    }
}

/**
 * The code to enter on the phone, or -1 if no phone is pairing
 */
int32_t ble_if_passkey(void)
{
    return passkey;
}

/* ---- advertising and link ---- */

/* the icon on the display shows if the device can be found */
static void set_advertising(bool on)
{
    if (advertising != on) {
        advertising = on;
        trigger_rendering();
    }
}

static void advertise(void)
{
    set_advertising(false);
    if (!running)
        return;

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t*)&uuid_service;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising data failed: %d", rc);
        return;
    }

    // the name does not fit into the advertising data next to the UUID
    struct ble_hs_adv_fields response;
    memset(&response, 0, sizeof(response));
    response.name = (uint8_t*)device_name;
    response.name_len = strlen(device_name);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan response failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(ADV_INTERVAL_MIN_MS);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(ADV_INTERVAL_MAX_MS);
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "advertising failed: %d", rc);
        return;
    }
    set_advertising(true);
}

/**
 * Negotiated ATT MTU of the connection
 */
uint16_t ble_if_mtu(void)
{
    uint16_t handle = conn_handle;
    return handle == NO_CONNECTION ? 0 : ble_att_mtu(handle);
}

/**
 * Short connection interval for the firmware update, a slow one with latency otherwise
 */
void ble_if_request_fast_link(bool fast)
{
    uint16_t handle = conn_handle;
    if (handle == NO_CONNECTION)
        return;
    struct ble_gap_upd_params params = {
        .itvl_min = fast ? 12 : 24,  // units of 1.25 ms
        .itvl_max = fast ? 24 : 40,
        .latency = fast ? 0 : 2,
        .supervision_timeout = fast ? 400 : 500, // units of 10 ms
        .min_ce_len = 0,
        .max_ce_len = 0,
    };
    int rc = ble_gap_update_params(handle, &params);
    if (rc != 0)
        ESP_LOGW(TAG, "connection update failed: %d", rc);
}

static void notify(uint16_t value_handle, const void* data, size_t len)
{
    uint16_t handle = conn_handle;
    if (handle == NO_CONNECTION)
        return;
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (!om)
        return;
    // the mbuf belongs to the stack from here on
    int rc = ble_gatts_notify_custom(handle, value_handle, om);
    if (rc != 0)
        ESP_LOGD(TAG, "notification failed: %d", rc);
}

/**
 * Report of the firmware update, called from ble_ota.c
 */
void ble_if_notify_ota_status(const uint8_t status[BLEP_OTA_STATUS_SIZE])
{
    if (subscribed_ota)
        notify(ota_control_handle, status, BLEP_OTA_STATUS_SIZE);
}

/* ---- values ---- */

static size_t info_value(uint8_t* out, size_t size)
{
    uint8_t flags = BLEP_INFO_FLAG_OTA | BLEP_INFO_FLAG_TRACK_COLOR;
    if (is_charging)
        flags |= BLEP_INFO_FLAG_CHARGING;
    int32_t battery = current_battery_level;
    if (battery < 0)
        battery = 0;
    return blep_info_encode(out, size, flags, (uint8_t)(battery > 100 ? 100 : battery), GIT_HASH);
}

static size_t position_value(uint8_t* out)
{
    blep_position_out_t position;
    memset(&position, 0, sizeof(position));
    const map_position_t* current = map_position;
    if (current && current->fix != BLEP_FIX_INVALID) {
        float hdop = current->hdop * 10.0f;
        position.latitude_e7 = blep_degrees_to_e7(current->latitude);
        position.longitude_e7 = blep_degrees_to_e7(current->longitude);
        position.altitude_m = blep_meters_to_i16(current->altitude);
        position.hdop_x10 = hdop > 0.0f ? (uint16_t)(hdop > 65535.0f ? 65535.0f : hdop) : 0;
        position.fix = current->fix;
        position.satellites_in_use = current->satellites_in_use;
        position.satellites_in_view = current->satellites_in_view;
    }
    blep_position_out_encode(out, &position);
    return BLEP_POSITION_OUT_SIZE;
}

static size_t wifi_status_value(uint8_t* out, size_t size)
{
    // only the name of the network, the password is on the QR code of the display
    return blep_wifi_status_encode(out, size, wifi_ap_running(), wifi_ap_station_count(), wifi_ap_ssid());
}

/**
 * The WiFi access point started, stopped or a phone joined or left. Called from the WiFi task.
 */
void ble_if_wifi_status_changed(void)
{
    wifi_status_dirty = true;
    if (worker_task)
        xTaskNotifyGive(worker_task);
}

static int att_error(blep_err_t err)
{
    return err == BLEP_ERR_LENGTH ? BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN : BLEP_ATT_ERR_VALUE_NOT_ALLOWED;
}

static void request_wifi(bool on)
{
    uint32_t event = on ? TASK_EVENT_ENABLE_WIFI : TASK_EVENT_DISABLE_WIFI;
    if (on == wifi_ap_running())
        return;
    // the main task starts and stops the WiFi task
    xQueueSend(eventQueueHandle, &event, 0);
    wifi_notify_activity();
}

static int write_value(uint8_t id, const uint8_t* data, size_t len)
{
    switch (id) {
    case BLEP_UUID_TIME: {
        int64_t epoch;
        blep_err_t err = blep_time_decode(data, len, &epoch);
        if (err != BLEP_OK)
            return att_error(err);
        gps_set_time_from_phone(epoch);
        return 0;
    }
    case BLEP_UUID_POSITION_IN: {
        blep_position_in_t position;
        blep_err_t err = blep_position_in_decode(data, len, &position);
        if (err != BLEP_OK)
            return att_error(err);
        gps_set_position_from_phone(&position);
        return 0;
    }
    case BLEP_UUID_WIFI_CONTROL: {
        uint8_t on;
        blep_err_t err = blep_wifi_control_decode(data, len, &on);
        if (err != BLEP_OK)
            return att_error(err);
        request_wifi(on == BLEP_WIFI_ON);
        return 0;
    }
    case BLEP_UUID_SETTINGS: {
        blep_settings_t settings;
        blep_err_t err = blep_settings_decode(data, len, &settings);
        if (err != BLEP_OK)
            return att_error(err);
        return display_settings_set(&settings) ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    case BLEP_UUID_OTA_CONTROL:
        return ble_ota_control_write(data, len);
    case BLEP_UUID_OTA_DATA:
        return ble_ota_data_write(data, len);
    case BLEP_UUID_DEVICE_CONTROL: {
        uint8_t command;
        blep_err_t err = blep_device_control_decode(data, len, &command);
        if (err != BLEP_OK)
            return att_error(err);
        // forget the paired phone, the next one can pair
        ble_store_clear();
        open_pairing_window();
        ESP_LOGI(TAG, "paired phone forgotten");
        if (conn_handle != NO_CONNECTION)
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int read_value(uint8_t id, uint8_t* out, size_t size, size_t* len)
{
    switch (id) {
    case BLEP_UUID_INFO:
        *len = info_value(out, size);
        break;
    case BLEP_UUID_TIME:
        blep_time_encode(out, (int64_t)time(NULL));
        *len = BLEP_TIME_SIZE;
        break;
    case BLEP_UUID_POSITION_OUT:
        *len = position_value(out);
        break;
    case BLEP_UUID_WIFI_STATUS:
        *len = wifi_status_value(out, size);
        break;
    case BLEP_UUID_SETTINGS: {
        blep_settings_t settings = display_settings_get();
        blep_settings_encode(out, &settings);
        *len = BLEP_SETTINGS_SIZE;
        break;
    }
    case BLEP_UUID_OTA_CONTROL:
        ble_ota_status_read(out);
        *len = BLEP_OTA_STATUS_SIZE;
        break;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
    return *len ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_cb(uint16_t conn, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    // the callbacks all run in the host task, the buffers are not shared
    static uint8_t rx[MAX_WRITE_SIZE];
    static uint8_t value[MAX_VALUE_SIZE];
    (void)conn;
    (void)attr_handle;
    uint8_t id = (uint8_t)(uintptr_t)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = 0;
        if (OS_MBUF_PKTLEN(ctxt->om) > sizeof(rx))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (ble_hs_mbuf_to_flat(ctxt->om, rx, sizeof(rx), &len) != 0)
            return BLE_ATT_ERR_UNLIKELY;
        return write_value(id, rx, len);
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        size_t len = 0;
        int rc = read_value(id, value, sizeof(value), &len);
        if (rc != 0)
            return rc;
        return os_mbuf_append(ctxt->om, value, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* every characteristic needs an encrypted link with a passkey */
#define READ_FLAGS (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN)
#define WRITE_FLAGS (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)
#define WRITE_NO_RSP_FLAGS (BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)

static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_service.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &uuid_info.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_INFO,
                .flags = READ_FLAGS },
            { .uuid = &uuid_time.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_TIME,
                .flags = READ_FLAGS | WRITE_FLAGS },
            { .uuid = &uuid_position_in.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_POSITION_IN,
                .flags = WRITE_FLAGS },
            { .uuid = &uuid_position_out.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_POSITION_OUT,
                .val_handle = &position_out_handle, .flags = READ_FLAGS | BLE_GATT_CHR_F_NOTIFY },
            { .uuid = &uuid_wifi_control.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_WIFI_CONTROL,
                .flags = WRITE_FLAGS },
            { .uuid = &uuid_wifi_status.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_WIFI_STATUS,
                .val_handle = &wifi_status_handle, .flags = READ_FLAGS | BLE_GATT_CHR_F_NOTIFY },
            { .uuid = &uuid_settings.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_SETTINGS,
                .flags = READ_FLAGS | WRITE_FLAGS },
            { .uuid = &uuid_ota_control.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_OTA_CONTROL,
                .val_handle = &ota_control_handle, .flags = READ_FLAGS | WRITE_FLAGS | BLE_GATT_CHR_F_NOTIFY },
            { .uuid = &uuid_ota_data.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_OTA_DATA,
                .flags = WRITE_NO_RSP_FLAGS },
            { .uuid = &uuid_device_control.u, .access_cb = access_cb, .arg = (void*)(uintptr_t)BLEP_UUID_DEVICE_CONTROL,
                .flags = WRITE_FLAGS },
            { 0 },
        },
    },
    { 0 },
};

/* ---- GAP events ---- */

/*
 * One phone only: the phone of this encrypted link is bonded, the keys of every other phone are removed.
 *
 * Called when the link is encrypted, the stack stored the keys of a new phone before. A pairing that
 * fails does not get here, so a stranger without the passkey can not remove the paired phone.
 */
static void forget_other_phones(uint16_t handle)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(handle, &desc) != 0)
        return;

    ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int count = 0;
    if (ble_store_util_bonded_peers(peers, &count, MYNEWT_VAL(BLE_STORE_MAX_BONDS)) != 0)
        return;
    for (int i = 0; i < count; i++) {
        if (ble_addr_cmp(&peers[i], &desc.peer_id_addr) != 0) {
            ESP_LOGI(TAG, "new phone paired, the old one is forgotten");
            ble_store_util_delete_peer(&peers[i]);
        }
    }
}

static void clear_subscriptions(void)
{
    subscribed_position = false;
    subscribed_wifi = false;
    subscribed_ota = false;
}

static int gap_event(struct ble_gap_event* event, void* arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGW(TAG, "connection failed: %d", event->connect.status);
            advertise();
            break;
        }
        conn_handle = event->connect.conn_handle;
        advertising = false; // the stack stops advertising for the connection
        clear_subscriptions();
        ESP_LOGI(TAG, "phone connected");
        if (pairing_allowed())
            show_passkey(); // it might want to pair
        trigger_rendering(); // the icon shows the connection
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "phone disconnected: %d", event->disconnect.reason);
        conn_handle = NO_CONNECTION;
        clear_subscriptions();
        ble_ota_disconnected();
        advertise();
        trigger_rendering(); // the icon shows the connection
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == position_out_handle)
            subscribed_position = event->subscribe.cur_notify;
        else if (event->subscribe.attr_handle == wifi_status_handle)
            subscribed_wifi = event->subscribe.cur_notify;
        else if (event->subscribe.attr_handle == ota_control_handle)
            subscribed_ota = event->subscribe.cur_notify;
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU is %u", event->mtu.value);
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        uint16_t handle = event->passkey.conn_handle;
        // phones that are already paired never get here, they use their key
        if (event->passkey.params.action != BLE_SM_IOACT_DISP || !pairing_allowed()) {
            ESP_LOGW(TAG, "pairing refused");
            ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        // the paired phone is only forgotten when the new one is bonded, see forget_other_phones()
        struct ble_sm_io io;
        memset(&io, 0, sizeof(io));
        io.action = BLE_SM_IOACT_DISP;
        show_passkey();
        io.passkey = (uint32_t)passkey;
        int rc = ble_sm_inject_io(handle, &io);
        if (rc != 0)
            ESP_LOGE(TAG, "passkey not accepted: %d", rc);
        break;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // the phone lost its key and pairs again
        if (!pairing_allowed())
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
            ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            ESP_LOGI(TAG, "link is encrypted");
            clear_passkey();
            forget_other_phones(event->enc_change.conn_handle);
            // longer packets for the firmware update. Not when the phone connects: a paired phone starts
            // the encryption at once and the controller does not answer the command until the host
            // answered the key request, the host waits for the command.
            ble_gap_set_data_len(event->enc_change.conn_handle, 251, 2120);
            pairing_until_us = 0; // a phone is paired now
        } else {
            ESP_LOGW(TAG, "encryption failed: %d", event->enc_change.status);
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        break;

    default:
        break;
    }
    return 0;
}

/* ---- worker: notifications that the stack does not trigger ---- */

static void worker(void* arg)
{
    int64_t last_position_us = 0;
    while (!worker_stop) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        if (worker_stop)
            break;

        if (wifi_status_dirty) {
            wifi_status_dirty = false;
            if (subscribed_wifi) {
                uint8_t value[MAX_VALUE_SIZE];
                size_t len = wifi_status_value(value, sizeof(value));
                if (len)
                    notify(wifi_status_handle, value, len);
            }
        }

        int64_t now = esp_timer_get_time();
        if (passkey >= 0 && conn_handle == NO_CONNECTION && now > passkey_until_us)
            clear_passkey(); // nobody tried again

        if (subscribed_position && now - last_position_us >= POSITION_INTERVAL_US) {
            last_position_us = now;
            if (gps_is_position_known()) {
                uint8_t value[BLEP_POSITION_OUT_SIZE];
                notify(position_out_handle, value, position_value(value));
            }
        }
    }
    worker_task = NULL;
    xSemaphoreGive(worker_done);
    vTaskDelete(NULL);
}

/* ---- start and stop ---- */

static void on_reset(int reason)
{
    // the link and the advertising are gone, on_sync() starts advertising again
    ESP_LOGW(TAG, "stack reset: %d", reason);
    conn_handle = NO_CONNECTION;
    clear_subscriptions();
    ble_ota_disconnected();
    set_advertising(false);
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        ESP_LOGE(TAG, "no Bluetooth address");
        return;
    }
    ESP_LOGI(TAG, "advertising as %s", device_name);
    advertise();
}

static void host_task(void* param)
{
    nimble_port_run(); // returns when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

/**
 * Bluetooth is active: the device advertises or a phone is connected
 */
bool ble_if_is_running(void)
{
    return running && (advertising || conn_handle != NO_CONNECTION);
}

bool ble_if_is_connected(void)
{
    return running && conn_handle != NO_CONNECTION;
}

/**
 * Start Bluetooth, does nothing if it runs. Called from the main task.
 */
void ble_if_start(void)
{
    if (running)
        return;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth init failed: %s", esp_err_to_name(err));
        return;
    }

    // same suffix as the name of the WiFi access point
    snprintf(device_name, sizeof(device_name), "%s", wifi_ap_ssid());
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(device_name);
    ble_att_set_preferred_mtu(PREFERRED_MTU);

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // the display shows the passkey, the phone types it. The keys are stored.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();

    int rc = ble_gatts_count_cfg(gatt_services);
    if (rc == 0)
        rc = ble_gatts_add_svcs(gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT service failed: %d", rc);
        nimble_port_deinit();
        return;
    }

    conn_handle = NO_CONNECTION;
    advertising = false;
    clear_subscriptions();
    passkey = -1;
    open_pairing_window();
    worker_stop = false;
    wifi_status_dirty = false;
    if (!worker_done)
        worker_done = xSemaphoreCreateBinary();
    if (xTaskCreate(worker, "ble_worker", WORKER_STACK_SIZE, NULL, 3, &worker_task) != pdPASS) {
        ESP_LOGE(TAG, "worker task failed");
        worker_task = NULL;
    }

    running = true;
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "started");
}

/**
 * Stop Bluetooth, a running firmware update is aborted. Called from the main task.
 */
void ble_if_stop(void)
{
    if (!running)
        return;
    running = false; // no more advertising
    set_advertising(false);

    ble_ota_stop();

    if (worker_task) {
        worker_stop = true;
        xTaskNotifyGive(worker_task);
        xSemaphoreTake(worker_done, pdMS_TO_TICKS(3000));
    }

    ble_gap_adv_stop();
    if (conn_handle != NO_CONNECTION) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        // let the stack send the disconnect before it goes away
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    int rc = nimble_port_stop();
    if (rc == 0)
        nimble_port_deinit();
    else
        ESP_LOGE(TAG, "stop failed: %d", rc);

    conn_handle = NO_CONNECTION;
    clear_subscriptions();
    clear_passkey();
    ESP_LOGI(TAG, "stopped");
}

#endif /* ESP_S3 */
