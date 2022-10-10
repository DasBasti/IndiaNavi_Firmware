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
#include "waypoint.h"

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
    uint8_t satellites_in_view;
    uint8_t satellites_in_use;
} map_position_t;

typedef struct
{
    rect_t box;
    uint8_t width;
    uint8_t height;
    uint8_t tile_zoom;

    map_tile_t** tiles;
    uint32_t tile_count;
    uint16_t pos_x;
    uint16_t pos_y;

    error_code_t (*onBeforeRender)(const display_t* dsp, void* map_t);
    error_code_t (*onAfterRender)(const display_t* dsp, void* map_t);
} map_t;

map_t* map_create(int16_t left, int16_t top, uint8_t width, uint8_t height, uint16_t tile_size, font_t* font);
error_code_t map_update_zoom_level(map_t* map, uint8_t level);
uint8_t map_get_zoom_level(map_t* map);
map_tile_t* map_get_tile(map_t* map, uint8_t x, uint8_t y);
error_code_t map_update_position(map_t* map, map_position_t* pos);
error_code_t map_update_tiles(map_t* map);
error_code_t map_calculate_waypoint(map_t *map, waypoint_t* wp_t);
uint32_t map_add_waypoint(waypoint_t* wp);
void map_set_first_waypoint(waypoint_t* wp);
error_code_t map_free_waypoints();
error_code_t map_update_waypoint_path(map_t *map);
error_code_t map_run_on_waypoints(void (*function)(waypoint_t *wp));

error_code_t map_render(const display_t* dsp, void* component);
error_code_t map_tile_render(const display_t* dsp, void* component);

void map_tile_attach_onBeforeRender_callback(map_t* map, error_code_t (*cb)(const display_t* dsp, void* component));
void map_tile_attach_onAfterRender_callback(map_t* map, error_code_t (*cb)(const display_t* dsp, void* component));

#endif /* PLATINENMACHER_GUI_MAP_H */