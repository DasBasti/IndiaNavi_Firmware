/*
 * Waypoint component for showing ways on a map
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "waypoint.h"
#include "error.h"

#include <esp_log.h>
static const char* TAG = "waypoint";

error_code_t waypoint_render_marker(const display_t* dsp, void* comp)
{
	waypoint_t *wp = (waypoint_t *)comp;
	if ((wp->active == 1) && (wp->tile_x != 0) && (wp->tile_y != 0))
	{
		
					uint16_t x = wp->pos_x + (1 * 256);
					uint16_t y = wp->pos_y + (1 * 256);
						display_circle_fill(dsp, x, y, 2, wp->color);
					if (wp->next)
					{
						// line to next waypoint
						uint32_t x2 = wp->next->pos_x + (wp->next->tile_x - wp->tile_x + 1) * 256;
						uint32_t y2 = wp->next->pos_y + (wp->next->tile_y - wp->tile_y + 1) * 256;
						//ESP_LOGI(TAG, "WP Line to %d/%d tile %d/%d", x2,y2, wp->next->tile_x, wp->next->tile_y);
						display_line_draw(dsp, x, y, x2, y2, wp->color);
						/*if (!zoom_level_selected)
						{
							display_line_draw(dsp, x + 1, y, x2 + 1, y2, wp->color);
							display_line_draw(dsp, x - 1, y, x2 - 1, y2, wp->color);
						}*/
						uint16_t vec_len = length(x, y, x2, y2);
						display_line_draw(dsp, x, y, x + (x / vec_len * 5), y + (y / vec_len * 5), BLACK);
					}
					display_pixel_draw(dsp, x, y, WHITE);
	}
	
    return ABORT;
}