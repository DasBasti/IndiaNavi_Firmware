/*
 * sd.c
 *
 *  Created on: Jan 9, 2021
 *      Author: bastian
 */

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_vfs_fat.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_defs.h>
#include <Platinenmacher.h>
#include "helper.h"

#include "icons_32/icons_32.h"
#include "gui.h"
#include "pins.h"
#include "tasks.h"

uint8_t sd_status = UNAVAILABLE;
char fn[30];
sdmmc_card_t *card;

static const char *TAG = "SD";

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
	.command_timeout_ms = 0};

static const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
	.format_if_mount_failed = false,
	.max_files = 1,
	.allocation_unit_size = 0};

error_code_t statusRender(const display_t *dsp, void *comp)
{
	image_t *icon = sd_indicator_label->child;
	if (sd_status == OK)
		icon->data = SD; // show SD card symbol
	else
		icon->data = noSD; // show SD card symbol

	return OK;
}

/* 
 * Queue the tile for loading. Tasks handles Queue
 */
error_code_t loadTile(map_tile_t *tile)
{
	tile->image->loaded = 0;
	if (xQueueSend(mapLoadQueueHandle, &tile, 0) == pdTRUE)
		return PM_OK;
	return PM_FAIL;
}

/*
 * Queue arbitrary file reads.
 */
error_code_t loadFile(async_file_t *file)
{
	if (xQueueSend(fileLoadQueueHandle, &file, 0) == pdTRUE)
		return PM_OK;
	return PM_FAIL;
}

error_code_t fileExists(async_file_t *file)
{
	xSemaphoreTake(sd_semaphore, portMAX_DELAY);
	FILINFO fno;
	FRESULT fres = f_stat(file->filename, &fno);
	xSemaphoreGive(sd_semaphore);
	if (FR_OK == fres)
		return PM_OK;
	return PM_FAIL;
}

error_code_t openFileForWriting(async_file_t *file)
{
	// try to open file
	FRESULT res = f_open(file->file, file->filename, FA_WRITE | FA_CREATE_NEW);
	if (FR_NO_PATH == res)
	{
		// 1. try to create path to file if a path is given
		if (strlen(file->filename) < 2)
			return PM_FAIL;

		char *path = RTOS_Malloc(strlen(file->filename));
		char *tmp_path = RTOS_Malloc(strlen(file->filename));

		// 2. skip first // for root
		strcpy(path, file->filename);
		path += 2;
		//strcat(tmp_path, "/");

		// 3. loop through folders
		char *strtokCtx;
		char *token = strtok_r(path, "/", &strtokCtx);
		while (token != NULL)
		{
			strcat(tmp_path, token);
			// Create the folder in the path
			res = f_mkdir(tmp_path);
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

async_file_t *createPhysicalFile()
{
	async_file_t *f = RTOS_Malloc(sizeof(async_file_t));
	f->file = RTOS_Malloc(sizeof(FIL));
	return f;
}

error_code_t writeToFile(async_file_t *file, void *in_data, uint32_t count, uint32_t *written)
{
	FRESULT res = f_write(file->file, in_data, count, written);
	if (FR_OK == res)
		return PM_OK;
	return PM_FAIL;
}

error_code_t closeFile(async_file_t *file)
{
	FRESULT res = f_close(file->file);
	if (FR_OK == res)
		return PM_OK;
	return PM_FAIL;
}

void closePhysicalFile(async_file_t *file)
{
	if (file)
	{
		if (file->file)
		{
			if (file->file->fptr)
				ESP_LOGI(TAG, "File: %d is still open", file->file->fptr);
			//f_close(file->file);
			//RTOS_Free(file->file);
		}
		if (file->dest)
		{
			ESP_LOGI(TAG, "Free file->dest");
			RTOS_Free(file->dest);
		}
		RTOS_Free(file);
	}
}

void StartSDTask(void const *argument)
{
	FRESULT res = FR_NOT_READY;
	xSemaphoreTake(sd_semaphore, portMAX_DELAY); // block SD mutex
	ESP_LOGI(TAG, "init gpio %d", SD_VCC_nEN);

	/* create power regulator */
	gpio_t *reg_gpio = gpio_create(OUTPUT, 0, SD_VCC_nEN);
	reg_gpio->onValue = GPIO_RESET;
	regulator_t *reg = regulator_gpio_create(reg_gpio);
	reg->disable(reg);
	vTaskDelay(100);
	reg->enable(reg);

	gpio_t *dc_dt = gpio_create(INPUT, 0, SD_CARD_nDET);
	gpio_set_pull_mode(SD_CARD_nDET, GPIO_PULLUP_ENABLE);

	/* initialize SD card */
	vTaskDelay(100 / portTICK_PERIOD_MS);

	for (;;)
	{
		if (!gpio_read(dc_dt))
		{
			if (sd_status != OK)
			{
				sd_status = OK;
				esp_err_t ret = esp_vfs_fat_sdmmc_mount("", &host, &slot_config, &mount_config, &card);
				if (ret != ESP_OK)
				{
					if (ret == ESP_FAIL)
					{
						ESP_LOGE(TAG, "Failed to mount filesystem.");
					}
					else
					{
						ESP_LOGE(TAG, "Failed to initialize the card (0x%x).", ret);
						ESP_LOGE("mem", "free: %d", xPortGetFreeHeapSize());
					}
					sd_status = !OK;
				}
				// Card has been initialized, print its properties
				ESP_LOGI(TAG, "SDC: init done");
				xSemaphoreGive(sd_semaphore);
				trigger_rendering();
			}
			else
			{
				uint8_t cnt = 0;
				map_tile_t *tile;
				while (pdTRUE == xQueueReceive(mapLoadQueueHandle, &tile, 0))
				{
					FIL t_img;
					uint32_t br;
					// TODO: decompress lz4 tiles
					save_sprintf(fn, "//MAPS/%u/%u/%u.RAW",
								 tile->z,
								 tile->x,
								 tile->y);
					ESP_LOGI(TAG, "Load %s  to %u", fn, (uint)tile->image->data);

					xSemaphoreTake(sd_semaphore, portMAX_DELAY);
					res = f_open(&t_img, fn, FA_READ);
					if (FR_OK == res && tile->image->data != 0)
					{
						res = f_read(&t_img,
									 tile->image->data, 32768,
									 &br); // Tilesize 256*256/2 bytes
						if (FR_OK == res)
						{
							tile->image->loaded = LOADED;
						}
						f_close(&t_img);
					}
					else
					{
						tile->image->loaded = NOT_FOUND;
						/*save_sprintf(tile->label->text,
									 "%d/%d not found.", tile->x,
									 tile->y);*/
					}
					xSemaphoreGive(sd_semaphore);
					cnt++;
				}
				async_file_t *file;
				while (pdTRUE == xQueueReceive(fileLoadQueueHandle, &file, 0))
				{
					FIL t_file;
					uint32_t br;
					ESP_LOGI(TAG, "Load %s ", file->filename);
					xSemaphoreTake(sd_semaphore, portMAX_DELAY);
					res = f_open(&t_file, file->filename, FA_READ);
					if (FR_OK == res && file->dest != 0)
					{
						res = f_read(&t_file,
									 file->dest, 32768,
									 &br);
						if (FR_OK == res)
						{
							//ESP_LOGI(TAG, "--- %s", file->dest);
							file->loaded = true;
						}
						f_close(&t_file);
					}
					else
					{
						ESP_LOGI(TAG, "%s not found.", file->filename);
					}
					xSemaphoreGive(sd_semaphore);
				}

				if (cnt)
				{ // we loaded at least one tile. so rerender the GUI
					trigger_rendering();
				}
			}
		}
		else
		{
			if (sd_status == OK)
			{
				ESP_LOGE("SD", "Unmount filesystem.");

				xSemaphoreTake(sd_semaphore, portMAX_DELAY);
				sd_status = UNAVAILABLE;
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
