/*
 * sd.c
 *
 *  Created on: Jan 9, 2021
 *      Author: bastian
 */

#include <Platinenmacher.h>
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
#include "icons_32/icons_32.h"
#include "pins.h"
#include "tasks.h"

uint8_t sd_status = UNAVAILABLE;
char fn[30];
sdmmc_card_t* card;

static const char* TAG = "SD";

static const sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
static const sdmmc_host_t host = {
    .flags = SDMMC_HOST_FLAG_4BIT,
    .slot = SDMMC_HOST_SLOT_1,
    .max_freq_khz = SDMMC_FREQ_HIGHSPEED,
    .io_voltage = 3.3f,
    .init = &sdmmc_host_init,
    .set_bus_width = &sdmmc_host_set_bus_width,
    .get_bus_width = &sdmmc_host_get_slot_width,
    .set_bus_ddr_mode = &sdmmc_host_set_bus_ddr_mode,
    .set_card_clk = &sdmmc_host_set_card_clk,
    .do_transaction = &sdmmc_host_do_transaction,
    .deinit = &sdmmc_host_deinit,
    .io_int_enable = &sdmmc_host_io_int_enable,
    .io_int_wait = &sdmmc_host_io_int_wait,
    .command_timeout_ms = 0
};

static const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 1,
    .allocation_unit_size = 0
};

error_code_t statusRender(const display_t* dsp, void* comp)
{
    image_t* icon = sd_indicator_label->child;
    if (sd_status == OK)
        icon->data = SD; // show SD card symbol
    else
        icon->data = noSD; // show SD card symbol

    return PM_OK;
}

/*
 * Queue arbitrary file reads.
 */
error_code_t loadFile(async_file_t* file)
{
    FRESULT res;
    FIL t_file;
    FILINFO fno;
    uint32_t br;
    ESP_LOGI(TAG, "Load %s ", file->filename);
    if (xSemaphoreTake(sd_semaphore, pdTICKS_TO_MS(1000))) {
        res = f_stat(file->filename, &fno);
        if (FR_OK == res) {
            res = f_open(&t_file, file->filename, FA_READ);
            if (FR_OK == res && file->dest != 0) {
                res = f_read(&t_file,
                    file->dest, fno.fsize, &br);
                if (FR_OK == res) {
                    file->loaded = LOADED;
                }
                f_close(&t_file);
            } else {
                ESP_LOGI(TAG, "%s not found.", file->filename);
            }
        }
        xSemaphoreGive(sd_semaphore);
    } else {
        ESP_LOGE(TAG, "sd semapore not available");
    }

    if (file->loaded == 1)
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
    // try to open file
    FRESULT res = f_open(file->file, file->filename, FA_WRITE | FA_CREATE_NEW);
    if (FR_NO_PATH == res) {
        // 1. try to create path to file if a path is given
        if (strlen(file->filename) < 2)
            return PM_FAIL;

        char* path = RTOS_Malloc(strlen(file->filename));
        char* tmp_path = RTOS_Malloc(strlen(file->filename));

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
    FRESULT res = f_write(file->file, in_data, count, written);
    if (FR_OK == res)
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

void closePhysicalFile(async_file_t* file)
{
    if (file) {
        if (file->file) {
            if (file->file->fptr)
                ESP_LOGI(TAG, "File: %d is still open", file->file->fptr);
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
    xSemaphoreTake(sd_semaphore, portMAX_DELAY); // block SD mutex
    ESP_LOGI(TAG, "init gpio %d", SD_VCC_nEN);

    /* create power regulator */
    gpio_t* reg_gpio = gpio_create(OUTPUT, 0, SD_VCC_nEN);
    reg_gpio->onValue = GPIO_RESET;
    regulator_t* reg = regulator_gpio_create(reg_gpio);
    reg->disable(reg);
    vTaskDelay(100);
    reg->enable(reg);

    gpio_t* dc_dt = gpio_create(INPUT, 0, SD_CARD_nDET);
    gpio_set_pull_mode(SD_CARD_nDET, GPIO_PULLUP_ENABLE);

    /* initialize SD card */
    vTaskDelay(100 / portTICK_PERIOD_MS);

    for (;;) {
        if (!gpio_read(dc_dt)) {
            if (sd_status != OK) {
                sd_status = OK;
                esp_err_t ret = esp_vfs_fat_sdmmc_mount("", &host, &slot_config, &mount_config, &card);
                if (ret != ESP_OK) {
                    if (ret == ESP_FAIL) {
                        ESP_LOGE(TAG, "Failed to mount filesystem.");
                    } else {
                        ESP_LOGE(TAG, "Failed to initialize the card (0x%x).", ret);
                    }
                    sd_status = !OK;
                }
                // Card has been initialized, print its properties
                ESP_LOGI(TAG, "SDC: init done");
                xSemaphoreGive(sd_semaphore);
                trigger_rendering();
            }
        } else {
            if (sd_status == OK) {
                ESP_LOGE("SD", "Unmount filesystem.");
                sd_status = UNAVAILABLE;
                xSemaphoreTake(sd_semaphore, portMAX_DELAY);
                // deinit SDMMC periphery
                esp_vfs_fat_sdmmc_unmount();
                // show on gui
                trigger_rendering();
            }
        }

        if (sd_indicator_label)
            sd_indicator_label->onBeforeRender = statusRender;

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
