/* OTA example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

#include "helper.h"

#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <sys/socket.h>

#include "gui.h"
#include "tasks.h"

static const char* TAG = "OTA";
#define FIRMWARE_UPGRADE_URL "http://laptop.local:8070/firmware.bin"
extern const uint8_t server_cert_pem_start[] asm("_binary_AmazonRootCA1_4_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_AmazonRootCA1_4_pem_end");

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
    }
    return ESP_OK;
}

error_code_t do_background_ota(void* pvParameter)
{
    ESP_LOGI(TAG, "Starting OTA...");

    ESP_ERROR_CHECK(esp_tls_init_global_ca_store());
    ESP_ERROR_CHECK(esp_tls_set_global_ca_store((const unsigned char*)server_cert_pem_start, server_cert_pem_end - server_cert_pem_start));

    esp_http_client_config_t http_config = {
        .url = FIRMWARE_UPGRADE_URL,
        .cert_pem = (char*)server_cert_pem_start,
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
        .skip_cert_common_name_check = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };
    async_file_t AFILE = { 0 };
    async_file_t* ota = &AFILE;
    ota->filename = "//OTA";
    ota->loaded = false;
    loadFile(ota);
    uint8_t timeout = 0;
    while (!ota->loaded) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (timeout++ > 100) {
            ESP_LOGI(TAG, "timeout loading OTA url");
            return TIMEOUT;
        }
    }

    esp_err_t ret = ESP_FAIL;

    // Get length of first line in OAT file
    char* url = RTOS_Malloc(countline(ota->dest));
    readline(ota->dest, url);
    http_config.url = url;
    ESP_LOGI(TAG, "Download from: %s", http_config.url);
    ret = esp_https_ota(&ota_config);
    if (ret != ESP_OK) {
        http_config.url = FIRMWARE_UPGRADE_URL;
        ESP_LOGI(TAG, "Download from internal url: %s", http_config.url);
        ret = esp_https_ota(&ota_config);
    }

    if (ret == ESP_OK) {
        deleteFile(ota);
        ESP_LOGI(TAG, "Firmware upgrade succeded OTA file deleted, press reset to restart.");
        return PM_OK;
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed: %d", ret);
    }
    return PM_FAIL;
}
