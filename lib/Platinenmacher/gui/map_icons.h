/*
 * Icons of points of interest on the map: the 16x16 bitmaps of lib/icons_16 centered on the
 * position of the POI, transparent where the icon has no pixels.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PLATINENMACHER_GUI_MAP_ICONS_H_
#define PLATINENMACHER_GUI_MAP_ICONS_H_

#include <stdint.h>

#include "display.h"

/*
 * Draws icon (the MFR_ICON_* id of mapsforge.h: 1 drinking water, 2 spring, 3 food, 4 supermarket,
 * 5 pharmacy, 6 hospital, 7 train, 8 bus, 9 tram, 10 parking, 11 castle, 12 tower, 13 other)
 * centered on x/y. Parts outside of the display are clipped. Returns 0 for an unknown icon,
 * which draws nothing.
 */
int map_icons_draw(const display_t* dsp, uint8_t icon, int16_t x, int16_t y);

#endif /* PLATINENMACHER_GUI_MAP_ICONS_H_ */
