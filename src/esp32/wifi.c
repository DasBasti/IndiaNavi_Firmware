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
#include <bootloader_random.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <esp_timer.h>
#include <nvs.h>
#include <qrcodegen.h>

#include "gui.h"
#include "helper.h"
#include "tasks.h"
#include <icons_16.h>
#include <string.h>
char wifi_file[32 + 1 + 64];
wifi_config_t wifi_config;
static int s_retry_num = 0;
static int s_max_retry_num = 10;
static const char* TAG = "WIFI";
static async_file_t AFILE;

static esp_netif_t* wifi_netif = 0;
static esp_netif_t* ap_netif = 0;

/* Access point of the device, so the app can connect without another WiFi */
#define AP_SSID_PREFIX "IndiaNavi-"
#define AP_PASSWORD_LEN 12
#define AP_MAX_CONNECTIONS 2
#define AP_NVS_NAMESPACE "wifi_ap"
#define AP_NVS_KEY "psk"
static char ap_ssid[sizeof(AP_SSID_PREFIX) + 4];
static char ap_password[AP_PASSWORD_LEN + 1];
static bool sta_enabled = false; // join the WiFi from the WIFI file as well
static volatile bool ap_running = false;
static volatile uint8_t ap_station_count = 0;

/* WiFi is switched off when nobody uses it, it is not needed while out and about.
 * It stays on while charging. */
#define WIFI_IDLE_TIMEOUT_US (10LL * 60 * 1000 * 1000)
static volatile int64_t last_activity_us;

/* "WIFI:T:WPA;S:IndiaNavi-XXXX;P:<12 chars>;;" fits into version 3, allow up to 4 */
#define AP_QR_MAX_VERSION 4
static uint8_t ap_qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(AP_QR_MAX_VERSION)];
static bool ap_qrcode_valid = false;

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
void StartWiFiTask(void const* argument);

static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(TAG, "station " MACSTR " joined the access point", MAC2STR(event->mac));
        ap_station_count++;
        wifi_notify_activity();
        ble_if_wifi_status_changed();
        trigger_rendering(); // remove the QR code
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
        ESP_LOGI(TAG, "station " MACSTR " left the access point", MAC2STR(event->mac));
        if (ap_station_count)
            ap_station_count--;
        // the idle timeout starts again when the last phone left
        wifi_notify_activity();
        ble_if_wifi_status_changed();
        if (!ap_station_count)
            trigger_rendering(); // show the QR code again
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (sta_enabled)
            esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!sta_enabled) {
            // nothing to do, only the access point is used
        } else if (s_retry_num < s_max_retry_num) {
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
 * status   HTTP status code of the response
 * content_length  Content-Length of the response, -1 for chunked responses
 *
 * @return
 *  - ESP_OK on successful
 *  - ESP_FAIL on error
 */
esp_err_t startDownloadFile(void* handler, const char* url, int* status, int64_t* content_length)
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
    if (!client)
        return ESP_FAIL;
    esp_err_t err = esp_http_client_perform(client);
    *status = esp_http_client_get_status_code(client);
    *content_length = esp_http_client_is_chunked_response(client) ? -1 : esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld", *status, *content_length);
    esp_http_client_cleanup(client);
    return err;
}

/*
 * Characters that are easy to read and type on a phone, no 0/O, 1/l/I
 */
static const char ap_password_chars[] = "abcdefghjkmnpqrstuvwxyz23456789";

static void generate_ap_password(void)
{
    uint8_t random[AP_PASSWORD_LEN];
    // RF is off at this point, use the SAR ADC as entropy source
    bootloader_random_enable();
    esp_fill_random(random, sizeof(random));
    bootloader_random_disable();
    for (size_t i = 0; i < AP_PASSWORD_LEN; i++)
        ap_password[i] = ap_password_chars[random[i] % (sizeof(ap_password_chars) - 1)];
    ap_password[AP_PASSWORD_LEN] = 0;
}

/**
 * Load the access point credentials, create them on first use.
 *
 * The password is stored in NVS so the phone can remember the network.
 * Has to be called once after NVS init and before any task uses WiFi or the ADC.
 */
void wifi_ap_credentials_init(void)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ap_ssid, sizeof(ap_ssid), AP_SSID_PREFIX "%02X%02X", mac[4], mac[5]);

    nvs_handle_t nvs;
    if (nvs_open(AP_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "NVS not available, access point password changes on every boot");
        generate_ap_password();
        return;
    }
    size_t len = sizeof(ap_password);
    if (nvs_get_str(nvs, AP_NVS_KEY, ap_password, &len) != ESP_OK || strlen(ap_password) != AP_PASSWORD_LEN) {
        ESP_LOGI(TAG, "create access point password");
        generate_ap_password();
        if (nvs_set_str(nvs, AP_NVS_KEY, ap_password) != ESP_OK || nvs_commit(nvs) != ESP_OK)
            ESP_LOGE(TAG, "can not store access point password");
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "access point: %s", ap_ssid);
}

const char* wifi_ap_ssid(void)
{
    return ap_ssid;
}

const char* wifi_ap_password(void)
{
    return ap_password;
}

bool wifi_ap_running(void)
{
    return ap_running;
}

/**
 * Number of phones connected to the access point
 */
uint8_t wifi_ap_station_count(void)
{
    return ap_station_count;
}

/**
 * Something uses WiFi (phone joined, HTTP request), restart the idle timeout
 */
void wifi_notify_activity(void)
{
    last_activity_us = esp_timer_get_time();
}

/**
 * QR code to join the access point, in the common WiFi format.
 * SSID and password only contain characters that need no escaping.
 *
 * returns NULL if the code can not be created
 */
const uint8_t* wifi_ap_qrcode(void)
{
    if (ap_qrcode_valid)
        return ap_qrcode;

    char text[64];
    uint8_t temp[sizeof(ap_qrcode)];
    snprintf(text, sizeof(text), "WIFI:T:WPA;S:%s;P:%s;;", ap_ssid, ap_password);
    ap_qrcode_valid = qrcodegen_encodeText(text, temp, ap_qrcode, qrcodegen_Ecc_LOW,
        qrcodegen_VERSION_MIN, AP_QR_MAX_VERSION, qrcodegen_Mask_AUTO, true);
    if (!ap_qrcode_valid)
        ESP_LOGE(TAG, "access point QR code not created");
    return ap_qrcode_valid ? ap_qrcode : NULL;
}

/*
 * Stop WiFi when no phone is connected and nothing happened for a while.
 * WiFi stays on while a charger is connected, there is enough power.
 */
static bool idle_timeout(void)
{
    if (is_charging || ap_station_count)
        return false;
    if (esp_timer_get_time() - last_activity_us < WIFI_IDLE_TIMEOUT_US)
        return false;
    ESP_LOGI(TAG, "nobody used WiFi for %lld minutes, switch it off", WIFI_IDLE_TIMEOUT_US / 60000000LL);
    s_stop_requested = true;
    return true;
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
    if (s_stop_requested || idle_timeout())
        return true;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms));
    return s_stop_requested || idle_timeout();
}

/*
 * Read the WiFi to join from the WIFI file: first line SSID, second line password
 *
 * returns true if a valid WiFi is configured
 */
static bool load_sta_config(char* ssid, size_t ssid_size, char* password, size_t password_size)
{
    async_file_t* creds = &AFILE;
    memset(creds, 0, sizeof(*creds));
    creds->filename = "//WIFI";
    creds->dest = wifi_file;
    creds->dest_size = sizeof(wifi_file);
    if (loadFile(creds) != PM_OK) {
        ESP_LOGI(TAG, "No WIFI file, only the access point is started");
        return false;
    }

    char* next = readline_n(wifi_file, ssid, ssid_size);
    readline_n(next, password, password_size);
    if (next == NULL)
        password[0] = 0;
    if (strlen(ssid) == 0 || strlen(ssid) > sizeof(wifi_config.sta.ssid) || strlen(password) >= sizeof(wifi_config.sta.password)) {
        ESP_LOGE(TAG, "SSID empty or SSID/password too long");
        return false;
    }
    return true;
}

void StartWiFiTask(void const* argument)
{
    esp_event_handler_instance_t instance_any_id = NULL;
    esp_event_handler_instance_t instance_got_ip = NULL;
    bool wifi_initialized = false;
    // one more byte than the field so a too long ssid/password is detected
    char ssid[sizeof(wifi_config.sta.ssid) + 1] = { 0 };
    char password[sizeof(wifi_config.sta.password) + 1] = { 0 };

    ESP_LOGI(TAG, "Start");
    waitForSDInit();
    ESP_LOGI(TAG, "Load credentials");
    sta_enabled = load_sta_config(ssid, sizeof(ssid), password, sizeof(password));

    if (!ap_ssid[0])
        wifi_ap_credentials_init();

    if (!wifi_netif)
        wifi_netif = esp_netif_create_default_wifi_sta();
    if (!ap_netif)
        ap_netif = esp_netif_create_default_wifi_ap();

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

    wifi_config_t ap_config = {
        .ap = {
            .channel = 1, // follows the channel of the joined WiFi in APSTA mode
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = AP_MAX_CONNECTIONS,
            .pmf_cfg = { .required = false },
        },
    };
    memcpy(ap_config.ap.ssid, ap_ssid, strlen(ap_ssid));
    ap_config.ap.ssid_len = strlen(ap_ssid);
    memcpy(ap_config.ap.password, ap_password, strlen(ap_password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(sta_enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    if (sta_enabled) {
        memset(&wifi_config, 0, sizeof(wifi_config));
        // ssid does not need to be \0 terminated if it has 32 characters
        memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
        memcpy(wifi_config.sta.password, password, strlen(password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "access point %s started, device is 192.168.4.1", ap_ssid);
    ap_station_count = 0;
    wifi_notify_activity(); // the idle timeout starts now
    ap_running = true;
    ble_if_wifi_status_changed();
    // the access point is up now, the app can connect directly
    start_mdns_service();
    upload_server_start();
    trigger_rendering(); // show the access point QR code

    if (!sta_enabled) {
        // only the access point, wait until WiFi is switched off
        while (!wait_or_stop(30000)) {
        }
        goto exit;
    }

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    while (!s_stop_requested && !idle_timeout()) {
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
            // announce the hostname in the joined WiFi as well
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
        }
        ESP_LOGI(TAG, "Reconnect....");
    }

exit:
    ESP_LOGI(TAG, "Stop");
    ap_running = false;
    ap_station_count = 0;
    ble_if_wifi_status_changed();
    upload_server_stop();
    mdns_free();
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
    sta_enabled = false;
    wifiTask_h = NULL;
    trigger_rendering(); // remove the access point QR code
    vTaskDelete(NULL);
}
