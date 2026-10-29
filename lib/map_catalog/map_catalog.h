/*
 * Catalog of the vector maps on the SD card
 *
 * All .MAP files in the MAPS folder are Mapsforge vector maps. The catalog keeps what is needed
 * to choose the maps of a screen without opening every file: name, size, bounding box and
 * creation date. It has no hardware dependency, so it can be tested on the host; reading the
 * headers is done by
 * the vector map (src/esp32/vector_map.c, src/linux/vector_map.c).
 *
 * Two rules decide which maps are drawn:
 * - New tops old: a map is left out when a newer map covers (almost) all of its area, so an old
 *   download of a region is replaced by a new one without deleting the old file.
 * - At most MAP_CATALOG_DRAWN maps per screen, the ones with the smallest area first.
 *
 * See Documentation/multiple_vector_maps.md.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MAP_CATALOG_H
#define MAP_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* maps on the card, more are ignored */
#define MAP_CATALOG_MAX 32

/* maps drawn on one screen */
#define MAP_CATALOG_DRAWN 4

/* 8.3 name with the dot and the terminating zero */
#define MAP_CATALOG_NAME_LEN 13

/* share of the area of an older map a newer one has to cover to replace it */
#define MAP_CATALOG_SUPERSEDED_SHARE 0.9

typedef struct {
    double min_lat, min_lon, max_lat, max_lon;
} map_bounds_t;

typedef struct {
    char name[MAP_CATALOG_NAME_LEN];
    uint64_t size;
    /* milliseconds since 1970, from the header of the map */
    uint64_t created;
    map_bounds_t bounds;
    /* a newer map covers this one, it is not drawn */
    bool superseded;
} map_entry_t;

typedef struct {
    map_entry_t maps[MAP_CATALOG_MAX];
    uint8_t count;
} map_catalog_t;

void map_catalog_clear(map_catalog_t* catalog);

/* true for names like "MAP.MAP" or "rheinl01.map" */
bool map_catalog_is_map_name(const char* name);

/* true for the names maps can be uploaded with: 1 to 8 letters, digits, '_' or '-' and ".map" */
bool map_catalog_is_upload_name(const char* name);

/* bytes at the start of a map file that hold its size and creation date */
#define MAP_CATALOG_HEADER_START 44

/*
 * The size of the whole file and the creation date (ms since 1970) from the first bytes of a map,
 * also of a map whose upload was not finished. false if it is not the start of a Mapsforge map.
 */
bool map_catalog_parse_header_start(const uint8_t* bytes, size_t length, uint64_t* file_size, uint64_t* created);

/* Adds a map, returns false when the catalog is full or the name too long. Call
 * map_catalog_resolve() after the last one. */
bool map_catalog_add(map_catalog_t* catalog, const char* name, uint64_t size, uint64_t created,
    map_bounds_t bounds);

/* Marks the maps that are covered by a newer one, returns the number of maps that are drawn */
uint8_t map_catalog_resolve(map_catalog_t* catalog);

/* Number of maps that are not replaced by a newer one */
uint8_t map_catalog_used(const map_catalog_t* catalog);

/*
 * Area of a screen in degrees: the position lat/lon is at the pixel anchor_x/anchor_y of a
 * width x height screen at the zoom level (Web Mercator, 256 px tiles).
 */
map_bounds_t map_catalog_screen_bounds(double lat, double lon, uint8_t zoom, uint16_t width,
    uint16_t height, int16_t anchor_x, int16_t anchor_y);

/*
 * The maps to draw for a screen: not superseded and overlapping it, the smallest first. Writes
 * up to max indices into the catalog to selected, returns how many.
 */
uint8_t map_catalog_select(const map_catalog_t* catalog, map_bounds_t screen, uint8_t* selected,
    uint8_t max);

#endif /* MAP_CATALOG_H */
