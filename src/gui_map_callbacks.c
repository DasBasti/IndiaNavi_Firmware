/*
 * Map component callback functions
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui/map.h"
#include "tasks.h"
#include "esp_log.h"

static const char* TAG = "GUI_MAP";

/*
 * Load tile data from SD Card on render command
 *
 * We need a memory buffer since it is not enough memory available
 * to hold all 6 tiles in memory at once.
 * 
 * returns PK_OK if loaded, UNAVAILABLE if buffer or sd semaphore is not available and
 * TIMEOUT if loading timed out
 */
error_code_t load_map_tile_on_demand(const display_t *dsp, void *image)
{
	// TODO: move to GUI
	uint8_t timeout = 0;
	uint8_t *imageBuf = RTOS_Malloc(256 * 256 / 2);
	ESP_LOGI(TAG, "Image at: 0x%X", (uint)imageBuf);
	if (!imageBuf)
		return UNAVAILABLE;

	image_t *img = (image_t *)image;
	if (!uxSemaphoreGetCount(sd_semaphore))
	{ // binary semaphore returns 1 on not taken
		RTOS_Free(imageBuf);
		return UNAVAILABLE;
	}

	img->data = imageBuf;
	loadTile(img->parent); // queue loading

	while (img->loaded != LOADED)
	{
		label_t *l = (label_t *)img->child;
		if (img->loaded == ERROR)
		{
			l->text = "Error";
			goto freeImageMemory;
		}
		if (img->loaded == NOT_FOUND)
		{
			//l->text = "Not Found";
			goto freeImageMemory;
		}
		vTaskDelay(10);
		timeout++;
		if (timeout == 100)
		{
			ESP_LOGI(TAG, "load timeout!");
			goto freeImageMemory;
		}
	}
	return PM_OK;

freeImageMemory:
	RTOS_Free(imageBuf);
	return TIMEOUT;
}

error_code_t check_if_map_tile_is_loaded(const display_t *dsp, void *image)
{
	// TODO: move to map component
	image_t *img = (image_t *)image;
	label_t *lbl = img->child;
	if (img->loaded != LOADED)
	{
		label_render(dsp, lbl);
	}
	else
	{
		RTOS_Free(img->data);
	}
	return PM_OK;
}