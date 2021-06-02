#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <Platinenmacher.h>

#include <esp_log.h>
#include <driver/gpio.h>

#include "pins.h"
#include "tasks.h"
#include "gui.h"

static const char *TAG = "MAIN";

#define taskGenericStackSize 1024 * 2
#define taskGPSStackSize 1024 * 3
#define taskGUIStackSize 1024 * 2
#define taskSDStackSize 1024 * 8

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

void app_main()
{
    ESP_LOGI(TAG, "Initial Heap Free: %d Byte", xPortGetFreeHeapSize());
    print_semaphore = xSemaphoreCreateMutex();
    gui_semaphore = xSemaphoreCreateMutex();
    sd_semaphore = xSemaphoreCreateMutex();
    mapLoadQueueHandle = xQueueCreate(6, sizeof(map_tile_t *));
    fileLoadQueueHandle = xQueueCreate(6, sizeof(async_file_t *));
    ESP_LOGI(TAG, "start");
    command_init();
    xTaskCreate(&StartHousekeepingTask, "housekeeping", taskGenericStackSize, NULL, 10, NULL);
    xTaskCreate(&StartGpsTask, "gps", taskGPSStackSize, NULL, 5, NULL);
    xTaskCreate(&StartGuiTask, "gui", taskGUIStackSize, NULL, 6, NULL);
    xTaskCreate(&StartSDTask, "sd", taskSDStackSize, NULL, 7, NULL);
    xTaskCreate(&StartWiFiTask, "wifi", taskGenericStackSize, NULL, 8, NULL);
}

/**
 * @brief  Function implementing the housekeepingTas thread.
 * @param  argument: Not used
 * @retval None
 */
void StartHousekeepingTask(void *argument)
{
    uint8_t cnt = 0;
    gpio_t *led = gpio_create(OUTPUT, 0, LED);
    uint8_t ledState = 1;
    for (;;)
    {
        gpio_write(led, ledState);
        ledState = !ledState;
        if (++cnt == 60)
        {
            ESP_LOGI(TAG, "Heap Free: %d Byte", xPortGetFreeHeapSize());
            cnt = 0;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
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
