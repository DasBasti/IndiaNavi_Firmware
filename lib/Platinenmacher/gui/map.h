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
    image_t *image;
    label_t *label;
    uint8_t z;
    uint8_t loaded;
} map_tile_t;

typedef struct
{
    uint8_t width;
    uint8_t height;
    uint8_t tile_zoom;

    map_tile_t **tiles;
    uint32_t tile_count;
    label_t *positon_marker;

    error_code_t (*onBeforeRender)(display_t *dsp, void *label);
    error_code_t (*onAfterRender)(display_t *dsp, void *label);
} map_t;

map_t *map_create(uint8_t width, uint8_t height, uint16_t tile_size);
error_code_t map_update_zoom_level(map_t *map, uint8_t level);
uint8_t map_get_zoom_level(map_t *map);
map_tile_t *map_get_tile(map_t *map, uint8_t x, uint8_t y);
error_code_t map_update_position(map_t *map, float _longitude, float _latitude);

#endif /* PLATINENMACHER_GUI_MAP_H */