/*
 * Map screen component for GUI
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>

#include "gui/graph.h"
#include "gui/label.h"
#include "gui/map.h"

#include "parser/gpx.h"


#include "gps.h"
#include "gui.h"
#include "tasks.h"

#if !defined(TESTING) && !defined(LINUX)
#include "esp_timer.h"
#endif

#include <icons_16.h>

#if !defined(TESTING) && !defined(LINUX)
    #include "esp_timer.h"
#endif

static const display_t* dsp;

static map_t* map;
static label_t* scaleBox;
static label_t* positon_marker;
static label_t* map_copyright;
static label_t* infoBox;
static graph_t* graph;
static gpx_t* gpx_data;
static waypoint_t* closest_wp;
static float closest_wp_distance;
static float longitude_scale; // cos(latitude), a degree of longitude is shorter than one of latitude

/* scale box and copyright notice sit above the height graph, and lower without it */
#define HEIGHT_GRAPH_SHIFT 29
static int16_t scaleBox_top_with_graph;
static int16_t map_copyright_top_with_graph;

static uint8_t zoom_level_selected = 0;
static volatile bool zoom_toggle_requested = false;
uint8_t zoom_level[] = { 16, 14 };
uint8_t zoom_level_scaleBox_width[] = { 63, 77 };
char* zoom_level_scaleBox_text[] = { "100m", "500m" };
graph_point_t* height_graph_data;
static uint16_t height_graph_data_len;
float height_min = __FLT_MAX__, height_max = -__FLT_MAX__;

#define INFOBOX_STRLEN (uint32_t)(dsp->size.width / f8x8.width)
static const uint8_t offset_x = 159;
static const uint8_t offset_y = 85;
static const char* TAG = "map_screen";

/**
 * render cb for InfoText label
 */
static error_code_t updateInfoText(const display_t* dsp, void* comp)
{
    if (!map_position || !infoBox->text)
        return UNAVAILABLE;

    // a hidden track hides its name, the line shows the position as without a track
    if (gpx_data && gpx_data->track_name && display_settings_show_track()) {
        save_snprintf(infoBox->text, INFOBOX_STRLEN, "%s", gpx_data->track_name);
        infoBox->backgroundColor = TRANSPARENT;
        return PM_OK;
    }

    infoBox->backgroundColor = WHITE;
    if (map_position->fix != GPS_FIX_INVALID) {
        char lat = 'N';
        if (map_position->latitude < 0)
            lat = 'S';

        char lon = 'E';
        if (map_position->longitude < 0)
            lon = 'W';

        save_snprintf(infoBox->text, (INFOBOX_STRLEN), "GPS: %f%c %f%c %.02fm (HDOP:%.01f)",
            map_position->latitude, lat, map_position->longitude, lon, map_position->altitude, map_position->hdop);
    } else {
        save_snprintf(infoBox->text, (INFOBOX_STRLEN), "No GPS Signal found!");
    }
    return PM_OK;
}

error_code_t render_position_marker(const display_t* dsp, void* comp)
{
    if (!map_position)
        return UNAVAILABLE;

    label_t* label = (label_t*)comp;
    uint8_t hdop = floor(map_position->hdop / 2);
    hdop += 8;
    if (map_position->fix != GPS_FIX_INVALID) {
        label->box.left = -offset_x + map->pos_x - label->box.width / 2 + 256;
        label->box.top = -offset_y + map->pos_y - label->box.height / 2 + 256;

        uint16_t label_left = label->box.left + (label->box.width /2);
        uint16_t label_top = label->box.top + (label->box.height /2);

        // same color as the track, it can be changed in the app
        display_circle_fill(dsp, label_left, label_top, 6, display_settings_track_color());
        display_circle_fill(dsp, label_left, label_top, 2, WHITE);
        display_circle_draw(dsp, label_left, label_top, hdop, BLACK);
        return PM_OK;
    }
    return ABORT;
}

error_code_t updateSatsInView(const display_t* dsp, void* comp)
{
    if (!map_position)
        return UNAVAILABLE;
    if (gps_indicator_label) {
        if (gps_indicator_label->text)
            save_snprintf(gps_indicator_label->text, 5, "%d", map_position->satellites_in_view);
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
        // equirectangular approximation, squared distance in degrees of latitude
        float dlat = wp->lat - map_position->latitude;
        float dlon = (wp->lon - map_position->longitude) * longitude_scale;
        float distance = dlat * dlat + dlon * dlon;
        if (distance < closest_wp_distance) {
            closest_wp_distance = distance;
            closest_wp = wp;
        }
    }
}

void add_waypoints_to_renderer(waypoint_t* wp)
{
    if (wp->active) {
        // the color can be changed in the app
        wp->color = display_settings_track_color();
        add_to_render_pipeline(waypoint_render_marker, wp, RL_PATH);
    }
}

/* the arrows are drawn after the whole track, so its line does not cover them */
static void add_arrows_to_renderer(waypoint_t* wp)
{
    if (wp->active && wp->arrow_to)
        add_to_render_pipeline(waypoint_render_arrow, wp, RL_PATH);
}

static void apply_zoom_toggle(void)
{
    ESP_LOGI(TAG, "Zoom level was: %d", zoom_level[zoom_level_selected]);
    zoom_level_selected = !zoom_level_selected;
    ESP_LOGI(TAG, "Zoom level is: %d", zoom_level[zoom_level_selected]);
    map_update_zoom_level(map, zoom_level[zoom_level_selected]);

    scaleBox->box.width = zoom_level_scaleBox_width[zoom_level_selected];
    scaleBox->text = zoom_level_scaleBox_text[zoom_level_selected];
}

/**
 * The height graph can be switched off in the app, the notices move down then
 */
static bool height_graph_visible(void)
{
    return graph && display_settings_show_height_graph();
}

static void update_height_graph_layout(void)
{
    int16_t shift = height_graph_visible() ? 0 : HEIGHT_GRAPH_SHIFT;
    if (scaleBox)
        scaleBox->box.top = scaleBox_top_with_graph + shift;
    if (map_copyright)
        map_copyright->box.top = map_copyright_top_with_graph + shift;
}

static error_code_t height_graph_render(const display_t* dsp, void* component)
{
    if (!height_graph_visible())
        return NOT_NEEDED;
    return graph_renderer(dsp, component);
}

static error_code_t map_pre_render_cb(const display_t* dsp, void* component)
{
    update_height_graph_layout();

    // zoom is changed from the button task, apply it in the render task
    if (zoom_toggle_requested && map && scaleBox) {
        zoom_toggle_requested = false;
        apply_zoom_toggle();
    }

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
    // the track can be hidden in the app, the position marker stays
    if (display_settings_show_track()) {
        map_run_on_waypoints(add_waypoints_to_renderer);
        // arrows show in which direction the track goes
        waypoint_place_arrows(map_first_waypoint(), WAYPOINT_ARROW_SPACING, WAYPOINT_ARROW_LOOKAHEAD);
        map_run_on_waypoints(add_arrows_to_renderer);
    }

    closest_wp_distance = __FLT_MAX__;
    longitude_scale = cosf(map_position->latitude * (float)M_PI / 180.0f);
    closest_wp = NULL;
    map_run_on_waypoints(find_closest_waypoint);

    if (closest_wp && graph && closest_wp->num < graph->data_len)
        graph->current_position = closest_wp->num;

    return PM_OK;
}

void populate_height_data_prepare_waypoints(waypoint_t* wp)
{
    if (!height_graph_data || wp->num >= height_graph_data_len)
        return;
    height_graph_data[wp->num].value = wp->ele;
    if (wp->next) {
        uint32_t diff = abs((int)(wp->ele - wp->next->ele));
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
    #if !defined(TESTING) && !defined(LINUX)
    uint64_t start = esp_timer_get_time();

    async_file_t wp_file = { 0 };
    wp_file.filename = filename;
    if (PM_OK == createFileBuffer(&wp_file))
        loadFile(&wp_file);

    if (wp_file.loaded == LOADED) {
        // start with an empty waypoint list
        map_free_waypoints();
        gpx_data = gpx_parser(wp_file.dest, map_add_waypoint);
    }
    RTOS_Free(wp_file.dest);

    if (gpx_data) {
        map_set_first_waypoint(gpx_data->waypoints);

        // populate height data
        height_min = __FLT_MAX__;
        height_max = -__FLT_MAX__;
        if (gpx_data->waypoints_num) {
            height_graph_data = RTOS_Malloc_Large(sizeof(graph_point_t) * gpx_data->waypoints_num);
            if (height_graph_data)
                height_graph_data_len = gpx_data->waypoints_num;
        }
        map_run_on_waypoints(populate_height_data_prepare_waypoints);
        ESP_LOGI(TAG, "Load waypoint information done. Took: %lu ms", (uint32_t)(esp_timer_get_time() - start) / 1000);
    } else {
        ESP_LOGI(TAG, "Load waypoint information failed. Took: %lu ms", (uint32_t)(esp_timer_get_time() - start) / 1000);
    }

    ESP_LOGI(TAG, "Heap Free: %zu Byte", xPortGetFreeHeapSize());
#else
#endif
}

/**
 * Called from the button handler task. The map is only modified in the render task.
 */
void toggleZoom()
{
    zoom_toggle_requested = true;
    trigger_rendering();
}

/**
 * Free all components of the map screen
 */
static void map_screen_free(void)
{
    set_short_press_event(NULL);
    free_all_render_pipelines();

    map_free(map);
    map = NULL;
    RTOS_Free(positon_marker);
    positon_marker = NULL;
    RTOS_Free(scaleBox);
    scaleBox = NULL;
    RTOS_Free(map_copyright);
    map_copyright = NULL;
    if (infoBox) {
        RTOS_Free(infoBox->text);
        RTOS_Free(infoBox);
        infoBox = NULL;
    }
    if (graph) {
        RTOS_Free(graph->min_label);
        RTOS_Free(graph->max_label);
        RTOS_Free(graph);
        graph = NULL;
    }
    RTOS_Free(height_graph_data);
    height_graph_data = NULL;
    height_graph_data_len = 0;
    closest_wp = NULL;
    map_free_waypoints();
    gpx_free(gpx_data);
    gpx_data = NULL;
    if (gps_indicator_label)
        gps_indicator_label->onBeforeRender = NULL;
}

void map_screen_create(const display_t* display)
{
    dsp = display;
    /* register pre_render callback */
    add_pre_render_callback(map_pre_render_cb);

    /* 3x3 tiles */
    map = map_create(-offset_x, -offset_y, 3, 3, 256, &f8x8);
    if (!map) {
        ESP_LOGE(TAG, "Can not create map");
        return;
    }
    set_screen_free_function(map_screen_free);
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
    scaleBox->borderColor = BLACK;
    scaleBox->textColor = BLACK;
    scaleBox->alignVertical = BOTTOM;
    scaleBox->alignHorizontal = CENTER;
    add_to_render_pipeline(label_render, scaleBox, RL_TOP);

    map_copyright = label_create("By theBrutzler & Platinenmacher 2023", &f8x8, 0, dsp->size.height - 10 - 45, 0, 0);
    label_shrink_to_text(map_copyright);
    map_copyright->box.left = dsp->size.width - map_copyright->box.width;
    map_copyright->onBeforeRender = map_render_copyright;
    add_to_render_pipeline(label_render, map_copyright, RL_GUI_ELEMENTS);

    map_update_zoom_level(map, zoom_level[zoom_level_selected]);
    // attach zoom_level event to short click
    set_short_press_event(toggleZoom);

#ifdef ESP_S3
    map_attach_onBeforeRender_callback(map, load_map_tiles_to_permanent_memory);
#else
    map_tile_attach_onBeforeRender_callback(map, load_map_tile_on_demand);
    map_tile_attach_onAfterRender_callback(map, check_if_map_tile_is_loaded);
#endif // ESP_S3

    load_waypoint_file("//track.gpx");

    if (height_graph_data && height_graph_data_len >= 2) {
        graph = graph_create(0, display->size.height - 45, display->size.width, 45, height_graph_data, height_graph_data_len, &f8x8);
        graph_set_range(graph, height_min, height_max);
        graph->current_position_color = BLUE;
        graph->line_color = BLACK;
        graph->background_color = WHITE;

        add_to_render_pipeline(height_graph_render, graph, RL_GUI_ELEMENTS);
    }
    // without a graph the scalebox and copyright notice move down
    scaleBox_top_with_graph = scaleBox->box.top;
    map_copyright_top_with_graph = map_copyright->box.top;
    update_height_graph_layout();

    infoBox = label_create("", &f8x8, 0, dsp->size.height - 14,
        dsp->size.width - 1, 13);
    infoBox->alignVertical = MIDDLE;
    infoBox->alignHorizontal = RIGHT;
    infoBox->backgroundColor = WHITE;
    infoBox->onBeforeRender = updateInfoText;
    infoBox->text = RTOS_Malloc(INFOBOX_STRLEN);
    add_to_render_pipeline(label_render, infoBox, RL_GUI_ELEMENTS);
}
