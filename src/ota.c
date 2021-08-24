/* OTA example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"

#include "nvs.h"
#include "nvs_flash.h"
#include <sys/socket.h>
#include "esp_wifi.h"

#include "tasks.h"
#include "gui.h"

static const char *TAG = "OTA";
#define OTA_URL_SIZE 1024
char ota_update_url[OTA_URL_SIZE] = {0};
#define FIRMWARE_UPGRADE_URL "http://laptop.local:8070/firmware.bin"

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
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
    }
    return ESP_OK;
}

void StartOTATask(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting OTA...");

    extern const char server_cert_pem_start[] asm("_binary_AmazonRootCA1_4_pem_start");
    //extern const uint8_t server_cert_pem_end[] asm("_binary_AmazonRootCA1_4_pem_end");
    //ESP_ERROR_CHECK(esp_tls_init_global_ca_store());
    //ESP_ERROR_CHECK(esp_tls_set_global_ca_store(server_cert_pem_start, server_cert_pem_end - server_cert_pem_start));
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
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
