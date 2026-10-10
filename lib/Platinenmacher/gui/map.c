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

static font_t* map_font;
static char* not_loaded_string = "no tile loaded";

static waypoint_t* waypoints = NULL;
static waypoint_t* prev_wp = NULL;

/* fraction of the world map as fixed point with 32 bits after the point */
static uint32_t fraction_to_world(float f)
{
    if (!(f > 0.0f)) // also NaN
        return 0;
    if (f >= 1.0f)
        return UINT32_MAX;
    return (uint32_t)(f * 4294967296.0f);
}

/* x position on the world map (Web Mercator), the same for every zoom level */
static uint32_t lon2world(float lon)
{
    return fraction_to_world((lon + 180.0f) / 360.0f);
}

/* y position on the world map (Web Mercator), the same for every zoom level */
static uint32_t lat2world(float lat)
{
    float rad = lat * (float)M_PI / 180.0f;
    return fraction_to_world((1.0f - logf(tanf(rad) + 1.0f / cosf(rad)) / (float)M_PI) / 2.0f);
}

/* number of the tile at a zoom level, valid up to zoom level 24 */
static inline uint32_t world2tile(uint32_t world, uint8_t zoom)
{
    return (uint64_t)world >> (32 - zoom);
}

/* pixel inside a tile of 256 pixels at a zoom level */
static inline uint16_t world2pixel(uint32_t world, uint8_t zoom)
{
    return ((uint64_t)world >> (24 - zoom)) & 0xff;
}

static map_tile_t* tile_create(int16_t left, int16_t top, uint16_t tile_size)
{
    map_tile_t* tile = RTOS_Malloc(sizeof(map_tile_t));
    if (!tile)
        return NULL;
    tile->image = image_create(0, left, top, tile_size, tile_size);
    tile->label = label_create(not_loaded_string, map_font, left, top, tile_size, tile_size);
    if (!tile->image || !tile->label) {
        RTOS_Free(tile->image);
        RTOS_Free(tile->label);
        RTOS_Free(tile);
        return NULL;
    }
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
    if (!map)
        return NULL;
    map->width = width;
    map->height = height;
    map->box.left = left;
    map->box.top = top;
    map->box.height = height * tile_size;
    map->box.width = width * tile_size;
    map->tile_count = width * height;
    map->tiles = RTOS_Malloc(sizeof(map_tile_t*) * map->tile_count);
    if (!map->tiles) {
        RTOS_Free(map);
        return NULL;
    }
    map_font = font;
    for (uint32_t x = 0; x < width; x++)
        for (uint32_t y = 0; y < height; y++) {
            uint32_t idx = (x * height) + y;
            map->tiles[idx] = tile_create((x * tile_size) + left, (y * tile_size) + top, tile_size);
            if (!map->tiles[idx]) {
                map_free(map);
                return NULL;
            }
            map->tiles[idx]->image->parent = map->tiles[idx];
            map->tiles[idx]->image->box.height = tile_size;
            map->tiles[idx]->image->box.width = tile_size;
            map->tiles[idx]->x = x;
            map->tiles[idx]->y = y;
        }
    return map;
}

/**
 * Free map, all tiles and loaded tile image data
 */
void map_free(map_t* map)
{
    if (!map)
        return;
    if (map->tiles) {
        for (uint32_t i = 0; i < map->tile_count; i++) {
            map_tile_t* tile = map->tiles[i];
            if (!tile)
                continue;
            if (tile->image) {
                RTOS_Free(tile->image->data);
                RTOS_Free(tile->image);
            }
            RTOS_Free(tile->label);
            RTOS_Free(tile);
        }
        RTOS_Free(map->tiles);
    }
    RTOS_Free(map);
}

error_code_t map_update_zoom_level(map_t* map, uint8_t level)
{
    if (map)
        map->tile_zoom = level;
    return PM_OK;
}

uint8_t map_get_zoom_level(map_t* map)
{
    return map->tile_zoom;
}

map_tile_t* map_get_tile(map_t* map, uint8_t x, uint8_t y)
{
    if (x >= map->width || y >= map->height)
        return NULL;

    return map->tiles[x * map->height + y];
}

static inline void update_map_tile_if_coords_change(map_tile_t* t, uint32_t x, uint32_t y, uint32_t z)
{
    if (t->x != x) {
        t->image->loaded = NOT_LOADED;
        t->x = x;
    }
    if (t->y != y) {
        t->image->loaded = NOT_LOADED;
        t->y = y;
    }
    if (t->z != z) {
        t->image->loaded = NOT_LOADED;
        t->z = z;
    }
}

error_code_t map_update_position(map_t* map, map_position_t* pos)
{
    uint32_t x = 0, y = 0;
    // tile with the position on it and the offset to its corner
    map->pos_x = 0;
    map->pos_y = 0;
    if (pos->longitude != 0.0) {
        uint32_t world_x = lon2world(pos->longitude);
        x = world2tile(world_x, map->tile_zoom);
        map->pos_x = world2pixel(world_x, map->tile_zoom);
    }
    // also for y axis
    if (pos->latitude != 0.0) {
        uint32_t world_y = lat2world(pos->latitude);
        y = world2tile(world_y, map->tile_zoom);
        map->pos_y = world2pixel(world_y, map->tile_zoom);
    }

    for (uint8_t i = 0; i < map->width; i++) {
        for (uint8_t j = 0; j < map->height; j++) {
            uint16_t idx = i * map->height + j;
            update_map_tile_if_coords_change(map->tiles[idx], x - 1 + i, y - 1 + j, map->tile_zoom);
            map->tiles[idx]->image->box.left = (i * 256) + map->box.left;
            map->tiles[idx]->image->box.top = (j * 256) + map->box.top;
            map->tiles[idx]->label->box.left = (i * 256) + map->box.left;
            map->tiles[idx]->label->box.top = (j * 256) + map->box.top;
            ESP_LOGI(__func__, "Tile %d/%d @ %d/%d", i,j,(i * 256) + map->box.left,(j * 256) + map->box.top);

        }
    }

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

void map_attach_onBeforeRender_callback(map_t* map, error_code_t (*cb)(const display_t* dsp, void* component))
{
    map->onBeforeRender = cb;
}

void map_attach_onAfterRender_callback(map_t* map, error_code_t (*cb)(const display_t* dsp, void* component))
{
    map->onAfterRender = cb;
}

error_code_t map_tile_render(const display_t* dsp, void* component)
{
    map_tile_t* tile = (map_tile_t*)component;
    if (tile->image && image_render(dsp, tile->image) == PM_OK) {
        if(tile->label)
            label_render(dsp, tile->label);
        return PM_OK;
    }

    return label_render(dsp, tile->label);
}

error_code_t map_render(const display_t* dsp, void* component)
{
    // Render map at position box
    map_t* map = (map_t*)component;
    if (map->onBeforeRender)
        map->onBeforeRender(dsp, map);
    for (uint32_t i = 0; i < map->tile_count; i++) {
        map_tile_render(dsp, map->tiles[i]);
    }
    if (map->onAfterRender)
        map->onAfterRender(dsp, map);
    return PM_OK;
}

error_code_t map_calculate_waypoint(map_t* map, waypoint_t* wp_t)
{
    wp_t->tile_x = world2tile(wp_t->world_x, map->tile_zoom);
    wp_t->tile_y = world2tile(wp_t->world_y, map->tile_zoom);

    // the tiles are a grid, tiles[0] is the top left one (idx = x * height + y)
    uint32_t tx = wp_t->tile_x - map->tiles[0]->x;
    uint32_t ty = wp_t->tile_y - map->tiles[0]->y;
    if (tx < map->width && ty < map->height) {
        wp_t->pos_x = tx * 256 + world2pixel(wp_t->world_x, map->tile_zoom) + map->box.left; // offset from tile 0
        wp_t->pos_y = ty * 256 + world2pixel(wp_t->world_y, map->tile_zoom) + map->box.top;  // offset from tile 0
        wp_t->active = 1;
    }

    return PM_OK;
}

void map_set_first_waypoint(waypoint_t* wp)
{
    waypoints = wp;
}

waypoint_t* map_first_waypoint(void)
{
    return waypoints;
}

/**
 * Add a waypoint to the list of waypoints, its latitude and longitude have to be set
 *
 * return number of waypoints
 */
uint32_t map_add_waypoint(waypoint_t* wp)
{
    // the position on the map does not change, it is calculated once and not on every render
    wp->world_x = lon2world(wp->lon);
    wp->world_y = lat2world(wp->lat);
    wp->next = NULL;
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
        RTOS_Free(nwp);
    }
    // reset list so new waypoints start at 0 and no freed waypoint is used
    waypoints = NULL;
    prev_wp = NULL;
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
        wp_ = wp_->next;
    }
    return PM_OK;
}