/*
 * Map screen component for GUI
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui/label.h"
#include "gui/map.h"

#include "nmea_parser.h"

#include "gui.h"
#include "tasks.h"

#include "icons_32/icons_32.h"

static const display_t* dsp;

static map_t* map;
static label_t* scaleBox;
static label_t* positon_marker;
static label_t* map_copyright;
static label_t* infoBox;

static uint8_t zoom_level_selected = 0;
uint8_t zoom_level[] = { 16, 14 };
uint8_t zoom_level_scaleBox_width[] = { 63, 77 };
char* zoom_level_scaleBox_text[] = { "100m", "500m" };

/**
 * render cb for InfoText label
 */
static error_code_t updateInfoText(const display_t* dsp, void* comp)
{
    return PM_OK;
    if (map_position->fix != GPS_FIX_INVALID) {
        xSemaphoreTake(print_semaphore, portMAX_DELAY);
        sprintf(infoBox->text, "GPS: %fN %fE %.02fm (HDOP:%f)",
            map_position->longitude, map_position->longitude, map_position->altitude, map_position->hdop);
        xSemaphoreGive(print_semaphore);
    } else {
        xSemaphoreTake(print_semaphore, portMAX_DELAY);
        sprintf(infoBox->text, "No GPS Signal found!");
        xSemaphoreGive(print_semaphore);
    }
    return PM_OK;
}

error_code_t render_position_marker(const display_t* dsp, void* comp)
{
    label_t* label = (label_t*)comp;
    uint8_t hdop = floor(map_position->hdop / 2);
    hdop += 8;
    if (map_position->fix != GPS_FIX_INVALID) {
        label->box.left = map->pos_x - label->box.width / 2;
        label->box.top = map->pos_y - label->box.height / 2;
        display_circle_fill(dsp, map->pos_x, map->pos_y, 6, BLUE);
        display_circle_fill(dsp, map->pos_x, map->pos_y, 2, WHITE);
        display_circle_draw(dsp, map->pos_x, map->pos_y, hdop, BLACK);
        return PM_OK;
    }
    return ABORT;
}

error_code_t updateSatsInView(const display_t* dsp, void* comp)
{
    xSemaphoreTake(print_semaphore, portMAX_DELAY);
    sprintf(gps_indicator_label->text, "%d", map_position->satellites_in_view);
    xSemaphoreGive(print_semaphore);
    if (gps_indicator_label) {
        image_t* icon = gps_indicator_label->child;
        if (map_position->fix != GPS_FIX_INVALID)
            icon->data = GPS_lock;
        else
            icon->data = GPS;
    }
    return PM_OK;
}

static error_code_t map_pre_render_cb(const display_t* dsp, void* component)
{
    // Only modify map if we are GPS fixed
    if (!map_position || map_position->fix == GPS_FIX_INVALID){
        return NOT_NEEDED;
	}

	if(gps_indicator_label){
		gps_indicator_label->onBeforeRender = updateSatsInView;
	}
    map_update_zoom_level(map, 16);
    map_update_position(map, map_position);

    //TODO: move to map component
    /*char *waypoint_file = RTOS_Malloc(32768);
	char *wp_line = RTOS_Malloc(50);
	async_file_t *wp_file = &AFILE;
	wp_file->filename = "//TRACK";
	wp_file->dest = waypoint_file;
	wp_file->loaded = false;
	loadFile(wp_file);
	uint8_t delay = 0;
	ESP_LOGI(TAG, "Load waypoint information queued");
	while (!wp_file->loaded)
	{
		vTaskDelay(100 / portTICK_PERIOD_MS);
		if (delay++ == 20)
			break;
	}
	ESP_LOGI(TAG, "Loaded waypoint information. Calculating...");
	uint64_t start = esp_timer_get_time();

	free_render_pipeline(RL_PATH);

	waypoint_t *wp_ = waypoints;
	while (wp_)
	{
		waypoint_t *nwp;
		nwp = wp_;
		wp_ = wp_->next;
		free(nwp);
	}

	waypoint_t *prev_wp = NULL;
	waypoint_t cur_wp = {};
	cur_wp.color = BLUE;
	if (wp_file->loaded)
	{
		ESP_LOGI(TAG, "Load waypoint information ");
		char *f = waypoint_file;
		bool header = true;
		while (1)
		{
			f = readline(f, wp_line);
			if (!f)
				break;
			if (wp_line[0] == '-' && wp_line[1] == '-')
			{
				header = false;
				continue;
			};
			if (header)
			{
				continue;
			}

			float flon = atoff(strtok(wp_line, " "));
			float flat = atoff(strtok(NULL, " "));
			//ESP_LOGI(TAG, "Read waypoint: %f - %f", flon, flat);
			// only load wp that are on currently shown tiles

			cur_wp.lon = flon;
			cur_wp.lat = flat;
			calculate_waypoints(&cur_wp);
			//ESP_LOGI(TAG, "Waypoint %d: %d/%d", cur_wp.num, cur_wp.tile_x, cur_wp.tile_y);
			for (uint8_t i = 0; i < 6; i++)
			{
				if (cur_wp.tile_x == map_tiles[i].x && cur_wp.tile_y == map_tiles[i].y)
				{
					waypoint_t *wp_t = RTOS_Malloc(sizeof(waypoint_t));
					memcpy(wp_t, &cur_wp, sizeof(waypoint_t));
					if (prev_wp)
					{
						prev_wp->next = wp_t;
					}
					else
					{
						waypoints = wp_t;
					}
					prev_wp = wp_t;
					add_to_render_pipeline(render_waypoint_marker, wp_t, RL_PATH);
					// increase for next tile
					cur_wp.num++;
					break;
				}
			}
			if (cur_wp.num % 10 == 0)
			{
				vTaskDelay(1);
			}
		}
		ESP_LOGI(TAG, "Number of waypoints: %d", cur_wp.num);
	}
	else
	{
		ESP_LOGI(TAG, "Load waypoint information failed");
	}
	RTOS_Free(waypoint_file);
	RTOS_Free(wp_line);
	ESP_LOGI(TAG, "Load waypoint information done. Took: %d ms", (uint32_t)(esp_timer_get_time() - start) / 1000);
	ESP_LOGI(TAG, "Heap Free: %d Byte", xPortGetFreeHeapSize());
	*/
    return PM_OK;
}

void map_screen_create(const display_t* display)
{
    dsp = display;
    /* register pre_render callback */
    add_pre_render_callback(map_pre_render_cb);

    map = map_create(-32, 42, 2, 2, 256, &f8x8);
    add_to_render_pipeline(map_render, map, RL_MAP);

    /* position marker */
    positon_marker = label_create("", &f8x16, 0, 0, 24, 24);
    positon_marker->textColor = BLUE;
    positon_marker->alignHorizontal = CENTER;
    positon_marker->alignVertical = MIDDLE;
    positon_marker->onBeforeRender = render_position_marker;
    add_to_render_pipeline(label_render, positon_marker, RL_TOP);

    /* scale 63px for 100m | 96px for 500ft @ zoom 16*/
    /* scale 77px for 500m | 94px for 2000ft @zoom 14 */
    scaleBox = label_create("100m", &f8x8, 10, dsp->size.height - 34, 63, 13);
    scaleBox->borderWidth = 1;
    scaleBox->borderLines = LEFT_SOLID | RIGHT_SOLID | BOTTOM_SOLID;
    //scaleBox->backgroundColor = WHITE;
    scaleBox->borderColor = BLACK;
    scaleBox->textColor = BLACK;
    scaleBox->alignVertical = BOTTOM;
    scaleBox->alignHorizontal = CENTER;
    add_to_render_pipeline(label_render, scaleBox, RL_TOP);

    map_copyright = label_create("(c) OpenStreetMap contributors", &f8x8, 0, dsp->size.height - 22, 0, 0);
    label_shrink_to_text(map_copyright);
    map_copyright->box.left = dsp->size.width - map_copyright->box.width;
    add_to_render_pipeline(label_render, map_copyright, RL_GUI_ELEMENTS);

    infoBox = label_create("", &f8x8, 0, dsp->size.height - 14,
        dsp->size.width - 1, 13);
    infoBox->borderWidth = 1;
    infoBox->borderLines = ALL_SOLID;
    infoBox->alignVertical = MIDDLE;
    infoBox->backgroundColor = WHITE;
    infoBox->onBeforeRender = updateInfoText;
    add_to_render_pipeline(label_render, infoBox, RL_GUI_ELEMENTS);

    map_update_zoom_level(map, 0);
    //map_update_position(map, map_position);
    map_tile_attach_onBeforeRender_callback(map, load_map_tile_on_demand);
    map_tile_attach_onAfterRender_callback(map, check_if_map_tile_is_loaded);
}

void toggleZoom()
{
    zoom_level_selected = !zoom_level_selected;
    map_update_zoom_level(map, zoom_level[zoom_level_selected]);

    //ESP_LOGI(TAG, "Set zoom level to: %d", tile_zoom);
    // TODO: Update Waypoints
    scaleBox->box.width = zoom_level_scaleBox_width[zoom_level_selected];
    scaleBox->text = zoom_level_scaleBox_text[zoom_level_selected];

    //map_update_position(map, map_position);
    trigger_rendering();
}