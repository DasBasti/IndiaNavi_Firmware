/* Map Files downloader
 * Downloads missing Files if WiFi is available
*/
#include "esp_event.h"
#include "esp_https_ota.h"
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
#include "tasks.h"

#define TRACK_FILE_SIZE 32768
#define WP_LINE_SIZE 256

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
static async_file_t* downloadfile;
label_t* download_status;
char* download_status_text = "Downloader active";
esp_err_t startDownloadFile(void* handler, const char* url);

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
        const char* contentLength = "Content-Length";
        if (0 == strcmp(evt->header_key, contentLength)) {
            if (PM_OK != openFileForWriting(downloadfile))
                return ESP_FAIL;
            ESP_LOGD(TAG, "Create File to download: %s", downloadfile->filename);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (downloadfile && downloadfile->file != 0) {
            uint32_t bytes_written = 0;
            writeToFile(downloadfile, evt->data, evt->data_len, &bytes_written);
            ESP_LOGD(TAG, "Wrote to download: %d/%lu", evt->data_len, bytes_written);
            if (evt->data_len != bytes_written)
                return ESP_FAIL;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        closeFile(downloadfile);
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

static void downloadMapTilesForZoomLevel(tileset_t* t, async_file_t* wp_file)
{
    uint32_t fail_counter = 0;
    size_t url_size = strlen(t->baseurl) + 40; // base+/zzz/xxxxxxxxxx/yyyyyyyyyy.raw
    char* url = RTOS_Malloc(url_size);
    downloadfile = createPhysicalFile();
    if (!url || !downloadfile) {
        RTOS_Free(url);
        closePhysicalFile(downloadfile);
        downloadfile = NULL;
        return;
    }
    ESP_LOGI(TAG, "Run zoom level:%d from %s", t->zoom, t->baseurl);
    for (uint32_t x = t->folder_min; x <= t->folder_max; x++) {
        for (uint32_t y = t->file_min; y <= t->file_max; y++) {
            save_snprintf(url, url_size, "%s/%u/%lu/%lu.raw", t->baseurl, t->zoom, x, y);
            save_snprintf(wp_file->filename, WP_LINE_SIZE, "//MAPS/%u/%lu/%lu.raw", t->zoom, x, y);
            if (fileExists(wp_file) != PM_OK) {
                // Get File because we can not find it on the SD card
                downloadfile->filename = wp_file->filename;
                esp_err_t err;
                do {
                    while (!isConnected()) {
                        ESP_LOGI(TAG, "Wait for WiFi connection");
                        vTaskDelay(3000 / portTICK_PERIOD_MS);
                    }
                    ESP_LOGI(TAG, "Get %s -> '%s'", url, wp_file->filename);
                    err = startDownloadFile(_http_event_handler, url);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
                        if (++fail_counter == 10) {
                            fail_counter = 0;
                            esp_restart();
                        }
                        vTaskDelay(1000 / portTICK_PERIOD_MS);
                    }
                } while (err != ESP_OK);
            } else {
                ESP_LOGD(TAG, "File %s exists!", wp_file->filename);
            }
            vPortYield();
        }
    }
    closePhysicalFile(downloadfile);
    downloadfile = NULL;
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
#if 0

    esp_http_client_config_t config = {
        .url = FIRMWARE_UPGRADE_URL,
        .event_handler = _http_event_handler,
        .cert_pem = server_cert_pem_start,
        //.keep_alive_enable = true,
    };

    async_file_t AFILE;
    async_file_t *ota = &AFILE;
    ota->filename = "//OTA";
    ota->dest = ota_update_url;
    ota->loaded = false;
    loadFile(ota);
    uint8_t timeout = 0;
    while (!ota->loaded)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if (timeout++ > 100)
        {
            ESP_LOGI(TAG, "timeout loading OTA url");
            break;
        }
    }
    // Blink Housekeeping LED frequency
    extern uint32_t ledDelay;
    ledDelay = 200;
    esp_err_t ret = ESP_FAIL;
    if (ota_update_url[0] != 0)
    {
        char *url = RTOS_Malloc(sizeof(ota_update_url));
        readline(ota_update_url, url);
        config.url = url;
        ESP_LOGI(TAG, "Download from: %s", config.url);
        ret = esp_https_ota(&config);
        if (ret != ESP_OK)
        {
            config.url = FIRMWARE_UPGRADE_URL;
            ESP_LOGI(TAG, "Download from internal url: %s", config.url);
            ret = esp_https_ota(&config);
        }
    }
    if (ret == ESP_OK)
    {
        esp_restart();
    }
    else
    {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        /*
         * TODO:
         * 
         * Restart GUI Task
         * Update info bar
         * redraw
         */
    }

#endif
fail_url:
    RTOS_Free(baseurl);
    RTOS_Free(base_tileset);
    RTOS_Free(wp_file->filename);
    RTOS_Free(waypoint_file);
    RTOS_Free(wp_line);
    vTaskDelete(NULL);
}
