/*
 * Map component for loading tile images according to a position
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include "map.h"

static uint8_t right_side = 0;

static float flon2tile(float lon, uint8_t zoom)
{
    return ((lon + 180) / 360) * pow(2, zoom);
}

static float flat2tile(float lat, uint8_t zoom)
{
    return ((1 - log(tan((lat * M_PI) / 180) + 1 / cos((lat * M_PI) / 180)) / M_PI) / 2) * pow(2, zoom);
}

static map_tile_t *tile_create(uint16_t tile_size)
{
    map_tile_t *tile = RTOS_Malloc(sizeof(map_tile_t));
    tile->image = image_create(0, 0, 0, tile_size, tile_size);
    tile->label = label_create("", 0, 0, 0, 0, 0);
    return tile;
}

map_t *map_create(uint8_t width, uint8_t height, uint16_t tile_size)
{
    if (width == 0 || height == 0)
        return NULL;

    map_t *map = RTOS_Malloc(sizeof(map_t));
    map->width = width;
    map->height = height;
    map->tile_count = width * height;
    map->tiles = RTOS_Malloc(sizeof(map_tile_t *) * map->tile_count);
    for (uint32_t i = 0; i < map->tile_count; i++)
    {
        map->tiles[i] = tile_create(tile_size);
    }
    return map;
}

error_code_t map_update_zoom_level(map_t *map, uint8_t level)
{
    // TODO: mechanism to check wheter the zoom level is available
    map->tile_zoom = level;
    return PM_OK;
}

uint8_t map_get_zoom_level(map_t *map)
{
    return map->tile_zoom;
}

map_tile_t *map_get_tile(map_t *map, uint8_t x, uint8_t y)
{
    if (x > map->width || y > map->height)
        return NULL;

    return map->tiles[x * map->height + y];
}

error_code_t map_update_position(map_t *map, map_position_t pos)
{
    uint16_t x = 0, y = 0, x_old = 0, y_old = 0;
    float xf = 0.0, yf = 0.0;
    // get tile number of tile with position on it as float and integer
    if (pos.longitude != 0.0)
    {
        xf = flon2tile(pos.longitude, map->tile_zoom);
        x = floor(xf);
    }
    // also for y axis
    if (pos.latitude != 0.0)
    {
        yf = flat2tile(pos.latitude, map->tile_zoom);
        y = floor(yf);
    }
    // get offset to tile corner of tile with position
    uint32_t pos_x = floor((xf - x) * 256); // offset from tile 0
    uint32_t pos_y = floor((yf - y) * 256); // offset from tile 0
    /* grab necessary tiles */
    if ((x != x_old) | (y != y_old))
    {
        x_old = x;
        y_old = y;
        for (uint8_t i = 0; i < map->width; i++)
        {
            for (uint8_t j = 0; j < map->height; j++)
            {
                uint8_t idx = i * map->height + j;
                if (pos_x < 128)
                {
                    map->tiles[idx]->x = x - 1 + i;
                    right_side = 1;
                }
                else
                {
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

    return PM_OK;
}
