/*
 * Battery level indicator component for showing charge level
 *
 * Copyright (c) 2024, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PLATINENMACHER_DISPLAY_GUI_BATTERY_INDICATOR_H_
#define PLATINENMACHER_DISPLAY_GUI_BATTERY_INDICATOR_H_

#include "font.h"
#include "image.h"
#include "label.h"

#include <icons_32.h>

typedef struct {
    image_t image;
    label_t label;
    char label_text[10];
    uint8_t level;
    bool charging;
    size_t num_levels;
    uint8_t* batlevels;
    uint8_t** batlevel_images;
    int (*save_printf)(char* __restrict, const char* __restrict, ...);
} battery_indicator_t;

battery_indicator_t* create_battery_indicator(int16_t left, int16_t top, uint8_t level, bool charging, font_t* font, uint8_t* batlevels, uint8_t** batlevel_images, size_t num_levels);
void battery_indicator_set_level(battery_indicator_t* battery, uint8_t level);

extern uint8_t* batlevel_images[];
extern uint8_t batlevels[];
extern size_t batlevel_num;

#endif /* PLATINENMACHER_DISPLAY_GUI_BATTERY_INDICATOR_H_ */