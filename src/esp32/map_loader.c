/* Map Files downloader
 * Downloads missing Files if WiFi is available
*/
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <sys/socket.h>

#include "gui.h"
#include "helper.h"
#include "navi/map_loader.h"
#include "navi/safe_print.h"
#include "navi/sd.h"
#include "navi/wifi.h"

#define TRACK_FILE_SIZE 32768
#define WP_LINE_SIZE 256
#define RETRY_DELAY_MIN_MS 1000
#define RETRY_DELAY_MAX_MS (5 * 60 * 1000)

typedef struct tileset tileset_t;
struct tileset {
    uint8_t zoom;
    uint32_t folder_min;
    uint32_t folder_max;
    uint32_t file_min;
    uint32_t file_max;
    char* wp_line;
    char* baseurl;
    uint32_t* file_count;
    tileset_t* next;
};

static const char* TAG = "DL";
static async_file_t* downloadfile; // the .tmp file of the running download
static uint32_t download_bytes;
static bool download_failed;
label_t* download_status;
char* download_status_text = "Downloader active";
esp_err_t startDownloadFile(void* handler, const char* url, int* status, int64_t* content_length);

static esp_err_t _http_event_handler(esp_http_client_event_t* evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        // error pages are not stored
        if (download_failed || esp_http_client_get_status_code(evt->client) != 200)
            break;
        if (!downloadfile->file || !downloadfile->file->obj.fs) {
            if (PM_OK != openFileForWriting(downloadfile)) {
                download_failed = true;
                break;
            }
            ESP_LOGD(TAG, "Create File to download: %s", downloadfile->filename);
        }
        uint32_t bytes_written = 0;
        writeToFile(downloadfile, evt->data, evt->data_len, &bytes_written);
        ESP_LOGD(TAG, "Wrote to download: %d/%lu", evt->data_len, bytes_written);
        download_bytes += bytes_written;
        if (evt->data_len != bytes_written)
            download_failed = true;
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    case HTTP_EVENT_ON_STATUS_CODE:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_STATUS_CODE");
        break;
    case HTTP_EVENT_ON_HEADERS_COMPLETE:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADERS_COMPLETE");
        break;
    }
    return ESP_OK;
}

/*
 * Download one tile into {name}.tmp and rename it only when the server
 * answered 200 and all data is on the card, so error pages and partial
 * downloads never count as an existing tile.
 *
 * Returns ESP_FAIL if the request failed and should be retried.
 */
static esp_err_t downloadTile(const char* url, char* filename, char* tmp_filename)
{
    downloadfile->filename = tmp_filename;
    download_bytes = 0;
    download_failed = false;

    int status = 0;
    int64_t content_length = 0;
    esp_err_t err = startDownloadFile(_http_event_handler, url, &status, &content_length);
    bool open = downloadfile->file && downloadfile->file->obj.fs;
    if (open && closeFile(downloadfile) != PM_OK)
        download_failed = true;

    bool complete = err == ESP_OK && status == 200 && !download_failed && download_bytes > 0
        && (content_length < 0 || content_length == download_bytes);
    if (complete && renameFile(tmp_filename, filename) == PM_OK)
        return ESP_OK;

    if (open)
        deleteFile(downloadfile);
    if (err == ESP_OK && status != 200) {
        // the server does not have this tile, do not ask again
        ESP_LOGW(TAG, "%s: HTTP %d, tile skipped", url, status);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Download of %s failed: %s, %lu bytes", url, esp_err_to_name(err), download_bytes);
    return ESP_FAIL;
}

static void downloadMapTilesForZoomLevel(tileset_t* t, async_file_t* wp_file)
{
    uint32_t retry_delay = RETRY_DELAY_MIN_MS;
    size_t url_size = strlen(t->baseurl) + 40; // base+/zzz/xxxxxxxxxx/yyyyyyyyyy.raw
    char* url = RTOS_Malloc(url_size);
    char* tmp_filename = RTOS_Malloc(WP_LINE_SIZE);
    downloadfile = createPhysicalFile();
    if (!url || !tmp_filename || !downloadfile) {
        RTOS_Free(url);
        RTOS_Free(tmp_filename);
        closePhysicalFile(downloadfile);
        downloadfile = NULL;
        return;
    }
    ESP_LOGI(TAG, "Run zoom level:%d from %s", t->zoom, t->baseurl);
    for (uint32_t x = t->folder_min; x <= t->folder_max; x++) {
        for (uint32_t y = t->file_min; y <= t->file_max; y++) {
            save_snprintf(url, url_size, "%s/%u/%lu/%lu.raw", t->baseurl, t->zoom, x, y);
            save_snprintf(wp_file->filename, WP_LINE_SIZE, "//MAPS/%u/%lu/%lu.raw", t->zoom, x, y);
            save_snprintf(tmp_filename, WP_LINE_SIZE, "//MAPS/%u/%lu/%lu.tmp", t->zoom, x, y);
            if (fileExists(wp_file) != PM_OK) {
                // Get File because we can not find it on the SD card
                for (;;) {
                    while (!isConnected()) {
                        ESP_LOGI(TAG, "Wait for WiFi connection");
                        vTaskDelay(3000 / portTICK_PERIOD_MS);
                    }
                    ESP_LOGI(TAG, "Get %s -> '%s'", url, wp_file->filename);
                    if (downloadTile(url, wp_file->filename, tmp_filename) == ESP_OK)
                        break;
                    // back off while the server can not be reached
                    vTaskDelay(pdMS_TO_TICKS(retry_delay));
                    retry_delay *= 2;
                    if (retry_delay > RETRY_DELAY_MAX_MS)
                        retry_delay = RETRY_DELAY_MAX_MS;
                }
                retry_delay = RETRY_DELAY_MIN_MS;
            } else {
                ESP_LOGD(TAG, "File %s exists!", wp_file->filename);
            }
            vPortYield();
        }
    }
    closePhysicalFile(downloadfile);
    downloadfile = NULL;
    RTOS_Free(tmp_filename);
    RTOS_Free(url);
}

void maploader_screen_element(const display_t* dsp)
{
    download_status = label_create(download_status_text, &f8x8, 0, 100, 0, 0);
    label_shrink_to_text(download_status);
    download_status->box.left = dsp->size.width - download_status->box.width;
    add_to_render_pipeline(label_render, download_status, RL_GUI_ELEMENTS);
}

void StartMapDownloaderTask(void* pvParameter)
{
    async_file_t AFILE = { 0 };
    async_file_t* wp_file = &AFILE;
    tileset_t* base_tileset = NULL;
    char* baseurl = NULL;
    ESP_LOGI(TAG, "Checking Map files...");
    char* waypoint_file = RTOS_Malloc(TRACK_FILE_SIZE);
    char* wp_line = RTOS_Malloc(WP_LINE_SIZE);
    // Load TRACK file to get track parameters
    wp_file->filename = RTOS_Malloc(WP_LINE_SIZE);
    if (!waypoint_file || !wp_line || !wp_file->filename)
        goto fail_url;
    save_snprintf(wp_file->filename, WP_LINE_SIZE, "//TRACK");
    wp_file->dest = waypoint_file;
    wp_file->dest_size = TRACK_FILE_SIZE;
    wp_file->loaded = false;
    if (loadFile(wp_file) != PM_OK) {
        ESP_LOGE(TAG, "No TRACK file");
        goto fail_url;
    }
    ESP_LOGI(TAG, "Load track information queued.");
    /*
    esp_http_client_config_t client_config = {
        //.url is set in loop
        .url = "http://platinenmacher.tech/indianavi/",
        .method = HTTP_METHOD_GET,
        .is_async = false,
        //.event_handler = _http_event_handler,
        .timeout_ms = 3000,
        //.cert_pem = server_cert_pem_start,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client;
    client = esp_http_client_init(&client_config);

    // Wait for WiFi to be established
    while (1)
    {
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK)
            break;
        ESP_LOGI(TAG, "Waiting for WiFi Connection");
        vTaskDelay(1000);
    }
*/
    ESP_LOGI(TAG, "Loaded track information.");

    base_tileset = RTOS_Malloc(sizeof(tileset_t));
    tileset_t* tileset = base_tileset;
    char* f = waypoint_file;
    f = readline_n(f, wp_line, WP_LINE_SIZE);
    if (!f || !tileset) {
        ESP_LOGE(TAG, "No URL in TRACK. %s", waypoint_file);
        goto fail_url;
    }
    uint32_t length = strlen(wp_line);
    baseurl = RTOS_Malloc(length + 1);
    if (!baseurl)
        goto fail_url;
    memcpy(baseurl, wp_line, length + 1);

    tileset->baseurl = baseurl;
    tileset->wp_line = wp_line;
    gui_set_app_mode(APP_MODE_DOWNLOAD);
    while (1) {
        f = readline_n(f, wp_line, WP_LINE_SIZE);
        if (!f) {
            ESP_LOGE(TAG, "No Zoom found in TRACK");
            goto fail_url;
        }
        ESP_LOGI(TAG, "Zoomline: %s", wp_line);
        uint8_t zoom = atoi(wp_line);

        // loop for multiple zooms
        if (zoom == 0)
            break;

        tileset->zoom = zoom;

        f = readline_n(f, wp_line, WP_LINE_SIZE);
        if (!f) {
            ESP_LOGE(TAG, "No folder_min found in TRACK");
            goto fail_url;
        }
        tileset->folder_min = atoi(wp_line);

        f = readline_n(f, wp_line, WP_LINE_SIZE);
        if (!f) {
            ESP_LOGE(TAG, "No folder_max found in TRACK");
            goto fail_url;
        }
        tileset->folder_max = atoi(wp_line);

        f = readline_n(f, wp_line, WP_LINE_SIZE);
        if (!f) {
            ESP_LOGE(TAG, "No file_min found in TRACK");
            goto fail_url;
        }
        tileset->file_min = atoi(wp_line);

        f = readline_n(f, wp_line, WP_LINE_SIZE);
        if (!f) {
            ESP_LOGE(TAG, "No file_max found in TRACK");
            goto fail_url;
        }
        tileset->file_max = atoi(wp_line);

        ESP_LOGI(TAG, "Get Map for [%lu-%lu]/[%lu-%lu]", tileset->folder_min, tileset->folder_max, tileset->file_min, tileset->file_max);
        ESP_LOGI(TAG, "Download from: %s", baseurl);

        tileset->file_count = 0;

        ESP_LOGI(TAG, "Connected. Start downloading maps");
        downloadMapTilesForZoomLevel(tileset, wp_file);
    }
fail_url:
    RTOS_Free(baseurl);
    RTOS_Free(base_tileset);
    RTOS_Free(wp_file->filename);
    RTOS_Free(waypoint_file);
    RTOS_Free(wp_line);
    vTaskDelete(NULL);
}
