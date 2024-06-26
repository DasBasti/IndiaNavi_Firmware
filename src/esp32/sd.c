/*
 * sd.c
 *
 *  Created on: Jan 9, 2021
 *      Author: bastian
 */

#include <Platinenmacher.h>
#include <driver/sdmmc_default_configs.h>
#include <driver/sdmmc_defs.h>
#include <driver/sdmmc_host.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "gui.h"
#include "helper.h"
#include "pins.h"
#include "tasks.h"
#include <icons_32.h>

uint8_t sd_status = UNAVAILABLE;
char fn[30];
sdmmc_card_t* card;

static const char* TAG = "SD";

#ifdef ESP_S3
static const sdmmc_slot_config_t slot_config = {
    .clk = SD_SPI_CLK,
    .cmd = SD_SPI_nCS,
    .d0 = SD_SPI_D0,
    .d1 = SD_SPI_D1,
    .d2 = SD_SPI_D2,
    .d3 = SD_SPI_D3,
    .d4 = GPIO_NUM_NC,
    .d5 = GPIO_NUM_NC,
    .d6 = GPIO_NUM_NC,
    .d7 = GPIO_NUM_NC,
    .cd = SD_CARD_nDET,
    .wp = SDMMC_SLOT_NO_WP,
    .width = 4,
    .flags = 0,
};
#else
static const sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
#endif // ESP_S3
static const sdmmc_host_t host = SDMMC_HOST_DEFAULT();

static const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 1,
    .allocation_unit_size = 0
};

error_code_t statusRender(const display_t* dsp, void* comp)
{
    image_t* icon = sd_indicator_label->child;
    if (sd_status == PM_OK)
        icon->data = SD;   // show SD card symbol
    else
        icon->data = noSD; // show SD card symbol

    return PM_OK;
}

/*
 * Wait for initialization of SD semaphore
 */
error_code_t waitForSDInit()
{
    size_t count = 0;
    while (!sd_semaphore) {
        vTaskDelay(1);
        if (count++ > 1000)
            return PM_FAIL;
    }
    return PM_OK;
}
/*
 * Queue arbitrary file reads.
 *
 * Allocates memory if destination buffer is not initialized.
 */
error_code_t loadFile(async_file_t* file)
{
    FRESULT res;
    FIL t_file;
    FILINFO fno;
    uint32_t br;
    ESP_LOGI(TAG, "Load %s ", file->filename);
    waitForSDInit();
    if (xSemaphoreTake(sd_semaphore, pdTICKS_TO_MS(1000))) {
        res = f_stat(file->filename, &fno);
        if (FR_OK == res) {
            if (!file->dest) {
                file->dest = RTOS_Malloc(fno.fsize);
            }
            res = f_open(&t_file, file->filename, FA_READ);
            if (FR_OK == res && file->dest != 0) {
                res = f_read(&t_file,
                    file->dest, fno.fsize, (UINT*)&br);
                if (FR_OK == res) {
                    file->loaded = LOADED;
                } else {
                    ESP_LOGE(TAG, "cannot read %s", file->filename);
                }
                f_close(&t_file);
            } else {
                ESP_LOGE(TAG, "cannot open %s", file->filename);
            }
        } else {
            ESP_LOGE(TAG, "cannot stat %s", file->filename);
        }
        xSemaphoreGive(sd_semaphore);
    } else {
        ESP_LOGE(TAG, "sd semapore not available");
    }

    if (file->loaded == LOADED)
        return PM_OK;
    return PM_FAIL;
}

error_code_t fileExists(async_file_t* file)
{
    xSemaphoreTake(sd_semaphore, portMAX_DELAY);
    FILINFO fno;
    FRESULT fres = f_stat(file->filename, &fno);
    xSemaphoreGive(sd_semaphore);
    if (FR_OK == fres)
        return PM_OK;
    return PM_FAIL;
}

error_code_t createFileBuffer(async_file_t* file)
{
    xSemaphoreTake(sd_semaphore, portMAX_DELAY);
    FILINFO fno;
    FRESULT fres = f_stat(file->filename, &fno);
    xSemaphoreGive(sd_semaphore);
    if (FR_OK == fres) {
        file->dest = RTOS_Malloc(fno.fsize);
        return PM_OK;
    }
    return PM_FAIL;
}

error_code_t openFileForWriting(async_file_t* file)
{
    if (!file->file)
        file->file = RTOS_Malloc(sizeof(FIL));
    // try to open file
    FRESULT res = f_open(file->file, file->filename, FA_WRITE | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_OPEN_APPEND);
    if (FR_NO_PATH == res) {
        // 1. try to create path to file if a path is given
        if (strlen(file->filename) < 2)
            return PM_FAIL;

        char* path = RTOS_Malloc(strlen(file->filename) + 1);
        char* tmp_path = RTOS_Malloc(strlen(file->filename) + 1);

        // 2. skip first // for root
        strcpy(path, file->filename);
        path += 2;
        // strcat(tmp_path, "/");

        // 3. loop through folders
        char* strtokCtx;
        char* token = strtok_r(path, "/", &strtokCtx);
        while (token != NULL) {
            strcat(tmp_path, token);
            // Create the folder in the path
            res = f_mkdir(tmp_path);
            if (FR_OK != res) {
                ESP_LOGD(TAG, "Folder %s could not be created: %d", tmp_path, res);
                break;
            }
            strcat(tmp_path, "/");
            token = strtok_r(NULL, "/", &strtokCtx);

            // Check if the path has a "File extention" so we skip creating a folder for it
            if (token && strchr(token, '.'))
                break;
        }
        RTOS_Free(tmp_path);
        // Retry to open file for writing
        res = f_open(file->file, file->filename, FA_WRITE | FA_CREATE_NEW);
    }

    ESP_LOGD(TAG, "File %s -> %d", file->filename, res);
    if (FR_OK == res)
        return PM_OK;
    return PM_FAIL;
}

async_file_t* createPhysicalFile()
{
    async_file_t* f = RTOS_Malloc(sizeof(async_file_t));
    f->file = RTOS_Malloc(sizeof(FIL));
    return f;
}

error_code_t writeToFile(async_file_t* file, void* in_data, uint32_t count, uint32_t* written)
{
    FRESULT res = f_write(file->file, in_data, count, (UINT*)written);
    if (FR_OK == res && FR_OK == f_sync(file->file))
        return PM_OK;
    return PM_FAIL;
}

error_code_t closeFile(async_file_t* file)
{
    FRESULT res = f_close(file->file);
    if (FR_OK == res)
        return PM_OK;
    return PM_FAIL;
}

error_code_t deleteFile(async_file_t* file)
{
    FRESULT res = f_unlink(file->filename);
    if (FR_OK == res)
        return PM_OK;
    return PM_FAIL;
}

void closePhysicalFile(async_file_t* file)
{
    if (file) {
        if (file->file) {
            if (file->file->fptr)
                ESP_LOGI(TAG, "File: %lu is still open", file->file->fptr);
            // f_close(file->file);
            // RTOS_Free(file->file);
        }
        if (file->dest) {
            ESP_LOGI(TAG, "Free file->dest");
            RTOS_Free(file->dest);
        }
        RTOS_Free(file);
    }
}

void StartSDTask(void const* argument)
{
    sd_semaphore = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "init semaphore");
    xSemaphoreTake(sd_semaphore, portMAX_DELAY); // block SD mutex
    ESP_LOGI(TAG, "init gpio %d", SD_VCC_nEN);

    /* create power regulator */
    gpio_t* reg_gpio = gpio_create(OUTPUT, 0, SD_VCC_nEN);
    reg_gpio->onValue = GPIO_RESET;
    regulator_t* reg = regulator_gpio_create(reg_gpio);
    reg->disable(reg);
    vTaskDelay(pdMS_TO_TICKS(100));
    reg->enable(reg);

    gpio_t* dc_dt = gpio_create(INPUT, 0, SD_CARD_nDET);

    /* initialize SD card */
    vTaskDelay(pdMS_TO_TICKS(100));

    for (;;) {
        if (!gpio_read(dc_dt)) {
            if (sd_status != PM_OK) {
                sd_status = PM_OK;
                esp_err_t ret = esp_vfs_fat_sdmmc_mount("", &host, &slot_config, &mount_config, &card);
                if (ret != ESP_OK) {
                    if (ret == ESP_FAIL) {
                        ESP_LOGE(TAG, "Failed to mount filesystem.");
                    } else {
                        ESP_LOGE(TAG, "Failed to initialize the card (0x%x).", ret);
                    }
                    sd_status = !PM_OK;
                }
                // Card has been initialized, print its properties
                ESP_LOGI(TAG, "SDC: init done");
                xSemaphoreGive(sd_semaphore);
                trigger_rendering();
            }
        } else {
            if (sd_status == PM_OK) {
                ESP_LOGE("SD", "Unmount filesystem.");
                sd_status = UNAVAILABLE;
                xSemaphoreTake(sd_semaphore, portMAX_DELAY);
                // deinit SDMMC periphery
                esp_vfs_fat_sdmmc_unmount();
                // show on gui
                trigger_rendering();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        if (sd_indicator_label)
            sd_indicator_label->onBeforeRender = statusRender;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
