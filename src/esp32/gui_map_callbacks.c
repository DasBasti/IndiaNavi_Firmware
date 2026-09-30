/*
 * Map component callback functions
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "esp_log.h"
#include "gui.h"
#include "gui/map.h"
#include "tasks.h"

static const char* TAG = "GUI_MAP";

#define TILE_DATA_SIZE (256 * 256 / 2) // 4 bit per pixel

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
    char fn[40];
    FRESULT res = FR_NOT_READY;
    image_t* img = (image_t*)image;
    map_tile_t* tile = img->parent; // the parent component of the image is the tile
    label_t* l = (label_t*)img->child;

    // never keep a pointer to memory that is freed below
    RTOS_Free(img->data);
    img->data = NULL;

    if (!sd_semaphore || !uxSemaphoreGetCount(sd_semaphore)) { // mutex returns 1 if not taken
        return UNAVAILABLE;
    }

    uint8_t* imageBuf = RTOS_Malloc(TILE_DATA_SIZE);
    if (!imageBuf)
        return UNAVAILABLE;

    FIL t_img = { 0 };
    UINT br = 0;
    // TODO: decompress lz4 tiles
    snprintf(fn, sizeof(fn), "//MAPS/%u/%lu/%lu.RAW",
        tile->z,
        tile->x,
        tile->y);
    ESP_LOGI(TAG, "Load %s  to %p", fn, imageBuf);

    img->loaded = NOT_LOADED;
    if (waitForSDInit() == PM_OK && xSemaphoreTake(sd_semaphore, SD_MUTEX_TIMEOUT)) {
        res = f_open(&t_img, fn, FA_READ);
        if (FR_OK == res) {
            res = f_read(&t_img, imageBuf, TILE_DATA_SIZE, &br);
            if (FR_OK == res && br == TILE_DATA_SIZE) {
                img->loaded = LOADED;
            } else {
                ESP_LOGI(TAG, "Error reading tile: %d (%u bytes)", res, br);
                img->loaded = ERROR;
            }
            f_close(&t_img);
        } else {
            ESP_LOGI(TAG, "Error from SD card: %d", res);
            img->loaded = NOT_FOUND;
        }
        xSemaphoreGive(sd_semaphore);
    } else {
        ESP_LOGI(TAG, "load timeout!");
    }

    if (img->loaded == LOADED) {
        img->data = imageBuf;
        if (l)
            l->text = "";
        return PM_OK;
    }

    if (l) {
        if (img->loaded == ERROR)
            l->text = "Error";
        if (img->loaded == NOT_FOUND)
            l->text = "Not Found";
    }

    RTOS_Free(imageBuf);
    return TIMEOUT;
}

error_code_t load_map_tiles_to_permanent_memory(const display_t* dsp, void* _map)
{
    char fn[40];
    FRESULT res = FR_NOT_READY;

    map_t* map = (map_t*)_map;

    if (!sd_semaphore || !uxSemaphoreGetCount(sd_semaphore)) { // mutex returns 1 if not taken
        return UNAVAILABLE;
    }

    for (size_t i = 0; i < map->tile_count; i++) {
        map_tile_t* tile = map->tiles[i];
        FIL t_img = { 0 };
        FILINFO t_img_nfo;
        UINT br = 0;

        if (tile->image->loaded == LOADED) {
            continue;
        }

        snprintf(fn, sizeof(fn), "//MAPS/%u/%lu/%lu.RAW",
            tile->z,
            tile->x,
            tile->y);
        // TODO: decompress lz4 tiles
        if (waitForSDInit() != PM_OK || !xSemaphoreTake(sd_semaphore, SD_MUTEX_TIMEOUT)) {
            ESP_LOGI(TAG, "load timeout!");
            continue;
        }

        // Check file info
        res = f_stat(fn, &t_img_nfo);
        if (FR_OK != res) {
            ESP_LOGI(TAG, "Error from SD card f_stat: %d", res);
            tile->image->loaded = NOT_FOUND;
        } else if (t_img_nfo.fsize < TILE_DATA_SIZE) {
            // the renderer reads a full tile, do not use smaller files
            ESP_LOGI(TAG, "Tile %s too small: %lu", fn, (unsigned long)t_img_nfo.fsize);
            tile->image->loaded = ERROR;
        } else {
            // Allocate tile memory if we need to
            if (!tile->image->data || tile->image->data_length != TILE_DATA_SIZE) {
                RTOS_Free(tile->image->data); // throw away old memory
                tile->image->data_length = 0;
                if ((tile->image->data = RTOS_Malloc(TILE_DATA_SIZE)))
                    tile->image->data_length = TILE_DATA_SIZE;
            }
            // open file and load image data
            res = f_open(&t_img, fn, FA_READ);
            ESP_LOGI(TAG, "Load %s to %p", fn, tile->image->data);
            if (FR_OK == res && tile->image->data != 0) {
                res = f_read(&t_img, tile->image->data, TILE_DATA_SIZE, &br);
                if (FR_OK == res && br == TILE_DATA_SIZE) {
                    tile->image->loaded = LOADED;
                    tile->label->text = "";
                } else {
                    ESP_LOGI(TAG, "Error from SD card f_read: %d", res);
                    tile->image->loaded = ERROR;
                }
                f_close(&t_img);
            } else {
                ESP_LOGI(TAG, "Error from SD card f_open: %d", res);
                if (FR_OK == res)
                    f_close(&t_img);
                tile->image->loaded = NOT_FOUND;
            }
        }
        xSemaphoreGive(sd_semaphore);

        if (tile->image->loaded != LOADED) {
            // do not render stale data of the previous tile
            RTOS_Free(tile->image->data);
            tile->image->data = NULL;
            tile->image->data_length = 0;
        }
        if (tile->image->loaded == ERROR) {
            tile->label->text = "Error";
        }
        if (tile->image->loaded == NOT_FOUND) {
            tile->label->text = "Not Found";
        }
    }

    return PM_OK;
}

error_code_t check_if_map_tile_is_loaded(const display_t* dsp, void* image)
{
    image_t* img = (image_t*)image;
    if (img->loaded == LOADED) {
        img->loaded = NOT_LOADED;
    }
    RTOS_Free(img->data);
    img->data = NULL;
    return PM_OK;
}

error_code_t map_render_copyright(const display_t* dsp, void* label)
{
    label_t* l = (label_t*)label;
    if (map_position && map_position->fix) {
        // TODO: get this information from map info on SD card!
        l->text = "(c) OpenStreetMap contributors";
        label_shrink_to_text(l);
        l->box.left = dsp->size.width - l->box.width;
    }
    return PM_OK;
}