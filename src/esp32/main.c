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
#include <esp_adc/adc_oneshot.h>
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
#include "pins.h"
#include "tasks.h"

#include <icons_32.h>

static const char* TAG = "MAIN";
#define HASH_LEN 32
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");

#define taskGenericStackSize 1024 * 2
#define taskPowerStackSize 1024 * 6
#define taskGPSStackSize 1024 * 7
#define taskGUIStackSize 1024 * 10
#define taskSDStackSize 1024 * 8
#define taskWifiStackSize 1024 * 8
#define taskDownloaderStackSize 1024 * 8

void StartGpsTask(void* argument);
void StartGuiTask(void* argument);
void StartPowerTask(void* argument);
void StartSDTask(void* argument);
void StartWiFiTask(void* argument);
void StartMapDownloaderTask(void* argument);

// Create semphore
SemaphoreHandle_t print_semaphore = NULL;
SemaphoreHandle_t gui_semaphore = NULL;
SemaphoreHandle_t sd_semaphore = NULL;

TaskHandle_t housekeepingTask_h;
TaskHandle_t gpsTask_h;
TaskHandle_t guiTask_h;
TaskHandle_t powerTask_h;
TaskHandle_t sdTask_h;
TaskHandle_t wifiTask_h;
TaskHandle_t mapLoaderTask_h;

QueueHandle_t eventQueueHandle = NULL;

uint32_t ledDelay = 100;

gpio_config_t esp_btn = {};
gpio_config_t esp_acc = {};
int32_t current_battery_level = 0;
int32_t is_charging;
gpio_t* led;

esp_timer_handle_t button_timer;
void (*_short_press)(void);
void (*_long_press)(void);

esp_pm_config_t pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 80,
    .light_sleep_enable = true,
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

static void print_sha256(const uint8_t* image_hash, const char* label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

/**
 * @brief Function to read battery voltage
 * @param none
 * @retval int: battery voltage in mV
 */
int readBatteryPercent(adc_oneshot_unit_handle_t adc_handle)
{
    int batteryVoltage;
    int chargerVoltage;
    task_events_e chargingTrigger = TASK_EVENT_NO_EVENT;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, VBAT_ADC, &batteryVoltage));
    ESP_LOGI(TAG, "Battery Voltage: %d", batteryVoltage);
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, VIN_ADC, &chargerVoltage));
    ESP_LOGI(TAG, "Charger Voltage: %d", chargerVoltage);

    if (chargerVoltage - 5 > batteryVoltage && !is_charging) {
        chargingTrigger = TASK_EVENT_START_CHARGING;
        xQueueSend(eventQueueHandle, &chargingTrigger, 0);
        is_charging = true;
        if (battery_indicator)
            battery_indicator->charging = true;
    } else if (chargerVoltage - 5 < batteryVoltage && is_charging) {
        chargingTrigger = TASK_EVENT_STOP_CHARGING;
        xQueueSend(eventQueueHandle, &chargingTrigger, 0);
        is_charging = false;
        if (battery_indicator)
            battery_indicator->charging = true;
    }

    const int min = 1550;
    const int max = 2330;
    if (batteryVoltage > max)
        return 100;

    if (batteryVoltage < min)
        return 0;

    /* ganz simpler dreisatz, den man in wenigen
       sekunden im Kopf lösen kann */
    int value = batteryVoltage - min;

    return value * 100 / (max - min);
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

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

static void IRAM_ATTR handleButtonPress(void* arg)
{
    uint32_t gpio_num = UINT32_MAX;
#ifdef WITH_ACC
    if (gpio_get_level(I2C_INT) == I2C_INT_LEVEL) {
        gpio_num = I2C_INT;
    }
#endif
    if (gpio_get_level(BTN) == BTN_LEVEL) {
        gpio_num = TASK_EVENT_BUTTON_DOWN;
    } else {
        gpio_num = TASK_EVENT_BUTTON_UP;
    }
    xQueueSendFromISR(eventQueueHandle, &gpio_num, NULL);
}

/**
 * Remove ISR trigger from Button and change application to shut down
 */
void button_timer_trigger(void* arg)
{
    gpio_isr_handler_remove(BTN);
    if (_long_press)
        _long_press();
    else
        gui_set_app_mode(APP_MODE_TURN_OFF);
}

error_code_t enter_deep_sleep_if_not_charging()
{
    if (is_charging)
        return DEFERRED;

    const gpio_config_t config = {
        .pin_bit_mask = BIT(BTN),
        .mode = GPIO_MODE_INPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BTN, BTN_LEVEL));
    rtc_gpio_pullup_en(BTN);
    rtc_gpio_pulldown_dis(BTN);

    ESP_LOGI(TAG, "enter deep sleep now...");
    esp_deep_sleep_start();
    return PM_OK;
}

void set_short_press_event(void (*event)(void))
{
    _short_press = event;
}

void set_long_press_event(void (*event)(void))
{
    _long_press = event;
}

void app_main()
{
    uint16_t cnt = 300;
    uint32_t event_num;

    led = gpio_create(OUTPUT, 0, LED);

    // hook into light sleep power management
    esp_pm_light_sleep_register_cbs(&esp_pm_config);

    /* Set Button IO to input, with PUI and any edge IRQ */
    esp_btn.mode
        = GPIO_MODE_INPUT;
    esp_btn.pull_up_en = true;
    esp_btn.pin_bit_mask = BIT64(BTN);
    esp_btn.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&esp_btn);
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
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
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    get_sha256_of_partitions();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Initial Heap Free: %zu Byte", xPortGetFreeHeapSize());
    print_semaphore = xSemaphoreCreateMutex();
    gui_semaphore = xSemaphoreCreateMutex();
    eventQueueHandle = xQueueCreate(6, sizeof(uint32_t));

    gpio_set_intr_type(BTN, GPIO_INTR_ANYEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_EDGE);
    gpio_isr_handler_add(BTN, handleButtonPress, NULL);

    // configure PM
    // ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

#ifdef WITH_ACC
    /* Set Accelerator IO to input, with PUI and falling edge IRQ */
    esp_acc.mode = GPIO_MODE_INPUT;
    esp_acc.pull_up_en = true;
    esp_acc.pin_bit_mask = BIT64(I2C_INT);
    esp_acc.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&esp_acc);
    gpio_set_intr_type(I2C_INT, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_EDGE);
    gpio_isr_handler_add(I2C_INT, handleButtonPress, NULL);
#endif

    esp_timer_create_args_t button_timer_args = {
        .callback = button_timer_trigger,
    };
    esp_timer_create(&button_timer_args, &button_timer);

#ifndef JTAG
    xTaskCreate(&StartSDTask, "sd", taskSDStackSize, NULL, 1, &sdTask_h);
    while (!sd_semaphore) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
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
    xTaskCreate(&StartPowerTask, "power", taskPowerStackSize, NULL, tskIDLE_PRIORITY, &powerTask_h);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreate(&StartGpsTask, "gps", taskGPSStackSize, NULL, tskIDLE_PRIORITY, &gpsTask_h);
    xTaskCreate(&StartGuiTask, "gui", taskGUIStackSize, NULL, 6, &guiTask_h);
    // xTaskCreate(&StartWiFiTask, "wifi", taskWifiStackSize, NULL, 8, &wifiTask_h);

    /*
       uint32_t evt;
       evt = TASK_EVENT_ENABLE_DISPLAY;
       xQueueSend(eventQueueHandle, &evt, 0);
       evt = TASK_EVENT_ENABLE_GPS;
       xQueueSend(eventQueueHandle, &evt, 0);
      */
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
            ESP_LOGI(TAG, "current_battery_level %ld", current_battery_level);
        }

        if (xQueueReceive(eventQueueHandle, &event_num, ledDelay / portTICK_PERIOD_MS)) {
            if (event_num == TASK_EVENT_BUTTON_DOWN) {
                ESP_LOGI(TAG, "Button down");
                esp_timer_start_once(button_timer, 3000000); // 3 Seconds timeout for long press
            } else if (event_num == TASK_EVENT_BUTTON_UP) {
                ESP_LOGI(TAG, "Button up");
                esp_timer_stop(button_timer); // stop long press timer
                if (_short_press)
                    _short_press();
            }
#ifdef WITH_ACC
            else if (event_num == I2C_INT) {
                ESP_LOGI(TAG, "Acc");
                // gpio_isr_handler_add(I2C_INT, handleButtonPress, (void*) I2C_INT);
            }
#endif
            else if (event_num == TASK_EVENT_ENABLE_GPS) {
                xTaskCreate(&StartGpsTask, "gps", taskGPSStackSize, NULL, tskIDLE_PRIORITY, &gpsTask_h);
            } else if (event_num == TASK_EVENT_DISABLE_GPS) {
                vTaskDelete(gpsTask_h);
            } else if (event_num == TASK_EVENT_ENABLE_DISPLAY) {
                xTaskCreate(&StartGuiTask, "gui", taskGUIStackSize, NULL, 6, &guiTask_h);
            } else if (event_num == TASK_EVENT_DISABLE_DISPLAY) {
                vTaskDelete(guiTask_h);
            } else if (event_num == TASK_EVENT_ENABLE_WIFI || event_num == TASK_EVENT_START_CHARGING) {
                xTaskCreate(&StartWiFiTask, "wifi", taskWifiStackSize, NULL, 8, &wifiTask_h);
            } else if (event_num == TASK_EVENT_DISABLE_WIFI || event_num == TASK_EVENT_STOP_CHARGING) {
                vTaskDelete(wifiTask_h);
                trigger_rendering();
            }
        }
    }
}

/**
 * @brief Function implementing the gpsTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartGpsTask(void* argument)
{
    for (;;) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the guiTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartGuiTask(void* argument)
{
    for (;;) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the SDTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartSDTask(void* argument)
{
    for (;;) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the WiFiTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartWiFiTask(void* argument)
{
    for (;;) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the MapDownloaderTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartMapDownloaderTask(void* argument)
{
    for (;;) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the powerTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartPowerTask(void* argument)
{
    TickType_t delay_time = 10;
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VBAT_ADC, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VIN_ADC, &config));

    for (;;) {
        current_battery_level = readBatteryPercent(adc1_handle);
        if (battery_indicator) {
            battery_indicator_set_level(battery_indicator, current_battery_level);
        }
        if (is_charging)
            delay_time = 1000;
        else
            delay_time = 60000;
        vTaskDelay(pdMS_TO_TICKS(delay_time));
    }
}
