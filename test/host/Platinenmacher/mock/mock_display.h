/*
 * Mockups for testing the display and rendering
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef TEST_HOST_PLATINENMACHER_MOCK_DISPLAY_H
#define TEST_HOST_PLATINENMACHER_MOCK_DISPLAY_H

#include "display.h"

error_code_t write_pixel(const display_t* dsp, int16_t x, int16_t y,
    uint8_t color)
{
    dsp->fb[((y * dsp->size.width) + x)] = color;
    return PM_OK;
}

uint8_t decompress(rect_t* size, int16_t x, int16_t y, const uint8_t* data)
{

    return data[x * size->width + y];
}

#define DISPLAY_WIDTH 20
#define DISPLAY_HEIGHT 20
#endif /* TEST_HOST_PLATINENMACHER_MOCK_DISPLAY_H */