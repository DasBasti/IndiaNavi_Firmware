#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <Platinenmacher.h>

#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <driver/adc.h>

#include "pins.h"
#include "tasks.h"
#include "gui.h"

static const char *TAG = "MAIN";
#define HASH_LEN 32
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");

#define taskGenericStackSize 1024 * 2
#define taskGPSStackSize 1024 * 3
#define taskGUIStackSize 1024 * 2
#define taskSDStackSize 1024 * 8
#define taskWifiStackSize 1024 * 3

void StartHousekeepingTask(void *argument);
void StartGpsTask(void *argument);
void StartGuiTask(void *argument);
void StartPowerTask(void *argument);
void StartSDTask(void *argument);
void StartWiFiTask(void *argument);

//Create semphore
SemaphoreHandle_t print_semaphore = NULL;
SemaphoreHandle_t gui_semaphore = NULL;
SemaphoreHandle_t sd_semaphore = NULL;

// File loading queue
QueueHandle_t mapLoadQueueHandle = NULL;
QueueHandle_t fileLoadQueueHandle = NULL;

uint32_t ledDelay = 1000;

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i)
    {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = {0};
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

void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
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
    mapLoadQueueHandle = xQueueCreate(6, sizeof(map_tile_t *));
    fileLoadQueueHandle = xQueueCreate(6, sizeof(async_file_t *));
    ESP_LOGI(TAG, "start");
    command_init();
    xTaskCreate(&StartHousekeepingTask, "housekeeping", taskGenericStackSize, NULL, 10, &housekeepingTask_h);
    xTaskCreate(&StartGpsTask, "gps", taskGPSStackSize, NULL, 5, &gpsTask_h);
    xTaskCreate(&StartGuiTask, "gui", taskGUIStackSize, NULL, 6, &guiTask_h);
    xTaskCreate(&StartSDTask, "sd", taskSDStackSize, NULL, 1, &sdTask_h);
    //xTaskCreate(&StartWiFiTask, "wifi", taskWifiStackSize, NULL, 8, &wifiTask_h);
}

/**
 * @brief Function to read battery voltage
 * @param none
 * @retval int: battery voltage in mV
 */
int readBatteryPercent()
{
    int batteryVoltage;
    adc_power_acquire();
    batteryVoltage = adc1_get_raw(ADC1_CHANNEL_6);
    ESP_LOGI(TAG, "ADC1/6: %d", batteryVoltage);
    adc_power_release();

    const int min = 1750;
    const int max = 2100;
    if (batteryVoltage > max)
        return 100;
    
    /* ganz simpler dreisatz, den man in wenigen
       sekunden im Kopf lösen kann */
    int value = batteryVoltage - min;

    return value*100/(max-min);
}

/**
 * @brief  Function implementing the housekeepingTas thread.
 * @param  argument: Not used
 * @retval None
 */
void StartHousekeepingTask(void *argument)
{
    uint8_t cnt = 30;
    gpio_t *led = gpio_create(OUTPUT, 0, LED);
    uint8_t ledState = 1;
    // configure ADC for VBATT measurement
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11));
    ESP_ERROR_CHECK(adc_gpio_init(ADC_UNIT_1, ADC1_CHANNEL_6));

    // configure ADC for VIN measurement -> Loading cable attached?
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11));
    ESP_ERROR_CHECK(adc_gpio_init(ADC_UNIT_1, ADC1_CHANNEL_7));

    for (;;)
    {
        gpio_write(led, ledState);
        ledState = !ledState;
        if (++cnt >= 60)
        {
            ESP_LOGI(TAG, "Heap Free: %d Byte", xPortGetFreeHeapSize());
            cnt = 0;

            if(battery_label){
                save_sprintf(battery_label->text, "%03d%%", readBatteryPercent());
               	label_shrink_to_text(battery_label);

            }

        }
        vTaskDelay(ledDelay / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the gpsTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartGpsTask(void *argument)
{
    for (;;)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the guiTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartGuiTask(void *argument)
{
    for (;;)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the powerTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartPowerTask(void *argument)
{
    for (;;)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the SDTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartSDTask(void *argument)
{
    for (;;)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Function implementing the WiFiTask thread.
 * @param argument: Not used
 * @retval None
 */
__weak void StartWiFiTask(void *argument)
{
    for (;;)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
