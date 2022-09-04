/*
 * Mockups for testing the display and rendering
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef TEST_HOST_PLATINENMACHER_MOCK_DISPLAY_H
#define TEST_HOST_PLATINENMACHER_MOCK_DISPLAY_H

#include "display.h"

error_code_t write_pixel(struct display *dsp, uint16_t x, uint16_t y,
                         uint8_t color)
{
    dsp->fb[((y * dsp->size.width) + x)] = color;
    return PM_OK;
}

uint8_t decompress(rect_t *size, uint16_t x, uint16_t y, uint8_t *data)
{

    return data[x*size->width+y];
}

#endif /* TEST_HOST_PLATINENMACHER_MOCK_DISPLAY_H */