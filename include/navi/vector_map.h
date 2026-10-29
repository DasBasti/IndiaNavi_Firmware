/*
 * Vector map
 *
 * Draws the Mapsforge map files of the SD card (all .MAP files in MAPS, map.map is one of them)
 * directly into the framebuffer as polygons, lines and points, instead of loading pre-rendered
 * tile images. The maps that cover the screen are drawn together, at most MAP_CATALOG_DRAWN, and
 * a newer map replaces an older one of the same region (see lib/map_catalog and
 * Documentation/multiple_vector_maps.md). Needs the mapsforge component (see
 * components/mapsforge), without it nothing is available.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_VECTOR_MAP_H_
#define NAVI_VECTOR_MAP_H_

#include <stdbool.h>
#include <stdint.h>

#include "Platinenmacher.h"

/*
 * Reads the headers of the maps on the SD card into the catalog of the maps. true if the firmware
 * has vector map support and there is at least one map. Called when the map screen is created,
 * so a reload of the screen also finds new maps.
 */
bool vector_map_available(void);

/*
 * Draw the maps at a zoom level (Web Mercator, 256 px tiles) over the whole display, white where
 * no map is. The position appears at the pixel anchor_x/anchor_y, as it does on the pre-rendered
 * tiles.
 *
 * Locks the SD card while it draws.
 */
error_code_t vector_map_render(const display_t* dsp, double lat, double lon, uint8_t zoom,
    int16_t anchor_x, int16_t anchor_y);

#endif /* NAVI_VECTOR_MAP_H_ */
