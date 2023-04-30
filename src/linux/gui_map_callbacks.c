/*
 * Map component callback functions for Linux version
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui/map.h"
#include "tasks.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

static const char* TAG = "GUI_MAP";

/*
 * Load tile data from SD Card on render command
 *
 * We load the image into the tile if it is not already the correct image
 */
error_code_t load_map_tile_on_demand(const display_t* dsp, void* image)
{
    char fn[255]; // Filename size for zoom level 16.
    int fd;
    uint8_t* imageBuf = RTOS_Malloc(256 * 256 / 2);

    if (!imageBuf)
        return UNAVAILABLE;

    image_t* img = (image_t*)image;

    img->data = imageBuf;
    map_tile_t* tile = img->parent; // the parent component of the image is the tile
    
    uint32_t br;
    // TODO: decompress lz4 tiles
    save_sprintf(fn, "tiles/raw/%u/%u/%u.raw",
        tile->z,
        tile->x,
        tile->y);
    ESP_LOGI(TAG, "Load %s  to %p", fn, tile->image->data);

        fd = open(fn, O_RDONLY);
        if (-1 != fd && tile->image->data != 0) {
            ssize_t count;
            count = read(fd, tile->image->data, 32768); // Tilesize 256*256/2 bytes
            ESP_LOGI(TAG, "Load %ld bytes", count);
            if (count == 32768) {
                tile->image->loaded = LOADED;
            }
            close(fd);
        } else {
            tile->image->loaded = NOT_FOUND;
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