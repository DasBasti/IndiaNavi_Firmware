/*
 * Pictures that are stored in the firmware, run length encoded with a few paints
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "artwork.h"

/* a run within one row */
static void draw_run(const display_t* dsp, const artwork_paint_t* paint, int16_t x, int16_t y, uint16_t length)
{
    if (paint->mask == 0xffff || paint->a == paint->b) {
        display_rect_fill(dsp, x, y, length, 1, paint->a);
        return;
    }
    uint16_t row = (paint->mask >> ((y & 3) * 4)) & 0xf;
    for (uint16_t i = 0; i < length; i++, x++)
        display_pixel_draw(dsp, x, y, (row >> (x & 3)) & 1 ? paint->a : paint->b);
}

error_code_t artwork_draw(const display_t* dsp, const artwork_t* art, int16_t left, int16_t top)
{
    if (!dsp || !art)
        return PM_FAIL;

    uint32_t pixels = (uint32_t)art->width * art->height;
    uint32_t position = 0;
    for (uint32_t i = 0; i < art->data_size && position < pixels;) {
        uint8_t paint = art->data[i] >> 4;
        uint32_t length = art->data[i++] & 0xf;
        if (length == 0 && i < art->data_size)
            length = art->data[i++] + 16;
        if (position + length > pixels)
            length = pixels - position;

        if (paint == 0 || paint >= art->paint_count) {
            position += length; // transparent
            continue;
        }
        // split the run at the end of the rows
        while (length) {
            uint16_t x = position % art->width;
            uint16_t y = position / art->width;
            uint16_t part = art->width - x;
            if (part > length)
                part = length;
            draw_run(dsp, &art->paints[paint], left + x, top + y, part);
            position += part;
            length -= part;
        }
    }
    return PM_OK;
}
