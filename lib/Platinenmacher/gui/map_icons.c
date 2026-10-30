/*
 * Icons of points of interest on the map, see map_icons.h
 *
 * SPDX-License-Identifier: MIT
 */

#include "map_icons.h"

#include <icons_16.h>
#include <stddef.h>

/* indexed by the MFR_ICON_* ids of mapsforge.h, 0 is no icon */
static const uint8_t* const ICONS[] = {
    NULL,
    poi_drinking_water,
    poi_spring,
    poi_food,
    poi_supermarket,
    poi_pharmacy,
    poi_hospital,
    poi_train,
    poi_bus,
    poi_tram,
    poi_parking,
    poi_castle,
    poi_tower,
    poi_other,
};

int map_icons_draw(const display_t* dsp, uint8_t icon, int16_t x, int16_t y)
{
    if (icon == 0 || icon >= sizeof(ICONS) / sizeof(ICONS[0]))
        return 0;
    display_draw_image(dsp, ICONS[icon], x - ICON_SIZE / 2, y - ICON_SIZE / 2, ICON_SIZE, ICON_SIZE);
    return 1;
}
