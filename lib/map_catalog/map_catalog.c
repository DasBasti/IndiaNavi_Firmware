/*
 * Catalog of the vector maps on the SD card, see map_catalog.h
 *
 * SPDX-License-Identifier: MIT
 */

#include "map_catalog.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

void map_catalog_clear(map_catalog_t* catalog)
{
    catalog->count = 0;
}

bool map_catalog_is_map_name(const char* name)
{
    size_t length = strlen(name);
    if (length < 5 || length >= MAP_CATALOG_NAME_LEN)
        return false;
    const char* extension = name + length - 4;
    return extension[0] == '.' && tolower((unsigned char)extension[1]) == 'm'
        && tolower((unsigned char)extension[2]) == 'a' && tolower((unsigned char)extension[3]) == 'p';
}

bool map_catalog_add(map_catalog_t* catalog, const char* name, uint64_t size, uint64_t created,
    map_bounds_t bounds)
{
    if (catalog->count >= MAP_CATALOG_MAX || strlen(name) >= MAP_CATALOG_NAME_LEN)
        return false;
    map_entry_t* entry = &catalog->maps[catalog->count++];
    strcpy(entry->name, name);
    entry->size = size;
    entry->created = created;
    entry->bounds = bounds;
    entry->superseded = false;
    return true;
}

/* in square degrees, enough to compare maps that lie on top of each other */
static double area(map_bounds_t b)
{
    double height = b.max_lat - b.min_lat;
    double width = b.max_lon - b.min_lon;
    return height > 0 && width > 0 ? height * width : 0;
}

static map_bounds_t intersection(map_bounds_t a, map_bounds_t b)
{
    map_bounds_t i = {
        .min_lat = fmax(a.min_lat, b.min_lat),
        .min_lon = fmax(a.min_lon, b.min_lon),
        .max_lat = fmin(a.max_lat, b.max_lat),
        .max_lon = fmin(a.max_lon, b.max_lon),
    };
    return i;
}

static bool overlap(map_bounds_t a, map_bounds_t b)
{
    return a.min_lat <= b.max_lat && b.min_lat <= a.max_lat && a.min_lon <= b.max_lon && b.min_lon <= a.max_lon;
}

uint8_t map_catalog_resolve(map_catalog_t* catalog)
{
    uint8_t drawn = 0;
    for (uint8_t i = 0; i < catalog->count; i++) {
        map_entry_t* old = &catalog->maps[i];
        old->superseded = false;
        double old_area = area(old->bounds);
        for (uint8_t j = 0; j < catalog->count && !old->superseded; j++) {
            const map_entry_t* new = &catalog->maps[j];
            if (new->created <= old->created)
                continue;
            double covered = area(intersection(old->bounds, new->bounds));
            // a map without area (a damaged header) is covered by any newer map around it
            old->superseded = old_area > 0 ? covered >= MAP_CATALOG_SUPERSEDED_SHARE * old_area
                                           : overlap(old->bounds, new->bounds);
        }
        if (!old->superseded)
            drawn++;
    }
    return drawn;
}

static double x_to_lon(double x, double world)
{
    return x / world * 360.0 - 180.0;
}

static double y_to_lat(double y, double world)
{
    double n = M_PI - 2.0 * M_PI * y / world;
    return 180.0 / M_PI * atan(sinh(n));
}

map_bounds_t map_catalog_screen_bounds(double lat, double lon, uint8_t zoom, uint16_t width,
    uint16_t height, int16_t anchor_x, int16_t anchor_y)
{
    double world = 256.0 * (double)(1ULL << zoom);
    double rad = lat * M_PI / 180.0;
    double left = (lon + 180.0) / 360.0 * world - anchor_x;
    double top = (1.0 - log(tan(rad) + 1.0 / cos(rad)) / M_PI) / 2.0 * world - anchor_y;
    map_bounds_t b = {
        .min_lat = y_to_lat(top + height, world),
        .min_lon = x_to_lon(left, world),
        .max_lat = y_to_lat(top, world),
        .max_lon = x_to_lon(left + width, world),
    };
    return b;
}

uint8_t map_catalog_select(const map_catalog_t* catalog, map_bounds_t screen, uint8_t* selected,
    uint8_t max)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < catalog->count; i++) {
        const map_entry_t* entry = &catalog->maps[i];
        if (entry->superseded || !overlap(entry->bounds, screen))
            continue;
        // insert by area, a detailed regional map before a big one
        uint8_t at = count;
        while (at > 0 && area(catalog->maps[selected[at - 1]].bounds) > area(entry->bounds))
            at--;
        if (at >= max)
            continue;
        for (uint8_t k = (count < max ? count : max - 1); k > at; k--)
            selected[k] = selected[k - 1];
        selected[at] = i;
        if (count < max)
            count++;
    }
    return count;
}
