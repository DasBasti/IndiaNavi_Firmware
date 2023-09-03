/*
 * Map component callback functions for Linux version
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui/map.h"
#include "tasks.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#if USE_CURL
#    include <curl/curl.h>
#    include <curl/easy.h>
#endif

static const char* TAG = "GUI_MAP";
char* path_prefix = "tiles/raw";

void set_path_prefix(char* prefix)
{
    path_prefix = prefix;
}

size_t load_data(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    image_t* img = (image_t*)stream;
    ESP_LOGI(__func__, "Loaded: %lu start @ %p", size * nmemb, img->data);
    memcpy(img->data, ptr, size * nmemb);
    return size;
}

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
    label_t* l = (label_t*)img->child;

    img->data = imageBuf;
    map_tile_t* tile = img->parent; // the parent component of the image is the tile

    uint32_t br;
    // TODO: decompress lz4 tiles
    save_sprintf(fn, "%s/%u/%u/%u.raw",
        path_prefix,
        tile->z,
        tile->x,
        tile->y);
    ESP_LOGI(TAG, "Load %s  to %p", fn, tile->image->data);

#if USE_CURL
    CURL* curl = curl_easy_init();
    CURLcode res;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, fn);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, load_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, tile->image);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }

#else
    fd = open(fn, O_RDONLY);
    if (-1 != fd && tile->image->data != 0) {
        ssize_t count;
        count = read(fd, tile->image->data, 32768); // Tilesize 256*256/2 bytes
        ESP_LOGI(TAG, "Load %ld bytes", count);
        if (count == 32768) {
            tile->image->loaded = LOADED;
            l->text = NULL;
        }
        close(fd);
    } else {
        tile->image->loaded = NOT_FOUND;
    }
#endif
    if (img->loaded == LOADED)
        return PM_OK;

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