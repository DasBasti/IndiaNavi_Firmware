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

#include <driver/adc.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_pm.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <lsm303.h>

#include "gui.h"
#include "pins.h"
#include "tasks.h"

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

// File loading queue
QueueHandle_t eventQueueHandle = NULL;

uint32_t ledDelay = 100;

gpio_config_t esp_btn = {};
gpio_config_t esp_acc = {};

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
    adc_power_acquire();
    batteryVoltage = adc1_get_raw(ADC1_CHANNEL_6);
    ESP_LOGI(TAG, "Battery Voltage: %d", batteryVoltage);
    chargerVoltage = adc1_get_raw(ADC1_CHANNEL_7);
    ESP_LOGI(TAG, "Charger Voltage: %d", chargerVoltage);
    adc_power_release();

    if (chargerVoltage - 5 > batteryVoltage)
        return -1;

    const int min = 1750;
    const int max = 2100;
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
    if (gpio_get_level(I2C_INT) == I2C_INT_LEVEL) {
        gpio_num = I2C_INT;
        xQueueSendFromISR(eventQueueHandle, &gpio_num, NULL);
    }
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

    ESP_LOGI(TAG, "Initial Heap Free: %d Byte", xPortGetFreeHeapSize());
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

    /* Set Accelerator IO to input, with PUI and falling edge IRQ */
    esp_acc.mode = GPIO_MODE_INPUT;
    esp_acc.pull_up_en = true;
    esp_acc.pin_bit_mask = BIT64(I2C_INT);
    esp_acc.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&esp_acc);
    gpio_set_intr_type(I2C_INT, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_EDGE);
    gpio_isr_handler_add(I2C_INT, handleButtonPress, NULL);

    // configure ADC for VBATT measurement
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11));
    // ESP_ERROR_CHECK(adc_gpio_init(ADC_UNIT_1, ADC1_CHANNEL_6));

    // configure ADC for VIN measurement -> Loading cable attached?
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11));
    // ESP_ERROR_CHECK(adc_gpio_init(ADC_UNIT_1, ADC1_CHANNEL_7));

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
    // ESP_ERROR_CHECK(lsm303_init(I2C_MASTER_NUM, I2C_SDA, I2C_SCL));
    // ESP_ERROR_CHECK(lsm303_enable_taping(1));
    for (;;) {
        uint8_t tap_register = 0;
        // ESP_ERROR_CHECK(lsm303_read_tap(&tap_register));
        if (tap_register & 0x09) // double tap
        {
            ESP_LOGI(TAG, "Double Tap recognized. rerender.");
            trigger_rendering();
        }

        gpio_write(led, GPIO_SET);
        if (++cnt >= 300) {
            ESP_LOGI(TAG, "Heap Free: %d Byte", xPortGetFreeHeapSize());
#ifdef DEBUG
            esp_pm_dump_locks(stdout);
#endif
            cnt = 0;
        }

        if (battery_label) {
            int bat = readBatteryPercent();
            ESP_LOGI(TAG, "bat %d", bat);
            if (bat >= 0) {
                save_sprintf(battery_label->text, "%03d%%", bat);
            } else {
                save_sprintf(battery_label->text, "CHRG");
            }
            label_shrink_to_text(battery_label);
        }
        // Delay for LED blinking. LED blinks faster if ISRs are handled
        if (xQueueReceive(eventQueueHandle, &event_num, ledDelay / portTICK_PERIOD_MS)) {
            // ESP_LOGI(TAG, "GPIO[%d] intr, val: %d\n", io_num, gpio_get_level(io_num));
            if (event_num == BTN) {
                if (!xSemaphoreGetMutexHolder(gui_semaphore))
                    toggleZoom();
                gpio_isr_handler_add(BTN, handleButtonPress, (void*)BTN);
            }
            if (event_num == I2C_INT) {
                ESP_LOGI(TAG, "Acc");
                // gpio_isr_handler_add(I2C_INT, handleButtonPress, (void*) I2C_INT);
            }
            if (event_num == TASK_EVENT_ENABLE_GPS) {
                xTaskCreate(&StartGpsTask, "gps", taskGPSStackSize, NULL, tskIDLE_PRIORITY, &gpsTask_h);
            }
            if (event_num == TASK_EVENT_DISABLE_GPS) {
                vTaskDelete(&gpsTask_h);
            }
            if (event_num == TASK_EVENT_ENABLE_DISPLAY) {
                xTaskCreate(&StartGuiTask, "gui", taskGUIStackSize, NULL, 6, &guiTask_h);
            }
            if (event_num == TASK_EVENT_DISABLE_DISPLAY) {
                vTaskDelete(&guiTask_h);
            }
            if (event_num == TASK_EVENT_ENABLE_WIFI) {
                xTaskCreate(&StartWiFiTask, "wifi", taskWifiStackSize, NULL, 8, &wifiTask_h);
            }
            if (event_num == TASK_EVENT_DISABLE_WIFI) {
                vTaskDelete(&wifiTask_h);
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
