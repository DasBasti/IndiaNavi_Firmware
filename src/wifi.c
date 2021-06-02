/*
 * WiFi Task managing WiFi connection
 *
 * running if WiFi connection is available 
 * else wait for wifi available
 *
 *  Created on: Juin 1, 2021
 *      Author: Bastian Neumann
 */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#include "gui.h"
#include "tasks.h"
#include "icons_32/icons_32.h"
char wifi_file[32 + 1 + 64];
wifi_config_t wifi_config;
static int s_retry_num = 0;
static int s_max_retry_num = 1;

static const char *TAG = "WIFI";
async_file_t AFILE;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

void readCreds(char *c, uint8_t *ssid, uint8_t *psk)
{
    while (c)
    {
        if (*c == '\n')
            break;
        if (*c == '\r')
            continue;
        *ssid = *c;
        c++;
        ssid++;
    }
    c++;
    while (c)
    {
        if (*c == '\n')
            break;
        if (*c == '\r')
            continue;
        *psk = *c;
        c++;
        psk++;
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < s_max_retry_num)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void StartWiFiTask(void const *argument)
{
    ESP_LOGI(TAG, "Start");

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    async_file_t *creds = &AFILE;
    creds->filename = "//WIFI";
    creds->dest = wifi_file;
    creds->loaded = false;
    loadFile(creds);
    while (!creds->loaded)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    //wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    //wifi_config.sta.pmf_cfg.capable = true;
    //wifi_config.sta.pmf_cfg.required = false;

    readCreds(wifi_file, wifi_config.sta.ssid, wifi_config.sta.password);
    for (;;)
    {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &event_handler,
                                                            NULL,
                                                            &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &event_handler,
                                                            NULL,
                                                            &instance_got_ip));

        ESP_ERROR_CHECK(esp_wifi_init(&config));

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "wifi_init_sta finished.");
        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               portMAX_DELAY);

        /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
        if (bits & WIFI_CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                     wifi_config.sta.ssid, wifi_config.sta.password);
        }
        else if (bits & WIFI_FAIL_BIT)
        {
            ESP_LOGI(TAG, "Failed to connect to SSID:'%s', password:'%s'",
                     wifi_config.sta.ssid, wifi_config.sta.password);
        }
        else
        {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
        }

        /* The event will not be processed after unregister */
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
        vEventGroupDelete(s_wifi_event_group);

        for (;;)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}