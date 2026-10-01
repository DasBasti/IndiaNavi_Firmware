/*
 * HTTP upload server for the IndiaNavi app
 *
 * Implements the WiFi upload API (version 1) of the app, see
 * IndiaNavi_App/docs/wifi_upload_api.md. The server only runs while WiFi
 * is connected, which is the case in charge mode.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <ff.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "gui.h"
#include "helper.h"
#include "tasks.h"

#define API_VERSION 1
#define UPLOAD_CHUNK_SIZE 4096
#define FATFS_PATH_LEN 64  // "//" + longest allowed path + '\0'
#define DEVICE_ID_LEN 33
#define CONFIG_FILE_SIZE 1024
#define LISTING_START_SIZE 1024
#define RECV_RETRIES 3
#define TRANSFER_BODY_LEN 128
#define TRANSFER_TIMEOUT_US (120LL * 1000 * 1000) // no upload for 2 minutes stops the transfer
#define PROGRESS_STEPS 10                         // redraw every 10%
#define PROGRESS_MIN_INTERVAL_US (20LL * 1000 * 1000) // an e-ink refresh takes ~16 s

static const char* TAG = "UPLOAD";

static httpd_handle_t server;

/* transfer session announced by the app, read by the GUI */
static upload_progress_t progress;
static portMUX_TYPE progress_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t transfer_timer;
static uint8_t progress_shown_step;
static int64_t progress_shown_time;

/**
 * Copy the transfer state for the GUI
 */
upload_progress_t upload_get_progress(void)
{
    upload_progress_t p;
    taskENTER_CRITICAL(&progress_lock);
    p = progress;
    taskEXIT_CRITICAL(&progress_lock);
    return p;
}

static void restart_transfer_timer(void)
{
    // esp_timer_restart() only works on running timers
    esp_timer_stop(transfer_timer);
    esp_timer_start_once(transfer_timer, TRANSFER_TIMEOUT_US);
}

static void show_progress(void)
{
    progress_shown_time = esp_timer_get_time();
    trigger_rendering();
}

static void set_transfer_state(upload_state_t state)
{
    taskENTER_CRITICAL(&progress_lock);
    progress.state = state;
    progress.result_shown = false;
    taskEXIT_CRITICAL(&progress_lock);
}

/**
 * The GUI committed a refresh that showed the result of the transfer.
 * The result is only shown in this one refresh.
 */
void upload_progress_result_shown(upload_state_t shown)
{
    taskENTER_CRITICAL(&progress_lock);
    if (progress.state == shown && (shown == UPLOAD_DONE || shown == UPLOAD_ABORTED))
        progress.result_shown = true;
    taskEXIT_CRITICAL(&progress_lock);
}

static void transfer_timeout_cb(void* arg)
{
    if (progress.state != UPLOAD_ACTIVE)
        return;
    ESP_LOGW("UPLOAD", "transfer timed out after %lu of %lu files",
        (unsigned long)progress.files_done, (unsigned long)progress.files_total);
    set_transfer_state(UPLOAD_ABORTED);
    show_progress();
}

/*
 * Count one stored file of the running transfer and redraw if needed
 */
static void transfer_file_stored(size_t bytes)
{
    if (progress.state != UPLOAD_ACTIVE)
        return;

    taskENTER_CRITICAL(&progress_lock);
    progress.files_done++;
    progress.bytes_done += bytes;
    bool complete = progress.files_done >= progress.files_total;
    if (complete) {
        progress.state = UPLOAD_DONE;
        progress.result_shown = false;
    }
    uint8_t step = progress.files_done * PROGRESS_STEPS / progress.files_total;
    taskEXIT_CRITICAL(&progress_lock);

    if (complete) {
        esp_timer_stop(transfer_timer);
        ESP_LOGI("UPLOAD", "transfer complete");
        show_progress();
        return;
    }

    restart_transfer_timer();
    if (step != progress_shown_step && esp_timer_get_time() - progress_shown_time >= PROGRESS_MIN_INTERVAL_US) {
        progress_shown_step = step;
        show_progress();
    }
}
static char device_id[DEVICE_ID_LEN] = "IndiaNavi";

/*
 * Error answer with a short plain text reason
 */
static esp_err_t send_error(httpd_req_t* req, const char* status, const char* reason)
{
    ESP_LOGW(TAG, "%s %s: %s", status, req->uri, reason);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, reason);
}

static esp_err_t send_no_content(httpd_req_t* req)
{
    httpd_resp_set_status(req, HTTPD_204);
    return httpd_resp_send(req, NULL, 0);
}

static bool is_digits(const char* s, size_t len, size_t max_len)
{
    if (len == 0 || len > max_len)
        return false;
    for (size_t i = 0; i < len; i++)
        if (!isdigit((unsigned char)s[i]))
            return false;
    return true;
}

/*
 * Only "track.gpx" and "MAPS/{zoom}/{x}/{y}.raw" may be written or deleted.
 * Numbers have to fit into 8.3 names.
 */
static bool is_file_path_allowed(const char* path)
{
    if (strcasecmp(path, "track.gpx") == 0)
        return true;

    if (strncasecmp(path, "MAPS/", 5) != 0)
        return false;

    const char* zoom = path + 5;
    const char* slash1 = strchr(zoom, '/');
    if (!slash1)
        return false;
    const char* x = slash1 + 1;
    const char* slash2 = strchr(x, '/');
    if (!slash2)
        return false;
    const char* y = slash2 + 1;
    const char* dot = strchr(y, '.');
    if (!dot)
        return false;

    return is_digits(zoom, slash1 - zoom, 2)
        && is_digits(x, slash2 - x, 8)
        && is_digits(y, dot - y, 8)
        && strcasecmp(dot, ".raw") == 0;
}

/*
 * Folders may only contain letters, digits and '_' in each part.
 * This rules out "..", empty parts and anything that needs escaping.
 */
static bool is_dir_path_allowed(const char* path)
{
    size_t part_len = 0;
    for (const char* c = path; *c; c++) {
        if (*c == '/') {
            if (part_len == 0 || part_len > 8)
                return false;
            part_len = 0;
        } else if (isalnum((unsigned char)*c) || *c == '_') {
            part_len++;
        } else {
            return false;
        }
    }
    return part_len <= 8;
}

/*
 * Copy the part of the URI after prefix into path, without query string.
 */
static bool get_path_from_uri(httpd_req_t* req, const char* prefix, char* path, size_t size)
{
    size_t prefix_len = strlen(prefix);
    if (strncmp(req->uri, prefix, prefix_len) != 0)
        return false;

    const char* start = req->uri + prefix_len;
    size_t len = strcspn(start, "?#");
    if (len >= size)
        return false;

    memcpy(path, start, len);
    path[len] = 0;
    return true;
}

/*
 * Create all parent folders of a FatFs path. Needs the SD lock.
 */
static void create_parent_folders(const char* fatfs_path)
{
    char folder[FATFS_PATH_LEN];
    // skip the leading "//" of the root
    for (const char* c = fatfs_path + 2; *c; c++) {
        if (*c != '/')
            continue;
        size_t len = c - fatfs_path;
        memcpy(folder, fatfs_path, len);
        folder[len] = 0;
        FRESULT res = f_mkdir(folder);
        if (res != FR_OK && res != FR_EXIST)
            ESP_LOGD(TAG, "mkdir %s: %d", folder, res);
    }
}

/*
 * Read the device id from <id> in config.xml
 */
static void load_device_id(void)
{
    char* config = RTOS_Malloc(CONFIG_FILE_SIZE);
    if (!config)
        return;

    async_file_t file = { 0 };
    file.filename = "//config.xml";
    file.dest = config;
    file.dest_size = CONFIG_FILE_SIZE;
    if (loadFile(&file) == PM_OK) {
        char* start = strstr(config, "<id>");
        char* end = start ? strstr(start, "</id>") : NULL;
        if (start && end) {
            start += strlen("<id>");
            size_t len = 0;
            // only keep characters that need no escaping in JSON
            for (char* c = start; c < end && len < DEVICE_ID_LEN - 1; c++)
                if (isalnum((unsigned char)*c) || *c == '-' || *c == '_' || *c == ' ')
                    device_id[len++] = *c;
            if (len)
                device_id[len] = 0;
        }
    }
    RTOS_Free(config);
    ESP_LOGI(TAG, "device id: %s", device_id);
}

/*
 * GET /api/info
 */
static esp_err_t api_info_handler(httpd_req_t* req)
{
    wifi_notify_activity(); // keeps WiFi on
    uint64_t total = 0, free_bytes = 0;
    bool present = sd_get_info(&total, &free_bytes) == PM_OK;

    char json[256];
    snprintf(json, sizeof(json),
        "{\"id\":\"%s\",\"firmware\":\"%s\",\"api\":%d,\"ota\":true,"
        "\"sd\":{\"present\":%s,\"free\":%llu,\"total\":%llu}}",
        device_id, GIT_HASH, API_VERSION, present ? "true" : "false",
        (unsigned long long)free_bytes, (unsigned long long)total);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/*
 * POST /api/reload
 */
/*
 * Find "key": <number> in a flat JSON object
 */
static bool json_get_uint(const char* json, const char* key, uint64_t* value)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p)
        return false;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p++ != ':')
        return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (!isdigit((unsigned char)*p))
        return false;
    char* end;
    *value = strtoull(p, &end, 10);
    return end != p;
}

static esp_err_t send_transfer_status(httpd_req_t* req)
{
    upload_progress_t p = upload_get_progress();
    static const char* state_names[] = { "idle", "active", "done", "aborted" };

    char json[160];
    snprintf(json, sizeof(json),
        "{\"state\":\"%s\",\"files\":%lu,\"files_done\":%lu,\"bytes\":%llu,\"bytes_done\":%llu}",
        state_names[p.state], (unsigned long)p.files_total, (unsigned long)p.files_done,
        (unsigned long long)p.bytes_total, (unsigned long long)p.bytes_done);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/*
 * POST /api/transfer  {"files": 1523, "bytes": 49905664}
 *
 * Announces the number of files the app is going to upload.
 */
static esp_err_t api_transfer_post_handler(httpd_req_t* req)
{
    wifi_notify_activity(); // keeps WiFi on
    char body[TRANSFER_BODY_LEN];
    if (req->content_len == 0 || req->content_len >= sizeof(body))
        return send_error(req, "400 Bad Request", "body missing or too long");

    size_t len = 0;
    while (len < req->content_len) {
        int received = httpd_req_recv(req, body + len, req->content_len - len);
        if (received <= 0)
            return ESP_FAIL; // connection broken
        len += received;
    }
    body[len] = 0;

    uint64_t files = 0, bytes = 0;
    if (!json_get_uint(body, "files", &files) || files == 0 || files > UINT32_MAX)
        return send_error(req, "400 Bad Request", "\"files\" missing or not a positive number");
    json_get_uint(body, "bytes", &bytes); // optional

    taskENTER_CRITICAL(&progress_lock);
    progress.state = UPLOAD_ACTIVE;
    progress.result_shown = false;
    progress.files_total = files;
    progress.files_done = 0;
    progress.bytes_total = bytes;
    progress.bytes_done = 0;
    taskEXIT_CRITICAL(&progress_lock);

    ESP_LOGI(TAG, "transfer of %lu files (%llu bytes) announced", (unsigned long)files, (unsigned long long)bytes);
    restart_transfer_timer();
    progress_shown_step = 0;
    show_progress();
    return send_transfer_status(req);
}

/*
 * GET /api/transfer
 */
static esp_err_t api_transfer_get_handler(httpd_req_t* req)
{
    wifi_notify_activity(); // keeps WiFi on
    return send_transfer_status(req);
}

/*
 * DELETE /api/transfer
 *
 * Cancels the transfer and removes the progress from the display.
 */
static esp_err_t api_transfer_delete_handler(httpd_req_t* req)
{
    wifi_notify_activity(); // keeps WiFi on
    esp_timer_stop(transfer_timer);
    bool shown = progress.state != UPLOAD_IDLE;
    set_transfer_state(UPLOAD_IDLE);
    if (shown)
        show_progress();
    return send_no_content(req);
}

static esp_err_t api_reload_handler(httpd_req_t* req)
{
    wifi_notify_activity(); // keeps WiFi on
    ESP_LOGI(TAG, "reload track");
    gui_reload_track();
    return send_no_content(req);
}

/*
 * Restart a moment after the answer of the request was sent
 */
static void restart_cb(void* arg)
{
    esp_restart();
}

static void restart_later(void)
{
    static esp_timer_handle_t timer;
    if (!timer) {
        const esp_timer_create_args_t args = { .callback = restart_cb, .name = "restart" };
        if (esp_timer_create(&args, &timer) != ESP_OK)
            return;
    }
    esp_timer_start_once(timer, 2000 * 1000);
}

/*
 * PUT /api/firmware
 * The body is the firmware image (the .bin of the app). It is written to the
 * inactive OTA partition and checked. The device boots it after POST /api/restart.
 */
static esp_err_t api_firmware_put_handler(httpd_req_t* req)
{
    wifi_notify_activity(); // keeps WiFi on
    size_t content_len = req->content_len;
    if (content_len == 0)
        return send_error(req, "411 Length Required", "Content-Length missing");

    const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
    if (!part)
        return send_error(req, "500 Internal Server Error", "no update partition");
    if (content_len > part->size)
        return send_error(req, "507 Insufficient Storage", "firmware is too big");

    char* buf = malloc(UPLOAD_CHUNK_SIZE);
    if (!buf)
        return send_error(req, "500 Internal Server Error", "out of memory");

    // erase each sector when it is written instead of the whole image at
    // once, so the flash is not blocked for seconds
    esp_ota_handle_t ota;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        free(buf);
        return send_error(req, "500 Internal Server Error", "can not start update");
    }
    ESP_LOGI(TAG, "firmware update: %u bytes to %s", (unsigned)content_len, part->label);

    size_t remaining = content_len;
    bool write_ok = true;
    uint8_t retries = 0;
    while (remaining > 0) {
        int received = httpd_req_recv(req, buf, remaining < UPLOAD_CHUNK_SIZE ? remaining : UPLOAD_CHUNK_SIZE);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && ++retries <= RECV_RETRIES)
            continue;
        if (received <= 0)
            break;
        retries = 0;
        wifi_notify_activity();
        if (esp_ota_write(ota, buf, received) != ESP_OK) {
            write_ok = false;
            break;
        }
        remaining -= received;
        // every flash write stalls the other core, let its idle task run
        // so the task watchdog does not trigger
        vTaskDelay(1);
    }
    free(buf);

    if (remaining > 0 || !write_ok) {
        esp_ota_abort(ota);
        if (write_ok)
            return ESP_FAIL; // connection is broken, close the socket
        return send_error(req, "500 Internal Server Error", "write failed");
    }

    // checks the image (magic byte, checksum, hash)
    if (esp_ota_end(ota) != ESP_OK)
        return send_error(req, "400 Bad Request", "not a valid firmware image");
    if (esp_ota_set_boot_partition(part) != ESP_OK)
        return send_error(req, "500 Internal Server Error", "can not select the new firmware");

    ESP_LOGI(TAG, "firmware stored, boots after restart");
    return send_no_content(req);
}

/*
 * POST /api/restart
 */
static esp_err_t api_restart_handler(httpd_req_t* req)
{
    wifi_notify_activity();
    ESP_LOGI(TAG, "restart requested");
    esp_err_t res = send_no_content(req);
    restart_later();
    return res;
}

/*
 * Append text to the growing listing buffer
 */
static bool listing_append(char** buf, size_t* size, size_t* len, const char* text)
{
    size_t text_len = strlen(text);
    if (*len + text_len + 1 > *size) {
        size_t new_size = *size * 2;
        while (*len + text_len + 1 > new_size)
            new_size *= 2;
        char* new_buf = realloc(*buf, new_size);
        if (!new_buf)
            return false;
        *buf = new_buf;
        *size = new_size;
    }
    memcpy(*buf + *len, text, text_len + 1);
    *len += text_len;
    return true;
}

/*
 * GET /sd/{dir}/
 */
static esp_err_t sd_get_handler(httpd_req_t* req)
{
    wifi_notify_activity(); // keeps WiFi on
    char path[FATFS_PATH_LEN - 2];
    if (!get_path_from_uri(req, "/sd/", path, sizeof(path)))
        return send_error(req, "400 Bad Request", "path too long");

    size_t path_len = strlen(path);
    if (path_len && path[path_len - 1] != '/')
        return send_error(req, "400 Bad Request", "only folders can be read, path has to end with /");
    if (path_len)
        path[--path_len] = 0; // remove trailing '/'
    if (!is_dir_path_allowed(path))
        return send_error(req, "400 Bad Request", "path not allowed");

    char fatfs_path[FATFS_PATH_LEN];
    snprintf(fatfs_path, sizeof(fatfs_path), "/%s", path);

    size_t size = LISTING_START_SIZE, len = 0;
    char* listing = malloc(size);
    if (!listing)
        return send_error(req, "500 Internal Server Error", "out of memory");
    listing[0] = 0;

    if (!sd_lock()) {
        free(listing);
        return send_error(req, "503 Service Unavailable", "no SD card or card busy");
    }

    FF_DIR dir;
    FRESULT res = f_opendir(&dir, fatfs_path);
    if (res != FR_OK) {
        sd_unlock();
        free(listing);
        if (res == FR_NO_PATH || res == FR_NO_FILE || res == FR_INVALID_NAME)
            return send_error(req, "404 Not Found", "folder does not exist");
        return send_error(req, "500 Internal Server Error", "can not open folder");
    }

    bool ok = listing_append(&listing, &size, &len, "[");
    bool first = true;
    FILINFO info;
    while (ok) {
        res = f_readdir(&dir, &info);
        if (res != FR_OK || info.fname[0] == 0)
            break;
        if (info.fattrib & (AM_HID | AM_SYS))
            continue;

        // 8.3 names are upper case on the card, the app uses lower case names
        for (char* c = info.fname; *c; c++)
            *c = tolower((unsigned char)*c);

        const char* ext = strrchr(info.fname, '.');
        if (ext && strcmp(ext, ".tmp") == 0)
            continue; // unfinished upload

        char entry[64];
        if (info.fattrib & AM_DIR)
            snprintf(entry, sizeof(entry), "%s{\"name\":\"%s\",\"dir\":true}", first ? "" : ",", info.fname);
        else
            snprintf(entry, sizeof(entry), "%s{\"name\":\"%s\",\"size\":%lu}", first ? "" : ",", info.fname, (unsigned long)info.fsize);
        ok = listing_append(&listing, &size, &len, entry);
        first = false;
    }
    f_closedir(&dir);
    sd_unlock();

    if (!ok || !listing_append(&listing, &size, &len, "]")) {
        free(listing);
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    if (res != FR_OK) {
        free(listing);
        return send_error(req, "500 Internal Server Error", "can not read folder");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, listing, len);
    free(listing);
    return err;
}

/*
 * PUT /sd/{path}
 *
 * The file is written to {name}.tmp and renamed after all bytes are on the card.
 */
static esp_err_t sd_put_handler(httpd_req_t* req)
{
    wifi_notify_activity(); // keeps WiFi on
    char path[FATFS_PATH_LEN - 2];
    if (!get_path_from_uri(req, "/sd/", path, sizeof(path)) || !is_file_path_allowed(path))
        return send_error(req, "400 Bad Request", "path not allowed");

    if (httpd_req_get_hdr_value_len(req, "Content-Length") == 0)
        return send_error(req, "411 Length Required", "Content-Length missing");

    size_t content_len = req->content_len;

    uint64_t total, free_bytes;
    if (sd_get_info(&total, &free_bytes) != PM_OK)
        return send_error(req, "503 Service Unavailable", "no SD card or card busy");
    // keep one cluster size of reserve for the temporary file
    if ((uint64_t)content_len + 32768 > free_bytes)
        return send_error(req, "507 Insufficient Storage", "not enough space on SD card");

    char final_path[FATFS_PATH_LEN];
    char tmp_path[FATFS_PATH_LEN];
    snprintf(final_path, sizeof(final_path), "//%s", path);
    snprintf(tmp_path, sizeof(tmp_path), "//%s", path);
    char* ext = strrchr(tmp_path, '.');
    strcpy(ext, ".tmp"); // same length as ".raw" and ".gpx"

    char* buf = malloc(UPLOAD_CHUNK_SIZE);
    FIL* file = calloc(1, sizeof(FIL));
    if (!buf || !file) {
        free(buf);
        free(file);
        return send_error(req, "500 Internal Server Error", "out of memory");
    }

    if (!sd_lock()) {
        free(buf);
        free(file);
        return send_error(req, "503 Service Unavailable", "no SD card or card busy");
    }
    create_parent_folders(tmp_path);
    FRESULT res = f_open(file, tmp_path, FA_WRITE | FA_CREATE_ALWAYS);
    sd_unlock();
    if (res != FR_OK) {
        ESP_LOGE(TAG, "open %s: %d", tmp_path, res);
        free(buf);
        free(file);
        return send_error(req, "500 Internal Server Error", "can not create file");
    }

    size_t remaining = content_len;
    bool write_ok = true;
    uint8_t retries = 0;
    while (remaining > 0) {
        int received = httpd_req_recv(req, buf, remaining < UPLOAD_CHUNK_SIZE ? remaining : UPLOAD_CHUNK_SIZE);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && ++retries <= RECV_RETRIES)
            continue;
        if (received <= 0)
            break; // connection closed or timeout
        retries = 0;

        UINT written = 0;
        if (!sd_lock()) {
            write_ok = false;
            break;
        }
        res = f_write(file, buf, received, &written);
        sd_unlock();
        if (res != FR_OK || written != (UINT)received) {
            ESP_LOGE(TAG, "write %s: %d", tmp_path, res);
            write_ok = false;
            break;
        }
        remaining -= received;
    }
    free(buf);

    bool complete = write_ok && remaining == 0;
    bool renamed = false;
    bool locked = sd_lock();
    if (locked) {
        res = f_close(file);
        if (complete && res == FR_OK) {
            // f_rename does not overwrite, remove the old file first
            f_unlink(final_path);
            res = f_rename(tmp_path, final_path);
            renamed = res == FR_OK;
            if (!renamed)
                ESP_LOGE(TAG, "rename %s: %d", tmp_path, res);
        }
        if (!renamed)
            f_unlink(tmp_path);
        sd_unlock();
    }
    free(file);

    if (renamed) {
        ESP_LOGI(TAG, "stored %s (%u bytes)", final_path, (unsigned)content_len);
        transfer_file_stored(content_len);
        return send_no_content(req);
    }
    if (!locked)
        return send_error(req, "503 Service Unavailable", "SD card removed");
    if (remaining > 0 && write_ok)
        return ESP_FAIL; // connection is broken, close the socket
    return send_error(req, "500 Internal Server Error", "write failed");
}

/*
 * DELETE /sd/{path}
 */
static esp_err_t sd_delete_handler(httpd_req_t* req)
{
    wifi_notify_activity(); // keeps WiFi on
    char path[FATFS_PATH_LEN - 2];
    if (!get_path_from_uri(req, "/sd/", path, sizeof(path)) || !is_file_path_allowed(path))
        return send_error(req, "400 Bad Request", "path not allowed");

    char fatfs_path[FATFS_PATH_LEN];
    snprintf(fatfs_path, sizeof(fatfs_path), "//%s", path);

    if (!sd_lock())
        return send_error(req, "503 Service Unavailable", "no SD card or card busy");
    FRESULT res = f_unlink(fatfs_path);
    sd_unlock();

    if (res == FR_OK)
        return send_no_content(req);
    if (res == FR_NO_FILE || res == FR_NO_PATH)
        return send_error(req, "404 Not Found", "file does not exist");
    return send_error(req, "500 Internal Server Error", "delete failed");
}

static const httpd_uri_t uri_handlers[] = {
    { .uri = "/api/info", .method = HTTP_GET, .handler = api_info_handler },
    { .uri = "/api/reload", .method = HTTP_POST, .handler = api_reload_handler },
    { .uri = "/api/transfer", .method = HTTP_POST, .handler = api_transfer_post_handler },
    { .uri = "/api/transfer", .method = HTTP_GET, .handler = api_transfer_get_handler },
    { .uri = "/api/transfer", .method = HTTP_DELETE, .handler = api_transfer_delete_handler },
    { .uri = "/api/firmware", .method = HTTP_PUT, .handler = api_firmware_put_handler },
    { .uri = "/api/restart", .method = HTTP_POST, .handler = api_restart_handler },
    { .uri = "/sd/*", .method = HTTP_GET, .handler = sd_get_handler },
    { .uri = "/sd/*", .method = HTTP_PUT, .handler = sd_put_handler },
    { .uri = "/sd/*", .method = HTTP_DELETE, .handler = sd_delete_handler },
};

/**
 * Start the upload server if it is not running
 */
void upload_server_start(void)
{
    if (server)
        return;

    load_device_id();

    if (!transfer_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = transfer_timeout_cb,
            .name = "upload",
        };
        if (esp_timer_create(&timer_args, &transfer_timer) != ESP_OK) {
            ESP_LOGE(TAG, "Can not create transfer timer");
            return;
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6 * 1024;
    // the app uses 2 connections, keep sockets for other users of LWIP
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.max_uri_handlers = sizeof(uri_handlers) / sizeof(uri_handlers[0]);
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Can not start server");
        server = NULL;
        return;
    }

    for (size_t i = 0; i < sizeof(uri_handlers) / sizeof(uri_handlers[0]); i++)
        httpd_register_uri_handler(server, &uri_handlers[i]);

    ESP_LOGI(TAG, "Server started on port %d", config.server_port);
}

/**
 * Stop the upload server. Waits for a running request to finish.
 */
void upload_server_stop(void)
{
    if (!server)
        return;
    httpd_stop(server);
    server = NULL;

    // WiFi is gone, a running transfer can not continue
    esp_timer_stop(transfer_timer);
    if (progress.state != UPLOAD_IDLE) {
        set_transfer_state(UPLOAD_IDLE);
        trigger_rendering();
    }
    ESP_LOGI(TAG, "Server stopped");
}
