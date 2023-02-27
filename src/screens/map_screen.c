/*
 * Map screen component for GUI
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui/graph.h"
#include "gui/label.h"
#include "gui/map.h"

#include "parser/gpx.h"

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
static graph_t* graph;
static gpx_t* gpx_data;
static waypoint_t* closest_wp;
static int32_t dlat_min = INT32_MAX, dlon_min = INT32_MAX;

static uint8_t zoom_level_selected = 0;
uint8_t zoom_level[] = { 16, 14 };
uint8_t zoom_level_scaleBox_width[] = { 63, 77 };
char* zoom_level_scaleBox_text[] = { "100m", "500m" };
graph_point_t* height_graph_data;
float height_min = __FLT_MAX__, height_max = 0;

#define INFOBOX_STRLEN (dsp->size.width / f8x8.width)

static const char* TAG = "map_screen";

/**
 * render cb for InfoText label
 */
static error_code_t updateInfoText(const display_t* dsp, void* comp)
{
    if (!map_position)
        return UNAVAILABLE;

    if (gpx_data && gpx_data->track_name) {
        strncpy(infoBox->text, gpx_data->track_name, INFOBOX_STRLEN);
        infoBox->backgroundColor = TRANSPARENT;
    } else if (map_position->fix != GPS_FIX_INVALID) {
        char lat = 'N';
        if (map_position->latitude < 0)
            lat = 'S';

        char lon = 'E';
        if (map_position->longitude < 0)
            lon = 'W';

        xSemaphoreTake(print_semaphore, portMAX_DELAY);
        snprintf(infoBox->text, (INFOBOX_STRLEN), "GPS: %f%c %f%c %.02fm (HDOP:%.01f)",
            map_position->latitude, lat, map_position->longitude, lon, map_position->altitude, map_position->hdop);
        xSemaphoreGive(print_semaphore);
    } else {
        xSemaphoreTake(print_semaphore, portMAX_DELAY);
        snprintf(infoBox->text, (INFOBOX_STRLEN), "No GPS Signal found!");
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
        label->box.left = map->box.left + map->pos_x - label->box.width / 2;
        label->box.top = map->box.top + map->pos_y - label->box.height / 2;
        display_circle_fill(dsp, label->box.left, label->box.top, 6, BLUE);
        display_circle_fill(dsp, label->box.left, label->box.top, 2, WHITE);
        display_circle_draw(dsp, label->box.left, label->box.top, hdop, BLACK);
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

void find_closest_waypoint(waypoint_t* wp)
{
    // ignore inactive waypoints
    if (wp->active) {
        int32_t dlat = abs((wp->lat - map_position->latitude) * 1000000);
        int32_t dlon = abs((wp->lon - map_position->longitude) * 1000000);
        if (dlat < dlat_min && dlon < dlon_min) {
            dlat_min = dlat;
            dlon_min = dlon;
            closest_wp = wp;
        }
    }
}

void add_waypoints_to_renderer(waypoint_t* wp)
{
    if (wp->active)
        add_to_render_pipeline(waypoint_render_marker, wp, RL_PATH);
}

static error_code_t map_pre_render_cb(const display_t* dsp, void* component)
{
    // Only modify map if we are GPS fixed
    if (!map_position || map_position->fix == GPS_FIX_INVALID) {
        return NOT_NEEDED;
    }

    if (gps_indicator_label) {
        gps_indicator_label->onBeforeRender = updateSatsInView;
    }
    map_update_position(map, map_position);
    map_update_waypoint_path(map);

    free_render_pipeline(RL_PATH);
    map_run_on_waypoints(add_waypoints_to_renderer);

    dlat_min = INT32_MAX;
    dlon_min = INT32_MAX;
    closest_wp = NULL;
    map_run_on_waypoints(find_closest_waypoint);

    if (closest_wp)
        graph->current_position = closest_wp->num;

    return PM_OK;
}

void populate_height_data_prepare_waypoints(waypoint_t* wp)
{
    height_graph_data[wp->num].value = wp->ele;
    if (wp->next) {
        uint32_t diff = abs(wp->ele - wp->next->ele);
        height_graph_data[wp->num].color = BLUE;
        if (diff >= 10)
            height_graph_data[wp->num].color = RED;
        else if (diff > 1)
            height_graph_data[wp->num].color = GREEN;
    }
    if (wp->ele < height_min)
        height_min = wp->ele;
    if (wp->ele > height_max)
        height_max = wp->ele;

    if (map->tile_zoom > 14)
        wp->line_thickness = 3;
    else
        wp->line_thickness = 1;
    wp->color = BLUE;
}

void load_waypoint_file(char* filename)
{
    uint64_t start = esp_timer_get_time();

    async_file_t wp_file;
    wp_file.filename = filename;
    wp_file.loaded = 0;
    if (PM_OK == createFileBuffer(&wp_file))
        loadFile(&wp_file);

    if (wp_file.loaded == LOADED) {
        gpx_data = gpx_parser(wp_file.dest, map_add_waypoint);
        map_set_first_waypoint(gpx_data->waypoints);
        RTOS_Free(wp_file.dest);

        // populate height data
        height_graph_data = RTOS_Malloc(sizeof(graph_point_t) * gpx_data->waypoints_num + 1);
        map_run_on_waypoints(populate_height_data_prepare_waypoints);
        ESP_LOGI(TAG, "Load waypoint information done. Took: %lu ms", (uint32_t)(esp_timer_get_time() - start) / 1000);
    } else {
        ESP_LOGI(TAG, "Load waypoint information failed. Took: %lu ms", (uint32_t)(esp_timer_get_time() - start) / 1000);
    }

    ESP_LOGI(TAG, "Heap Free: %lu Byte", xPortGetFreeHeapSize());
}

void map_screen_create(const display_t* display)
{
    dsp = display;
    /* register pre_render callback */
    add_pre_render_callback(map_pre_render_cb);

    /* 3x3 tiles the middle tile is centered */
    map = map_create(-159, -85, 3, 3, 256, &f8x8);
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
    scaleBox = label_create("100m", &f8x8, 10, dsp->size.height - 15 - 45, 63, 13);
    scaleBox->borderWidth = 1;
    scaleBox->borderLines = LEFT_SOLID | RIGHT_SOLID | BOTTOM_SOLID;
    // scaleBox->backgroundColor = WHITE;
    scaleBox->borderColor = BLACK;
    scaleBox->textColor = BLACK;
    scaleBox->alignVertical = BOTTOM;
    scaleBox->alignHorizontal = CENTER;
    add_to_render_pipeline(label_render, scaleBox, RL_TOP);

    map_copyright = label_create("(c) OpenStreetMap contributors", &f8x8, 0, dsp->size.height - 10 - 45, 0, 0);
    label_shrink_to_text(map_copyright);
    map_copyright->box.left = dsp->size.width - map_copyright->box.width;
    add_to_render_pipeline(label_render, map_copyright, RL_GUI_ELEMENTS);

    map_update_zoom_level(map, zoom_level[zoom_level_selected]);
    map_tile_attach_onBeforeRender_callback(map, load_map_tile_on_demand);
    map_tile_attach_onAfterRender_callback(map, check_if_map_tile_is_loaded);

    load_waypoint_file("//track.gpx");

    if (height_graph_data) {
        graph = graph_create(0, display->size.height - 45, display->size.width, 45, height_graph_data, gpx_data->waypoints_num, &f8x8);
        graph_set_range(graph, height_min, height_max);
        graph->current_position_color = BLUE;
        graph->line_color = BLACK;
        graph->background_color = WHITE;

        add_to_render_pipeline(graph_renderer, graph, RL_GUI_ELEMENTS);
    } else {
        // move scalebox and copyright notice down
        map_copyright->box.top += 29;
        scaleBox->box.top += 29;
    }

    infoBox = label_create("", &f8x8, 0, dsp->size.height - 14,
        dsp->size.width - 1, 13);
    infoBox->alignVertical = MIDDLE;
    infoBox->alignHorizontal = RIGHT;
    infoBox->backgroundColor = WHITE;
    infoBox->onBeforeRender = updateInfoText;
    infoBox->text = RTOS_Malloc(INFOBOX_STRLEN);
    add_to_render_pipeline(label_render, infoBox, RL_GUI_ELEMENTS);
}

void toggleZoom()
{
    ESP_LOGI(TAG, "Zoom level was: %d", zoom_level[zoom_level_selected]);
    zoom_level_selected = !zoom_level_selected;
    ESP_LOGI(TAG, "Zoom level is: %d", zoom_level[zoom_level_selected]);
    map_update_zoom_level(map, zoom_level[zoom_level_selected]);
    map_update_position(map, map_position);

    scaleBox->box.width = zoom_level_scaleBox_width[zoom_level_selected];
    scaleBox->text = zoom_level_scaleBox_text[zoom_level_selected];

    trigger_rendering();
}