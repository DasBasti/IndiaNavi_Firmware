/*
 * Battery level indicator component for showing charge level
 *
 * Copyright (c) 2024, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "battery_indicator.h"
#include "memory.h"

void battery_indicator_set_level(battery_indicator_t* battery, uint8_t level)
{
    // battery is full if not below thresholds in batlevels
    battery->image.data = battery->batlevel_images[battery->num_levels - 1];
    for (size_t i = 0; i < battery->num_levels; i++) {
        if (level <= battery->batlevels[i]) {
            battery->image.data = battery->batlevel_images[i];
            break;
        }
    }

    if (battery->save_printf) {
        if (battery->charging) {
            battery->save_printf(battery->label_text, "CHRG");
        } else if (level == 100) {
            battery->save_printf(battery->label_text, "FULL");
        } else {
            battery->save_printf(battery->label_text, "%d%%", level);
        }
    }

    label_shrink_to_text(&battery->label);
}

battery_indicator_t* create_battery_indicator(int16_t left, int16_t top, uint8_t level, bool charging, font_t* font, uint8_t* batlevels, uint8_t** batlevel_images, size_t num_levels)
{
    battery_indicator_t* battery = RTOS_Malloc(sizeof(battery_indicator_t));
    battery->level = level;
    battery->num_levels = num_levels;
    battery->batlevels = batlevels;
    battery->batlevel_images = batlevel_images;

    battery->image.box.left = left;
    battery->image.box.top = top;
    battery->image.box.width = 32;
    battery->image.box.height = 32;

    battery->label.box.left = left + battery->image.box.width;
    battery->label.box.top = top;
    battery->label.box.width = 0;
    battery->label.box.height = 8;
    battery->label.text = battery->label_text;
    battery->label.font = font;

    battery->label.child = &battery->image;
    battery->label.alignVertical = MIDDLE;
    battery->label.backgroundColor = TRANSPARENT;

    label_shrink_to_text(&battery->label);
    battery_indicator_set_level(battery, level);

    return battery;
}

uint8_t* batlevel_images[] = { bat_0, bat_10, bat_30, bat_50, bat_80, bat_100 };
uint8_t batlevels[] = { 5, 15, 35, 45, 75, 90 };
size_t batlevel_num = 6;
