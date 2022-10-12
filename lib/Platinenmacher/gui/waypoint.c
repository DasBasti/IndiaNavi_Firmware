/*
 * Waypoint component for showing ways on a map
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "waypoint.h"
#include "error.h"

error_code_t waypoint_render_marker(const display_t* dsp, void* comp)
{
    waypoint_t* wp = (waypoint_t*)comp;
    if ((wp->active == 1) && (wp->tile_x != 0) && (wp->tile_y != 0)) {
        display_circle_fill(dsp, wp->pos_x, wp->pos_y, 2, wp->color);
        if (wp->next) {
            // line to next waypoint
            uint32_t x2 = wp->next->pos_x;
            uint32_t y2 = wp->next->pos_y;
            display_line_draw(dsp, wp->pos_x, wp->pos_y, x2, y2, wp->color);
            if (wp->line_thickness > 1) {
                display_line_draw(dsp, wp->pos_x + 1, wp->pos_y, x2 + 1, y2, wp->color);
                display_line_draw(dsp, wp->pos_x - 1, wp->pos_y, x2 - 1, y2, wp->color);
                // uint16_t vec_len = length(wp->pos_x, wp->pos_y, x2, y2);
                // display_line_draw(dsp, wp->pos_x, wp->pos_y, wp->pos_x + (wp->pos_x / vec_len * 5), wp->pos_y + (wp->pos_y / vec_len * 5), BLACK);
            }
            ESP_LOGI(__func__, "wp %d - %d/%d -> %d/%d", wp->num, wp->pos_x, wp->pos_x, wp->next->pos_x, wp->next->pos_y);
        }
        display_pixel_draw(dsp, wp->pos_x, wp->pos_y, WHITE);
    }

    return ABORT;
}