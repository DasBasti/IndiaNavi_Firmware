/*
 * Main Task for Wander Navi Application
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Platinenmacher.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <lsm303.h>

#include "gui.h"
#include "pins.h"
#include "tasks.h"

#include "icons_32/icons_32.h"

static const char* TAG = "MAIN";
#define HASH_LEN 32
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");

#define taskGenericStackSize 1024 * 2
#define taskGPSStackSize 1024 * 6
#define taskGUIStackSize 1024 * 10
#define taskSDStackSize 1024 * 8
#define taskWifiStackSize 1024 * 5
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

// File loading queue
QueueHandle_t eventQueueHandle = NULL;

uint32_t ledDelay = 100;

gpio_config_t esp_btn = {};
gpio_config_t esp_acc = {};
int32_t current_battery_level;
int32_t is_charging;

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
int readBatteryPercent()
{
    int batteryVoltage;
    int chargerVoltage;
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VBAT_ADC, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VIN_ADC, &config));
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, VBAT_ADC, &batteryVoltage));
    ESP_LOGI(TAG, "Battery Voltage: %d", batteryVoltage);
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, VIN_ADC, &chargerVoltage));
    ESP_LOGI(TAG, "Charger Voltage: %d", chargerVoltage);

    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));

    is_charging = (chargerVoltage - 5 > batteryVoltage);

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
    if (gpio_get_level(BTN) == BTN_LEVEL) {
        gpio_num = BTN;
        xQueueSendFromISR(eventQueueHandle, &gpio_num, NULL);
    }
#ifdef WITH_ACC
    if (gpio_get_level(I2C_INT) == I2C_INT_LEVEL) {
        gpio_num = I2C_INT;
        xQueueSendFromISR(eventQueueHandle, &gpio_num, NULL);
    }
#endif
    // deactivate isr handler until reinitialized by queue handling
    if (gpio_num != UINT32_MAX)
        gpio_isr_handler_remove(gpio_num);
}

void app_main()
{
    uint16_t cnt = 300;
    uint32_t event_num;
    gpio_t* led = gpio_create(OUTPUT, 0, LED);
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

    ESP_LOGI(TAG, "Initial Heap Free: %lu Byte", xPortGetFreeHeapSize());
    print_semaphore = xSemaphoreCreateMutex();
    gui_semaphore = xSemaphoreCreateMutex();
    sd_semaphore = xSemaphoreCreateMutex();
    eventQueueHandle = xQueueCreate(6, sizeof(uint32_t));

    /* Set Button IO to input, with PUI and falling edge IRQ */
    esp_btn.mode = GPIO_MODE_INPUT;
    esp_btn.pull_up_en = true;
    esp_btn.pin_bit_mask = BIT64(BTN);
    esp_btn.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&esp_btn);
    gpio_set_intr_type(BTN, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_EDGE);
    gpio_isr_handler_add(BTN, handleButtonPress, NULL);

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

    // inital battery level read
    current_battery_level = readBatteryPercent();

    ESP_LOGI(TAG, "start");

    xTaskCreate(&StartGpsTask, "gps", taskGPSStackSize, NULL, tskIDLE_PRIORITY, &gpsTask_h);
    xTaskCreate(&StartGuiTask, "gui", taskGUIStackSize, NULL, 6, &guiTask_h);
#ifndef JTAG
    xTaskCreate(&StartSDTask, "sd", taskSDStackSize, NULL, 1, &sdTask_h);
#endif
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
        gpio_write(led, GPIO_SET);
        if (++cnt >= 300) {
            ESP_LOGI(TAG, "Heap Free: %lu Byte", xPortGetFreeHeapSize());
            cnt = 0;
        }

        // FIXME: ADC crashes while running in this loop. Works fine outside of loop readBatteryPercent();
        current_battery_level = 75;
        if (battery_label) {
            image_t* bat_icon = battery_label->child;
            ESP_LOGI(TAG, "current_battery_level %ld", current_battery_level);
            if (current_battery_level > 80)
                bat_icon->data = bat_100;
            else if (current_battery_level > 50)
                bat_icon->data = bat_80;
            else if (current_battery_level > 30)
                bat_icon->data = bat_50;
            else if (current_battery_level > 10)
                bat_icon->data = bat_30;
            else if (current_battery_level > 1)
                bat_icon->data = bat_10;
            else if (current_battery_level == 0)
                bat_icon->data = bat_0;
            if (!is_charging) {
                save_sprintf(battery_label->text, "%3ld%%", current_battery_level);
            } else {
                save_sprintf(battery_label->text, "CHRG");
            }
            label_shrink_to_text(battery_label);
        }
        // Delay for LED blinking. LED blinks faster if ISRs are handled
        if (xQueueReceive(eventQueueHandle, &event_num, ledDelay / portTICK_PERIOD_MS)) {
            // ESP_LOGI(TAG, "GPIO[%d] intr, val: %d\n", io_num, gpio_get_level(io_num));
            if (event_num == BTN) {
                ESP_LOGI(TAG, "button gedrückt!");
                if (!xSemaphoreGetMutexHolder(gui_semaphore))
                    //toggleZoom();
                gpio_isr_handler_add(BTN, handleButtonPress, (void*)BTN);
            }
#ifdef WITH_ACC
            if (event_num == I2C_INT) {
                ESP_LOGI(TAG, "Acc");
                // gpio_isr_handler_add(I2C_INT, handleButtonPress, (void*) I2C_INT);
            }
#endif
            if (event_num == TASK_EVENT_ENABLE_GPS) {
                xTaskCreate(&StartGpsTask, "gps", taskGPSStackSize, NULL, tskIDLE_PRIORITY, &gpsTask_h);
            }
            if (event_num == TASK_EVENT_DISABLE_GPS) {
                vTaskDelete(gpsTask_h);
            }
            if (event_num == TASK_EVENT_ENABLE_DISPLAY) {
                xTaskCreate(&StartGuiTask, "gui", taskGUIStackSize, NULL, 6, &guiTask_h);
            }
            if (event_num == TASK_EVENT_DISABLE_DISPLAY) {
                vTaskDelete(guiTask_h);
            }
            if (event_num == TASK_EVENT_ENABLE_WIFI) {
                xTaskCreate(&StartWiFiTask, "wifi", taskWifiStackSize, NULL, 8, &wifiTask_h);
            }
            if (event_num == TASK_EVENT_DISABLE_WIFI) {
                vTaskDelete(wifiTask_h);
            }
        }
        gpio_write(led, GPIO_RESET);
        vTaskDelay(pdTICKS_TO_MS(100));
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
 * @brief Function implementing the powerTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartPowerTask(void* argument)
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
