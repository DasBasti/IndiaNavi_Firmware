/*
 * Map component for loading tile images according to a position
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PLATINENMACHER_GUI_MAP_H
#define PLATINENMACHER_GUI_MAP_H

#include "display.h"
#include "image.h"
#include "label.h"
#include "memory.h"

typedef struct
{
    uint32_t x;
    uint32_t y;
    image_t* image;
    label_t* label;
    uint8_t z;
    uint8_t loaded;
} map_tile_t;

typedef struct {
    float longitude;
    float latitude;
    float altitude;
    float hdop;
    uint8_t zoom_level;
    uint8_t fix;
} map_position_t;

typedef struct
{
    rect_t box;
    uint8_t width;
    uint8_t height;
    uint8_t tile_zoom;

    map_tile_t** tiles;
    uint32_t tile_count;
    label_t* positon_marker;

    error_code_t (*onBeforeRender)(const display_t* dsp, void* map_t);
    error_code_t (*onAfterRender)(const display_t* dsp, void* map_t);
} map_t;

map_t* map_create(int16_t top, int16_t left, uint8_t width, uint8_t height, uint16_t tile_size);
error_code_t map_update_zoom_level(map_t* map, uint8_t level);
uint8_t map_get_zoom_level(map_t* map);
map_tile_t* map_get_tile(map_t* map, uint8_t x, uint8_t y);
error_code_t map_update_position(map_t* map, map_position_t pos);
error_code_t map_update_tiles(map_t* map);

error_code_t map_render(const display_t* dsp, void* component);
error_code_t map_tile_render(const display_t* dsp, void* component);

void map_tile_attach_onBeforeRender_callback(map_t* map, error_code_t (*cb)(const display_t* dsp, void* component));
void map_tile_attach_onAfterRender_callback(map_t* map, error_code_t (*cb)(const display_t* dsp, void* component));

#endif /* PLATINENMACHER_GUI_MAP_H */