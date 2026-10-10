/*
 * Waypoint component for showing ways on a map
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "waypoint.h"
#include "error.h"

#include <math.h>
#include <stddef.h>

/* the arrow head is this much wider than the line on each side, and twice as long as half its width */
#define ARROW_MARGIN 5

error_code_t waypoint_render_marker(const display_t* dsp, void* comp)
{
    waypoint_t* wp = (waypoint_t*)comp;
    if ((wp->active == 1) && (wp->tile_x != 0) && (wp->tile_y != 0)) {
        display_circle_fill(dsp, wp->pos_x, wp->pos_y, wp->line_thickness + 1, wp->color);
        if (wp->next && wp->next->active) {
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
        }
        display_pixel_draw(dsp, wp->pos_x, wp->pos_y, WHITE);
    }

    return ABORT;
}
static float distance(const waypoint_t* a, const waypoint_t* b)
{
    return hypotf((float)(b->pos_x - a->pos_x), (float)(b->pos_y - a->pos_y));
}

void waypoint_place_arrows(waypoint_t* const* visible, uint32_t count, uint16_t spacing, uint16_t lookahead)
{
    // the first arrow comes after half the spacing, so it is not hidden under the start
    float travelled = spacing / 2.0f;
    for (uint32_t i = 0; i < count; i++) {
        waypoint_t* wp = visible[i];
        wp->arrow_to = NULL;
        if (!wp->next || !wp->next->active)
            continue;

        travelled += distance(wp, wp->next);
        if (travelled < spacing)
            continue;

        // the points of a GPX track can be very close, take the direction over a longer piece
        waypoint_t* ahead = wp->next;
        while (distance(wp, ahead) < lookahead && ahead->next && ahead->next->active)
            ahead = ahead->next;
        if (distance(wp, ahead) < 2)
            continue; // no direction to show, try the next segment

        wp->arrow_to = ahead;
        travelled = 0;
    }
}

/**
 * Arrow head in the color of the track with a border, its tip lies a bit ahead of the waypoint
 */
error_code_t waypoint_render_arrow(const display_t* dsp, void* comp)
{
    waypoint_t* wp = (waypoint_t*)comp;
    if (!wp->active || !wp->arrow_to)
        return ABORT;

    float length = distance(wp, wp->arrow_to);
    if (length < 1)
        return ABORT;
    float ux = (wp->arrow_to->pos_x - wp->pos_x) / length;
    float uy = (wp->arrow_to->pos_y - wp->pos_y) / length;

    // a thicker line (closer zoom) gets a larger arrow, so it stands out
    int16_t half_width = wp->line_thickness + ARROW_MARGIN;
    int16_t arrow_length = 2 * half_width;

    float tip_x = wp->pos_x + ux * arrow_length / 2;
    float tip_y = wp->pos_y + uy * arrow_length / 2;
    float base_x = tip_x - ux * arrow_length;
    float base_y = tip_y - uy * arrow_length;
    // corners of the base, at a right angle to the direction
    int16_t x1 = lroundf(base_x - uy * half_width);
    int16_t y1 = lroundf(base_y + ux * half_width);
    int16_t x2 = lroundf(base_x + uy * half_width);
    int16_t y2 = lroundf(base_y - ux * half_width);
    int16_t tx = lroundf(tip_x);
    int16_t ty = lroundf(tip_y);

    // fill with lines from the tip to every point of the base
    int16_t steps = 4 * half_width;
    for (int16_t i = 0; i <= steps; i++) {
        int16_t bx = x1 + (x2 - x1) * i / steps;
        int16_t by = y1 + (y2 - y1) * i / steps;
        display_line_draw(dsp, tx, ty, bx, by, wp->color);
    }

    // the border keeps the arrow visible on a map with the same color
    color_t border = wp->color == BLACK ? WHITE : BLACK;
    display_line_draw(dsp, tx, ty, x1, y1, border);
    display_line_draw(dsp, tx, ty, x2, y2, border);
    display_line_draw(dsp, x1, y1, x2, y2, border);

    return ABORT;
}
