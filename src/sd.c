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

#include "icons_32/icons_32.h"
#include "gui.h"
#include "pins.h"
#include "tasks.h"

uint8_t sd_status = UNAVAILABLE;
FRESULT res;
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

error_code_t statusRender(display_t *dsp, void *comp)
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
 * Read one line from string
 */
char *readline(char *c, char *d)
{
	while (c)
	{
		if (*c == '\n')
			break;
		if (*c == '\r')
			continue;
		*d = *c;
		c++;
		d++;
	}
	return ++c;
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

void StartSDTask(void const *argument)
{
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

	ESP_LOGI(TAG, "SDC: wait for indicator");

	while (!sd_indicator_label)
		vTaskDelay(1);
	sd_indicator_label->onBeforeRender = statusRender;

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
						ESP_LOGE("SD", "Failed to mount filesystem.");
					}
					else
					{
						ESP_LOGE("SD", "Failed to initialize the card (0x%x).", ret);
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
					save_sprintf(fn, "//MAPS/%d/%d/%d.RAW",
								 tile->z,
								 tile->x,
								 tile->y);
					ESP_LOGI(TAG, "Load %s  to %d", fn, (uint)tile->image->data);

					res = f_open(&t_img, fn, FA_READ);
					if (FR_OK == res && tile->image->data != 0)
					{
						res = f_read(&t_img,
									 tile->image->data, 32768,
									 &br); // Tilesize 256*256/2 bytes
						if (FR_OK == res)
						{
							tile->image->loaded = true;
						}
						f_close(&t_img);
					}
					else
					{
						save_sprintf(tile->label->text,
									 "%d/%d not found.", tile->x,
									 tile->y);
					}
					cnt++;
				}
				async_file_t *file;
				while (pdTRUE == xQueueReceive(fileLoadQueueHandle, &file, 0))
				{
					FIL t_file;
					uint32_t br;
					ESP_LOGI(TAG, "Load %s ", file->filename);

					res = f_open(&t_file, file->filename, FA_READ);
					if (FR_OK == res && file->dest != 0)
					{
						res = f_read(&t_file,
									 file->dest, 32768,
									 &br);
						if (FR_OK == res)
						{
							file->loaded = true;
						}
						f_close(&t_file);
					}
					else
					{
						save_sprintf(tile->label->text,
									 "%d/%d not found.", tile->x,
									 tile->y);
					}
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
				xSemaphoreTake(sd_semaphore, portMAX_DELAY);
				sd_status = UNAVAILABLE;
				// deinit SDMMC periphery
				esp_vfs_fat_sdmmc_unmount();
				// show on gui
				trigger_rendering();
			}
		}

		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}
