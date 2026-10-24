/*
 * The simulator has no vector map, it keeps using tile images.
 *
 * SPDX-License-Identifier: MIT
 */

#include "navi/vector_map.h"

bool vector_map_available(void)
{
    return false;
}

error_code_t vector_map_render(const display_t* dsp, double lat, double lon, uint8_t zoom,
    int16_t anchor_x, int16_t anchor_y)
{
    return UNAVAILABLE;
}
