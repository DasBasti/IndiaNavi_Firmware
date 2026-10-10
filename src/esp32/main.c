/*
 * Main Task for Wander Navi Application
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Platinenmacher.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <nvs.h>
#include <nvs_flash.h>

#include <lsm303.h>

#include "gui.h"
#include "navi/ble.h"
#include "navi/button.h"
#include "navi/display_settings.h"
#include "navi/gps.h"
#include "navi/power.h"
#include "navi/recorder.h"
#include "navi/sd.h"
#include "navi/system_events.h"
#include "navi/wifi.h"
#include "pins.h"

static const char* TAG = "MAIN";
#define HASH_LEN 32

SemaphoreHandle_t print_semaphore = NULL;

/* events for the main task, see system_events.h */
static QueueHandle_t event_queue = NULL;

uint32_t ledDelay = 100;

gpio_t* led;

/*
 * The CPU runs at 240 MHz while a task needs it and at 80 MHz otherwise. Below 80 MHz the APB clock is
 * lower than 80 MHz: Bluetooth needs it, and the GPS UART is clocked from it and receives without a lock.
 *
 * No light sleep: the GPS UART does not receive in it, Bluetooth blocks it without controller modem sleep
 * (CONFIG_BT_CTRL_MODEM_SLEEP) and all GPIOs are disabled in it (CONFIG_PM_SLP_DISABLE_GPIO), also the
 * supply pins of display, SD card and GPS.
 */
esp_pm_config_t pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 80,
    .light_sleep_enable = false,
};

esp_err_t light_sleep_cb(int64_t sleep_time_us, void* arg)
{
    assert(led);
    assert((gpio_value_t)arg == GPIO_RESET || (gpio_value_t)arg == GPIO_SET);
    gpio_value_t level = (gpio_value_t)arg;
    gpio_write(led, level);
    return ESP_OK;
}

esp_pm_sleep_cbs_register_config_t esp_pm_config = {
    .enter_cb = light_sleep_cb,
    .exit_cb = light_sleep_cb,
    .enter_cb_user_arg = (void*)GPIO_RESET,
    .exit_cb_user_arg = (void*)GPIO_SET,
};

bool system_post_event(task_events_e event, TickType_t wait)
{
    uint32_t e = event;
    return event_queue && xQueueSend(event_queue, &e, wait) == pdTRUE;
}

bool IRAM_ATTR system_post_event_from_isr(task_events_e event)
{
    uint32_t e = event;
    return event_queue && xQueueSendFromISR(event_queue, &e, NULL) == pdTRUE;
}

static void print_sha256(const uint8_t* image_hash, const char* label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition = { 0 };

    // get sha256 digest for bootloader
    partition.address = ESP_BOOTLOADER_OFFSET;
    partition.size = ESP_PARTITION_TABLE_OFFSET;
    partition.type = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

/*
 * A new firmware has to confirm that it runs, otherwise the bootloader goes
 * back to the previous one on the next reset. It is confirmed when it ran
 * for a while without crashing.
 */
#define FIRMWARE_VALID_AFTER_US (60LL * 1000 * 1000)
static void confirm_firmware(void)
{
    static bool confirmed = false;
    if (confirmed || esp_timer_get_time() < FIRMWARE_VALID_AFTER_US)
        return;
    confirmed = true;

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK
        && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "New firmware runs, cancel rollback");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

/*
 * Start and stop the tasks. Tasks are never deleted from outside, they could hold a mutex:
 * a stop request asks the task to end itself.
 */
static void handle_event(task_events_e event)
{
    switch (event) {
    case TASK_EVENT_BUTTON:
        button_handle_event();
        break;
#ifdef WITH_ACC
    case TASK_EVENT_ACCELEROMETER:
        ESP_LOGI(TAG, "Acc");
        break;
#endif
    case TASK_EVENT_ENABLE_GPS:
        gps_start_task();
        break;
    case TASK_EVENT_DISABLE_GPS:
        gps_request_stop();
        break;
    case TASK_EVENT_ENABLE_DISPLAY:
        gui_start_task();
        break;
    case TASK_EVENT_DISABLE_DISPLAY:
        // the GUI task can not be stopped safely, it holds the GUI and SD mutex while rendering
        ESP_LOGW(TAG, "Disabling the display is not supported");
        break;
    case TASK_EVENT_ENABLE_WIFI:
    case TASK_EVENT_START_CHARGING:
        wifi_start_task();
        break;
    case TASK_EVENT_DISABLE_WIFI:
    case TASK_EVENT_STOP_CHARGING:
        wifi_request_stop();
        trigger_rendering();
        break;
    case TASK_EVENT_ENABLE_BLE:
        ble_if_start();
        break;
    case TASK_EVENT_DISABLE_BLE:
        ble_if_stop();
        break;
    default:
        break;
    }
}

void app_main()
{
    uint16_t cnt = 300;
    uint32_t event_num;

    led = gpio_create(OUTPUT, 0, LED);

    // hook into light sleep power management
    esp_pm_light_sleep_register_cbs(&esp_pm_config);

    /* Set Button IO to input, with PUI and any edge IRQ */
    gpio_config_t esp_btn = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pin_bit_mask = BIT64(BTN),
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&esp_btn);
    if (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_EXT0)) {
        rtc_gpio_deinit(BTN);
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (gpio_get_level(BTN)) {
            ESP_LOGI(TAG, "Button Press not long enough for wakeup event. Go back to sleep. %d",gpio_get_level(BTN));
            enter_deep_sleep_if_not_charging();
        }
        ESP_LOGI(TAG, "Wake up from deep sleep. Reset Button GPIO");
        // after deep sleep we want to go into appliaction mode
        gui_set_app_mode(APP_MODE_GPS_CREATE);
    }
    if (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        // the battery was empty, start only if there is energy again
        power_set_wake_up_for_charger(true);
        if (!power_charger_connected_at_wakeup()) {
            enter_deep_sleep_if_not_charging();
        }
        ESP_LOGI(TAG, "Charger found after battery empty, start");
        // the empty screen is shown until the battery has charge for the device
        gui_set_app_mode(APP_MODE_BATTERY_EMPTY);
    }
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    get_sha256_of_partitions();
    // before the power task uses the ADC, password creation needs it as entropy source
    wifi_ap_credentials_init();
    display_settings_init();
    recorder_init();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Initial Heap Free: %zu Byte", xPortGetFreeHeapSize());
    print_semaphore = xSemaphoreCreateMutex();
    event_queue = xQueueCreate(6, sizeof(uint32_t));

    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_EDGE);
    button_init();

    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

#ifndef JTAG
    sd_start_task();
#endif

    ESP_LOGI(TAG, "load configuration file");
#if 0
    async_file_t conf_file;
    conf_file.filename = "config.xml";
    conf_file.loaded = 0;
    if (PM_OK == createFileBuffer(&conf_file))
        loadFile(&conf_file);
    if (conf_file.loaded == LOADED) {
        config_parser(conf_file.dest);
    } else {
        ESP_LOGI(TAG, "Can't load config.xml");
    }
#endif
    power_start_task();
    vTaskDelay(pdMS_TO_TICKS(100));
    gps_start_task();
    gui_start_task();
#ifdef WITH_ACC
    // ESP_ERROR_CHECK(lsm303_init(I2C_MASTER_NUM, I2C_SDA, I2C_SCL));
    // ESP_ERROR_CHECK(lsm303_enable_taping(1));
#endif
    for (;;) {
#ifdef WITH_ACC
        uint8_t tap_register = 0;
        // ESP_ERROR_CHECK(lsm303_read_tap(&tap_register));
        if (tap_register & 0x09) // double tap
        {
            ESP_LOGI(TAG, "Double Tap recognized. rerender.");
            trigger_rendering();
        }
#endif
        if (++cnt >= 300) {
            ESP_LOGI(TAG, "Heap Free: %zu Byte", xPortGetFreeHeapSize());
            cnt = 0;
            ESP_LOGI(TAG, "current_battery_level %ld", power_battery_level());
        }
        confirm_firmware();

        if (xQueueReceive(event_queue, &event_num, ledDelay / portTICK_PERIOD_MS))
            handle_event((task_events_e)event_num);
    }
}
