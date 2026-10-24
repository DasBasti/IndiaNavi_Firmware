/*
 * Vector map
 *
 * Draws a Mapsforge map file from the SD card (MAPS/map.map) directly into the framebuffer
 * as polygons, lines and points, instead of loading pre-rendered tile images. Needs the
 * mapsforge component (see components/mapsforge), without it nothing is available.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_VECTOR_MAP_H_
#define NAVI_VECTOR_MAP_H_

#include <stdbool.h>
#include <stdint.h>

#include "Platinenmacher.h"

/* true if the firmware has vector map support and the map file exists on the SD card */
bool vector_map_available(void);

/*
 * Draw the map at a zoom level (Web Mercator, 256 px tiles) over the whole display. The position
 * appears at the pixel anchor_x/anchor_y, as it does on the pre-rendered tiles.
 *
 * Locks the SD card while it draws.
 */
error_code_t vector_map_render(const display_t* dsp, double lat, double lon, uint8_t zoom,
    int16_t anchor_x, int16_t anchor_y);

#endif /* NAVI_VECTOR_MAP_H_ */
