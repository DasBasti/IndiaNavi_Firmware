/*
 * WiFi Task managing WiFi connection
 *
 * running if WiFi connection is available
 * else wait for wifi available
 *
 *  Created on: Juni 1, 2021
 *      Author: Bastian Neumann
 */
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <lwip/dns.h>

#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <mdns.h>

#include "gui.h"
#include "tasks.h"
#include <icons_32.h>
char wifi_file[32 + 1 + 64];
wifi_config_t wifi_config;
static int s_retry_num = 0;
static int s_max_retry_num = 10;
static const char* TAG = "WIFI";
static async_file_t AFILE;

static esp_netif_t* wifi_netif = 0;

static bool _is_connected = false;

uint8_t* wifi_indicator_image_data = WIFI_0;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

wifi_ap_record_t sta_record;
void StartOTATask(void* pvParameter);

static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < s_max_retry_num) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        _is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void start_mdns_service()
{
    // initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    // set hostname
    mdns_hostname_set("indianavi");
    // set default instance
    mdns_instance_name_set("India Navi");
}

bool isConnected()
{
    return _is_connected;
}

static error_code_t updateConnectionInfo()
{
    if (esp_wifi_sta_get_ap_info(&sta_record) != ESP_OK) {
        ESP_LOGE(TAG, "No Station available!");
        esp_wifi_connect();
        vTaskDelay(100);
        return PM_FAIL;
    }

    const ip_addr_t* ip = dns_getserver(0);
    if (ip->addr)
        return PM_OK;

    return PM_FAIL;
}

/**
 * Downlaod from a URL using the given handler function
 *
 * @param
 * handler  The handler function for processing HTTP events.
 * url      The location to get the data from.
 *
 * @return
 *  - ESP_OK on successful
 *  - ESP_FAIL on error
 */
esp_err_t startDownloadFile(void* handler, const char* url)
{
    esp_http_client_config_t client_config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .disable_auto_redirect = true,
        .is_async = false,
        .event_handler = handler,
        .timeout_ms = 30000,
        //.cert_pem = server_cert_pem_start,
        .keep_alive_enable = true,
        .user_agent = "IndiaNavi 1.0",
    };
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    esp_err_t err = esp_http_client_perform(client);
    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %llu\n",
        esp_http_client_get_status_code(client),
        esp_http_client_get_content_length(client));
    esp_http_client_cleanup(client);
    return err;
}

void StartWiFiTask(void const* argument)
{
    ESP_LOGI(TAG, "Start");
    waitForSDInit();
    ESP_LOGI(TAG, "Load credentials");
    async_file_t* creds = &AFILE;
    creds->filename = "//WIFI";
    creds->dest = wifi_file;
    creds->loaded = false;
    ESP_ERROR_CHECK(loadFile(creds));

    if (!wifi_netif)
        wifi_netif = esp_netif_create_default_wifi_sta();

    char* next = readline(wifi_file, (char*)wifi_config.sta.ssid);
    readline(next, (char*)wifi_config.sta.password);

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
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    for (;;) {
        s_wifi_event_group = xEventGroupCreate();
        _is_connected = false;

        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
         * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above)
         */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

        /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
         * happened.
         */
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                wifi_config.sta.ssid, wifi_config.sta.password);
            start_mdns_service();
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect to SSID:'%s', password:'%s'",
                wifi_config.sta.ssid, wifi_config.sta.password);
            continue;
        } else {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
            continue;
        }

        /* The event will not be processed after unregister */
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
        vEventGroupDelete(s_wifi_event_group);

        while (updateConnectionInfo() == PM_OK) {
            static uint8_t last_rssi_state = 0;
            ESP_LOGI(TAG, "ssid is: %d", sta_record.rssi);
            if (sta_record.rssi >= -70 && last_rssi_state != 3) {
                wifi_indicator_image_data = WIFI_3;
                trigger_rendering();
                last_rssi_state = 3;
            } else if (sta_record.rssi < -70 && sta_record.rssi >= -80 && last_rssi_state != 2) {
                wifi_indicator_image_data = WIFI_2;
                trigger_rendering();
                last_rssi_state = 2;
            } else if (sta_record.rssi < -80 && last_rssi_state != 1) {
                wifi_indicator_image_data = WIFI_1;
                trigger_rendering();
                last_rssi_state = 1;
            }
            vTaskDelay(pdMS_TO_TICKS(30000));

            do_background_ota(NULL);
        }
        ESP_LOGI(TAG, "Reconnect....");
    }
}