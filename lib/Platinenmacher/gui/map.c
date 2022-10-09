/*
 * Map component for loading tile images according to a position
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */
#include "map.h"
#include "waypoint.h"
#include <math.h>

static uint8_t right_side = 0;
static font_t* map_font;
static const char* not_loaded_string = "no tile loaded";

static waypoint_t* waypoints = NULL;
static waypoint_t* prev_wp = NULL;

static float flon2tile(float lon, uint8_t zoom)
{
    return ((lon + 180) / 360) * pow(2, zoom);
}

static float flat2tile(float lat, uint8_t zoom)
{
    return ((1 - log(tan((lat * M_PI) / 180) + 1 / cos((lat * M_PI) / 180)) / M_PI) / 2) * pow(2, zoom);
}

static map_tile_t* tile_create(int16_t left, int16_t top, uint16_t tile_size)
{
    map_tile_t* tile = RTOS_Malloc(sizeof(map_tile_t));
    tile->image = image_create(0, left, top, tile_size, tile_size);
    tile->label = label_create(not_loaded_string, map_font, left, top, tile_size, tile_size);
    tile->image->parent = tile;
    tile->image->child = tile->label;
    tile->label->child = tile->image;
    tile->label->alignHorizontal = CENTER;
    tile->label->alignVertical = MIDDLE;
    return tile;
}

map_t* map_create(int16_t left, int16_t top, uint8_t width, uint8_t height, uint16_t tile_size, font_t* font)
{
    if (width == 0 || height == 0)
        return NULL;

    map_t* map = RTOS_Malloc(sizeof(map_t));
    map->width = width;
    map->height = height;
    map->box.left = left;
    map->box.top = top;
    map->box.height = height * tile_size;
    map->box.width = width * tile_size;
    map->tile_count = width * height;
    map->tiles = RTOS_Malloc(sizeof(map_tile_t*) * map->tile_count);
    map_font = font;
    for (uint32_t x = 0; x < width; x++)
        for (uint32_t y = 0; y < height; y++) {
            uint32_t idx = (x * height) + y;
            map->tiles[idx] = tile_create((x * tile_size) + left, (y * tile_size) + top, tile_size);
            map->tiles[idx]->image->parent = map->tiles[idx];
            map->tiles[idx]->x = x;
            map->tiles[idx]->y = y;
        }
    return map;
}

error_code_t map_update_zoom_level(map_t* map, uint8_t level)
{
    // TODO: mechanism to check wheter the zoom level is available
    map->tile_zoom = level;
    return PM_OK;
}

uint8_t map_get_zoom_level(map_t* map)
{
    return map->tile_zoom;
}

map_tile_t* map_get_tile(map_t* map, uint8_t x, uint8_t y)
{
    if (x > map->width || y > map->height)
        return NULL;

    return map->tiles[x * map->height + y];
}

error_code_t map_update_position(map_t* map, map_position_t* pos)
{
    uint16_t x = 0, y = 0, x_old = 0, y_old = 0;
    float xf = 0.0, yf = 0.0;
    // TODO: test for matching zoom_level
    // get tile number of tile with position on it as float and integer
    if (pos->longitude != 0.0) {
        xf = flon2tile(pos->longitude, map->tile_zoom);
        x = floor(xf);
    }
    // also for y axis
    if (pos->latitude != 0.0) {
        yf = flat2tile(pos->latitude, map->tile_zoom);
        y = floor(yf);
    }
    // get offset to tile corner of tile with position
    uint32_t pos_x = floor((xf - x) * 256); // offset from tile 0
    uint32_t pos_y = floor((yf - y) * 256); // offset from tile 0
    /* grab necessary tiles */
    if ((x != x_old) | (y != y_old)) {
        x_old = x;
        y_old = y;
        for (uint8_t i = 0; i < map->width; i++) {
            for (uint8_t j = 0; j < map->height; j++) {
                uint32_t idx = i * map->height + j;
                if (pos_x < 128) {
                    map->tiles[idx]->x = x - 1 + i;
                    right_side = 1;
                } else {
                    map->tiles[idx]->x = x + i;
                    right_side = 0;
                }
                map->tiles[idx]->y = y + (-1 + j);
                map->tiles[idx]->z = map->tile_zoom;
            }
        }
    }

    pos_y += 256;
    if (right_side)
        pos_x += 256;

    map->pos_x = pos_x;
    map->pos_y = pos_y;

    return PM_OK;
}

void map_tile_attach_onBeforeRender_callback(map_t* map, error_code_t (*cb)(const display_t* dsp, void* component))
{
    for (uint32_t i = 0; i < map->tile_count; i++) {
        map->tiles[i]->image->onBeforeRender = cb;
    }
}

void map_tile_attach_onAfterRender_callback(map_t* map, error_code_t (*cb)(const display_t* dsp, void* component))
{
    for (uint32_t i = 0; i < map->tile_count; i++) {
        map->tiles[i]->image->onAfterRender = cb;
    }
}

error_code_t map_tile_render(const display_t* dsp, void* component)
{
    map_tile_t* tile = (map_tile_t*)component;
    if (tile->image && image_render(dsp, tile->image) == PM_OK && tile->image->loaded == LOADED) {
        return PM_OK;
    }

    return label_render(dsp, tile->label);
}

error_code_t map_render(const display_t* dsp, void* component)
{
    // Render map at position box
    map_t* map = (map_t*)component;
    for (uint32_t i = 0; i < map->tile_count; i++) {
        map_tile_render(dsp, map->tiles[i]);
    }
    return PM_OK;
}

error_code_t map_calculate_waypoint(map_t* map, waypoint_t* wp_t)
{
    float _xf, _yf;
    _xf = flon2tile(wp_t->lon, map->tile_zoom);
    wp_t->tile_x = floor(_xf);
    _yf = flat2tile(wp_t->lat, map->tile_zoom);
    wp_t->tile_y = floor(_yf);

    //TODO: merge this calculation with the active calculation
    for (uint32_t i = 0; i < map->tile_count; i++) {
        if (map->tiles[i]->x == wp_t->tile_x && map->tiles[i]->y == wp_t->tile_y) {

            uint16_t ty = i % map->width;
            uint16_t tx = (i - ty) / map->height;

            wp_t->pos_x = floor((_xf - wp_t->tile_x + tx) * 256); // offset from tile 0
            wp_t->pos_y = floor((_yf - wp_t->tile_y + ty) * 256); // offset from tile 0
        }
    }
    ESP_LOGI("MAP", "waypoints %d %d", wp_t->num, wp_t->tile_x);

    return PM_OK;
}

void map_set_first_waypoint(waypoint_t* wp)
{
    waypoints = wp;
}

/**
 * Add a waypoint to the list of waypoints
 *
 * return number of waypoints
 */
uint32_t map_add_waypoint(waypoint_t* wp)
{
    if (prev_wp) {
        wp->num = prev_wp->num + 1;
        prev_wp->next = wp;
    } else {
        waypoints = wp;
    }
    prev_wp = wp;
    return prev_wp->num;
}

error_code_t map_free_waypoints()
{
    waypoint_t* wp_ = waypoints;
    while (wp_) {
        waypoint_t* nwp;
        nwp = wp_;
        wp_ = wp_->next;
        free(nwp);
    }
    return PM_OK;
}

error_code_t map_run_on_waypoints(void (*function)(waypoint_t* wp))
{
    waypoint_t* wp_ = waypoints;
    while (wp_) {
        function(wp_);
        wp_ = wp_->next;
    }
    return PM_OK;
}

error_code_t map_update_waypoint_path(map_t* map)
{
    waypoint_t* wp_ = waypoints;
    while (wp_) {
        wp_->active = 0;
        map_calculate_waypoint(map, wp_);
        for (uint32_t i = 0; i < map->tile_count; i++) {
            if (wp_->tile_x == map->tiles[i]->x && wp_->tile_y == map->tiles[i]->y)
                wp_->active = 1;
        }
        wp_ = wp_->next;
    }
    return PM_OK;
}