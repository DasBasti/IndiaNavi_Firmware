/*
 * Map component callback functions
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "esp_log.h"
#include "gui/map.h"
#include "tasks.h"

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
error_code_t load_map_tile_on_demand(const display_t* dsp, void* image)
{
    char fn[30]; // Filename size for zoom level 16.
    FRESULT res = FR_NOT_READY;
    uint8_t* imageBuf = RTOS_Malloc(256 * 256 / 2);

    if (!imageBuf)
        return UNAVAILABLE;

    image_t* img = (image_t*)image;
    if (!uxSemaphoreGetCount(sd_semaphore)) { // binary semaphore returns 1 on not taken
        RTOS_Free(imageBuf);
        return UNAVAILABLE;
    }

    img->data = imageBuf;
    map_tile_t* tile = img->parent; // the parent component of the image is the tile
    FIL t_img;
    uint32_t br;
    // TODO: decompress lz4 tiles
    save_sprintf(fn, "//MAPS/%u/%lu/%lu.RAW",
        tile->z,
        tile->x,
        tile->y);
    ESP_LOGI(TAG, "Load %s  to %p", fn, tile->image->data);

    if (xSemaphoreTake(sd_semaphore, pdTICKS_TO_MS(1000))) {

        res = f_open(&t_img, fn, FA_READ);
        if (FR_OK == res && tile->image->data != 0) {
            res = f_read(&t_img,
                tile->image->data, 32768,
                (UINT*)&br); // Tilesize 256*256/2 bytes
            if (FR_OK == res) {
                tile->image->loaded = LOADED;
            }
            f_close(&t_img);
        } else {
            ESP_LOGI(TAG, "Error from SD card: %d", res);
            tile->image->loaded = NOT_FOUND;
        }
        xSemaphoreGive(sd_semaphore);
    } else {
        ESP_LOGI(TAG, "load timeout!");
        goto freeImageMemory;
    }

    if (img->loaded == LOADED)
        return PM_OK;

    label_t* l = (label_t*)img->child;
    if (img->loaded == ERROR) {
        l->text = "Error";
        goto freeImageMemory;
    }
    if (img->loaded == NOT_FOUND) {
        l->text = "Not Found";
        goto freeImageMemory;
    }

freeImageMemory:
    RTOS_Free(imageBuf);
    return TIMEOUT;
}

error_code_t check_if_map_tile_is_loaded(const display_t* dsp, void* image)
{
    image_t* img = (image_t*)image;
    if (img->loaded == LOADED) {
        img->loaded = NOT_LOADED;
        RTOS_Free(img->data);
    }
    return PM_OK;
}
