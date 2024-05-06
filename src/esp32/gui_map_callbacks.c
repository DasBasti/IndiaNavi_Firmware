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

    waitForSDInit();
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

    label_t* l = (label_t*)img->child;
    if (img->loaded == LOADED) {
        l->text = "";
        return PM_OK;
    }

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

error_code_t load_map_tiles_to_permanent_memory(const display_t* dsp, void* _map)
{
    char fn[30]; // Filename size for zoom level 16.
    FRESULT res = FR_NOT_READY;

    map_t* map = (map_t*)_map;

    if (!uxSemaphoreGetCount(sd_semaphore)) { // binary semaphore returns 1 on not taken
        return UNAVAILABLE;
    }

    for (size_t i = 0; i < map->tile_count; i++) {
        map_tile_t* tile = map->tiles[i];
        FIL t_img;
        FILINFO t_img_nfo;
        uint32_t br;

        if (tile->image->loaded == LOADED) {
            continue;
        }

        save_sprintf(fn, "//MAPS/%u/%lu/%lu.RAW",
            tile->z,
            tile->x,
            tile->y);
        // TODO: decompress lz4 tiles
        waitForSDInit();
        if (xSemaphoreTake(sd_semaphore, pdTICKS_TO_MS(1000))) {
            // Check file info
            res = f_stat((const TCHAR*)&fn, &t_img_nfo);
            if (FR_OK == res) {
                // Allocate file size if we need to
                if (!tile->image->data || tile->image->data_length != t_img_nfo.fsize) {
                    if (tile->image->data)        // throw away old memory
                        RTOS_Free(tile->image->data);
                    tile->image->data_length = 0; // reset length
                    if ((tile->image->data = RTOS_Malloc(t_img_nfo.fsize)))
                        tile->image->data_length = t_img_nfo.fsize;
                }
            } else {
                ESP_LOGI(TAG, "Error from SD card f_stat: %d", res);
                continue;
            }
            // open file and load image data
            res = f_open(&t_img, fn, FA_READ);
            ESP_LOGI(TAG, "Load %s to %p", fn, tile->image->data);
            if (FR_OK == res && tile->image->data != 0) {
                res = f_read(&t_img,
                    tile->image->data, t_img_nfo.fsize,
                    (UINT*)&br);
                if (FR_OK == res) {
                    tile->image->loaded = LOADED;
                } else {
                    ESP_LOGI(TAG, "Error from SD card f_read: %d", res);
                }
                f_close(&t_img);
            } else {
                ESP_LOGI(TAG, "Error from SD card f_open: %d", res);
                tile->image->loaded = NOT_FOUND;
            }
            xSemaphoreGive(sd_semaphore);
        } else {
            ESP_LOGI(TAG, "load timeout!");
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
        RTOS_Free(img->data);
    }
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