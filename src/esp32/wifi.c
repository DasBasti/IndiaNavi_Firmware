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
#include "helper.h"
#include "tasks.h"
#include <icons_32.h>
#include <string.h>
char wifi_file[32 + 1 + 64];
wifi_config_t wifi_config;
static int s_retry_num = 0;
static int s_max_retry_num = 10;
static const char* TAG = "WIFI";
static async_file_t AFILE;

static esp_netif_t* wifi_netif = 0;

static volatile bool _is_connected = false;
static volatile bool s_stop_requested = false;

#define WIFI_TASK_STACK_SIZE (1024 * 8)

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
void StartWiFiTask(void const* argument);

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
        } else if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        _is_connected = false;
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        _is_connected = true;
        if (s_wifi_event_group)
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void start_mdns_service()
{
    mdns_free(); // allow restarting the service after a reconnect
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
    if (ip && ip->addr)
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

/**
 * Start the WiFi task if it is not running
 */
void wifi_start_task(void)
{
    if (wifiTask_h)
        return;
    s_stop_requested = false;
    if (xTaskCreate((TaskFunction_t)&StartWiFiTask, "wifi", WIFI_TASK_STACK_SIZE, NULL, 8, &wifiTask_h) != pdPASS) {
        ESP_LOGE(TAG, "Can not create WiFi task");
        wifiTask_h = NULL;
    }
}

/**
 * Ask the WiFi task to shut down WiFi and delete itself.
 *
 * The task is never deleted from outside, it could hold the SD mutex
 * or HTTP resources at that moment.
 */
void wifi_request_stop(void)
{
    TaskHandle_t task = wifiTask_h;
    if (!task)
        return;
    s_stop_requested = true;
    xTaskNotifyGive(task);
}

/**
 * Wait for ms or until a stop is requested.
 *
 * @return true if the task should stop
 */
static bool wait_or_stop(uint32_t ms)
{
    if (s_stop_requested)
        return true;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms));
    return s_stop_requested;
}

void StartWiFiTask(void const* argument)
{
    esp_event_handler_instance_t instance_any_id = NULL;
    esp_event_handler_instance_t instance_got_ip = NULL;
    bool wifi_initialized = false;

    ESP_LOGI(TAG, "Start");
    waitForSDInit();
    ESP_LOGI(TAG, "Load credentials");
    async_file_t* creds = &AFILE;
    memset(creds, 0, sizeof(*creds));
    creds->filename = "//WIFI";
    creds->dest = wifi_file;
    creds->dest_size = sizeof(wifi_file);
    if (loadFile(creds) != PM_OK) {
        ESP_LOGE(TAG, "No WiFi credentials found");
        goto exit;
    }

    if (!wifi_netif)
        wifi_netif = esp_netif_create_default_wifi_sta();

    memset(&wifi_config, 0, sizeof(wifi_config));
    // one more byte than the field so a too long ssid/password is detected
    char ssid[sizeof(wifi_config.sta.ssid) + 1];
    char password[sizeof(wifi_config.sta.password) + 1];
    char* next = readline_n(wifi_file, ssid, sizeof(ssid));
    readline_n(next, password, sizeof(password));
    if (next == NULL)
        password[0] = 0;
    if (strlen(ssid) > sizeof(wifi_config.sta.ssid) || strlen(password) >= sizeof(wifi_config.sta.password)) {
        ESP_LOGE(TAG, "SSID or password too long");
        goto exit;
    }
    // ssid does not need to be \0 terminated if it has 32 characters
    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_config.sta.password, password, strlen(password));

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group)
        goto exit;

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
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

    if (esp_wifi_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed");
        goto exit;
    }
    wifi_initialized = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    while (!s_stop_requested) {
        _is_connected = false;

        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
         * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above)
         */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(1000));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "connected to ap SSID:%s", ssid);
            start_mdns_service();
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect to SSID:'%s'", ssid);
            // try again later
            if (wait_or_stop(30000))
                break;
            s_retry_num = 0;
            esp_wifi_connect();
            continue;
        } else {
            // timeout, check for stop request
            continue;
        }

        while (!s_stop_requested && updateConnectionInfo() == PM_OK) {
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
            if (wait_or_stop(30000))
                break;

            do_background_ota(NULL);
        }
        mdns_free();
        ESP_LOGI(TAG, "Reconnect....");
    }

exit:
    ESP_LOGI(TAG, "Stop");
    _is_connected = false;
    if (wifi_initialized) {
        esp_wifi_stop();
        esp_wifi_deinit();
    }
    if (instance_got_ip)
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    if (instance_any_id)
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    wifi_indicator_image_data = WIFI_0;
    s_retry_num = 0;
    wifiTask_h = NULL;
    vTaskDelete(NULL);
}
