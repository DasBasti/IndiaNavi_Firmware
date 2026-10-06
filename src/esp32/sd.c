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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "gui.h"
#include "helper.h"
#include "pins.h"
#include "tasks.h"
#include <icons_16.h>

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

static bool takeSD(void)
{
    if (waitForSDInit() != PM_OK)
        return false;
    return xSemaphoreTake(sd_semaphore, SD_MUTEX_TIMEOUT) == pdTRUE;
}

static void giveSD(void)
{
    xSemaphoreGive(sd_semaphore);
}

/*
 * Lock the SD card for direct FatFs access from other modules.
 *
 * returns false if no card is mounted or the card is busy
 */
bool sd_lock(void)
{
    return takeSD();
}

void sd_unlock(void)
{
    giveSD();
}

/*
 * Size and free space of the mounted card in bytes
 */
error_code_t sd_get_info(uint64_t* total, uint64_t* free)
{
    *total = 0;
    *free = 0;
    if (sd_status != PM_OK)
        return UNAVAILABLE;
    if (!takeSD())
        return UNAVAILABLE;
    esp_err_t err = esp_vfs_fat_info("", total, free);
    giveSD();
    return err == ESP_OK ? PM_OK : PM_FAIL;
}

/*
 * Load a whole file into memory.
 *
 * If file->dest is NULL a buffer of file size + 1 is allocated, the caller
 * has to free it. Otherwise file->dest_size bytes are available in dest and
 * the file is truncated to fit. dest is always \0 terminated.
 */
error_code_t loadFile(async_file_t* file)
{
    FRESULT res;
    FIL t_file = { 0 };
    FILINFO fno;
    UINT br = 0;

    if (!file || !file->filename)
        return PM_FAIL;
    if (file->dest && file->dest_size == 0)
        return PM_FAIL;

    ESP_LOGI(TAG, "Load %s ", file->filename);
    if (takeSD()) {
        res = f_stat(file->filename, &fno);
        if (FR_OK == res) {
            size_t to_read = fno.fsize;
            if (!file->dest) {
                file->dest = RTOS_Malloc_Large(to_read + 1);
                file->dest_size = file->dest ? to_read + 1 : 0;
            }
            if (to_read > file->dest_size - 1) {
                ESP_LOGW(TAG, "%s is larger than buffer (%lu > %u), truncated", file->filename, (unsigned long)fno.fsize, (unsigned)(file->dest_size - 1));
                to_read = file->dest_size - 1;
            }
            res = f_open(&t_file, file->filename, FA_READ);
            if (FR_OK == res && file->dest != 0) {
                res = f_read(&t_file, file->dest, to_read, &br);
                file->dest[br] = 0;
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
        giveSD();
    } else {
        ESP_LOGE(TAG, "sd semapore not available");
    }

    if (file->loaded == LOADED)
        return PM_OK;
    return PM_FAIL;
}

error_code_t fileExists(async_file_t* file)
{
    if (!takeSD())
        return PM_FAIL;
    FILINFO fno;
    FRESULT fres = f_stat(file->filename, &fno);
    giveSD();
    if (FR_OK == fres)
        return PM_OK;
    return PM_FAIL;
}

/*
 * Allocate a buffer to hold the file including a terminating \0
 */
error_code_t createFileBuffer(async_file_t* file)
{
    if (!takeSD())
        return PM_FAIL;
    FILINFO fno;
    FRESULT fres = f_stat(file->filename, &fno);
    giveSD();
    if (FR_OK == fres) {
        file->dest = RTOS_Malloc_Large(fno.fsize + 1);
        if (!file->dest)
            return PM_FAIL;
        file->dest_size = fno.fsize + 1;
        return PM_OK;
    }
    return PM_FAIL;
}

/*
 * Create all folders of the path to the file
 */
static void createPathToFile(const char* filename)
{
    size_t len = strlen(filename);
    if (len < 2)
        return;

    char* path = RTOS_Malloc(len + 1);
    char* tmp_path = RTOS_Malloc(len + 1);
    if (!path || !tmp_path)
        goto out;

    strcpy(path, filename);

    // skip first // for root
    char* strtokCtx;
    char* token = strtok_r(path + 2, "/", &strtokCtx);
    while (token != NULL) {
        strcat(tmp_path, token);
        // Create the folder in the path
        FRESULT res = f_mkdir(tmp_path);
        if (FR_OK != res && FR_EXIST != res) {
            ESP_LOGD(TAG, "Folder %s could not be created: %d", tmp_path, res);
            break;
        }
        strcat(tmp_path, "/");
        token = strtok_r(NULL, "/", &strtokCtx);

        // Check if the path has a "File extention" so we skip creating a folder for it
        if (token && strchr(token, '.'))
            break;
    }
out:
    RTOS_Free(tmp_path);
    RTOS_Free(path);
}

static error_code_t openFile(async_file_t* file, BYTE mode)
{
    if (!file || !file->filename)
        return PM_FAIL;

    if (!file->file) {
        // zeroed by RTOS_Malloc. FatFs dynamic buffers: f_open() only allocates fp->buf when it is NULL
        file->file = RTOS_Malloc(sizeof(FIL));
        if (!file->file)
            return PM_FAIL;
    }
    if (!takeSD())
        return PM_FAIL;
    // try to open file
    FRESULT res = f_open(file->file, file->filename, mode);
    if (FR_NO_PATH == res) {
        createPathToFile(file->filename);
        // Retry to open file
        res = f_open(file->file, file->filename, mode);
    }
    giveSD();

    ESP_LOGD(TAG, "File %s -> %d", file->filename, res);
    if (FR_OK == res)
        return PM_OK;
    return PM_FAIL;
}

/*
 * Create the file, or truncate it if it exists
 */
error_code_t openFileForWriting(async_file_t* file)
{
    return openFile(file, FA_WRITE | FA_CREATE_ALWAYS);
}

/*
 * Open the file for reading and writing without truncating it. The file is
 * created if it does not exist. The read/write pointer is at the start.
 */
error_code_t openFileForUpdate(async_file_t* file)
{
    return openFile(file, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
}

error_code_t seekFile(async_file_t* file, uint32_t offset)
{
    if (!file || !file->file)
        return PM_FAIL;
    if (!takeSD())
        return PM_FAIL;
    FRESULT res = f_lseek(file->file, offset);
    giveSD();
    if (FR_OK == res)
        return PM_OK;
    return PM_FAIL;
}

error_code_t readFromFile(async_file_t* file, void* out_data, uint32_t count, uint32_t* read)
{
    *read = 0;
    if (!file || !file->file)
        return PM_FAIL;
    if (!takeSD())
        return PM_FAIL;
    UINT br = 0;
    FRESULT res = f_read(file->file, out_data, count, &br);
    giveSD();
    *read = br;
    if (FR_OK == res)
        return PM_OK;
    return PM_FAIL;
}

async_file_t* createPhysicalFile()
{
    async_file_t* f = RTOS_Malloc(sizeof(async_file_t));
    if (!f)
        return NULL;
    // zeroed by RTOS_Malloc
    f->file = RTOS_Malloc(sizeof(FIL));
    if (!f->file) {
        RTOS_Free(f);
        return NULL;
    }
    return f;
}

error_code_t writeToFile(async_file_t* file, void* in_data, uint32_t count, uint32_t* written)
{
    *written = 0;
    if (!file || !file->file)
        return PM_FAIL;
    if (!takeSD())
        return PM_FAIL;
    UINT bw = 0;
    FRESULT res = f_write(file->file, in_data, count, &bw);
    if (FR_OK == res)
        res = f_sync(file->file);
    giveSD();
    *written = bw;
    if (FR_OK == res)
        return PM_OK;
    return PM_FAIL;
}

error_code_t closeFile(async_file_t* file)
{
    if (!file || !file->file)
        return PM_FAIL;
    if (!takeSD())
        return PM_FAIL;
    FRESULT res = f_close(file->file);
    giveSD();
    if (FR_OK == res)
        return PM_OK;
    return PM_FAIL;
}

error_code_t deleteFile(async_file_t* file)
{
    if (!takeSD())
        return PM_FAIL;
    FRESULT res = f_unlink(file->filename);
    giveSD();
    if (FR_OK == res)
        return PM_OK;
    return PM_FAIL;
}

/*
 * Close file if still open and free all memory of the file
 */
void closePhysicalFile(async_file_t* file)
{
    if (file) {
        if (file->file) {
            if (file->file->obj.fs) {
                ESP_LOGI(TAG, "File: %lu is still open, close it", (unsigned long)file->file->fptr);
                closeFile(file);
            }
            RTOS_Free(file->file);
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
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "init semaphore");
    xSemaphoreTake(mutex, portMAX_DELAY); // block SD mutex until card is mounted
    // publish the mutex only after we own it
    sd_semaphore = mutex;
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
                // the SD task holds the mutex while no card is mounted
                esp_err_t ret = esp_vfs_fat_sdmmc_mount("", &host, &slot_config, &mount_config, &card);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "SDC: init done");
                    sd_status = PM_OK;
                    xSemaphoreGive(sd_semaphore);
                    trigger_rendering();
                } else {
                    if (ret == ESP_FAIL) {
                        ESP_LOGE(TAG, "Failed to mount filesystem.");
                    } else {
                        ESP_LOGE(TAG, "Failed to initialize the card (0x%x).", ret);
                    }
                    sd_status = PM_FAIL;
                    // keep holding the mutex and retry later
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
            }
        } else {
            if (sd_status == PM_OK) {
                ESP_LOGE("SD", "Unmount filesystem.");
                xSemaphoreTake(sd_semaphore, portMAX_DELAY);
                sd_status = UNAVAILABLE;
                // deinit SDMMC periphery
                esp_vfs_fat_sdcard_unmount("", card);
                // show on gui
                trigger_rendering();
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                sd_status = UNAVAILABLE;
            }
        }

        if (sd_indicator_label)
            sd_indicator_label->onBeforeRender = statusRender;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
