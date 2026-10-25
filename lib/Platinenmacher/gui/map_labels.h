/*
 * Place names on the map: written with the 8x8 font, most important first, names that would
 * overlap an earlier one or do not fit on the display are skipped.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PLATINENMACHER_GUI_MAP_LABELS_H_
#define PLATINENMACHER_GUI_MAP_LABELS_H_

#include <stdint.h>

#include "display.h"
#include "font.h"

/* forget the names of the last frame */
void map_labels_reset(void);

/*
 * Writes the UTF-8 text (not zero terminated) centered on x/y, with a white halo so it stays
 * readable on the fills. Letters outside the font are written as ASCII (ä -> ae, é -> e). rank 1
 * is bold. Returns 1 if the name was drawn, 0 if it was skipped.
 */
int map_labels_draw(const display_t* dsp, font_t* font, int16_t x, int16_t y, const char* text, uint32_t len,
    uint8_t rank);

#endif /* PLATINENMACHER_GUI_MAP_LABELS_H_ */
