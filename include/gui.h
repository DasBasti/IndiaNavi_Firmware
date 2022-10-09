/*
 * gui.h
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#ifndef INC_GUI_H_
#define INC_GUI_H_

#include "Platinenmacher.h"

/* queue_element to load */
typedef struct
{
    image_t* tile;
    char filename[25];
    uint8_t index;
    uint8_t status;
} image_request_t;

typedef enum {
    APP_MODE_NONE,
    APP_TEST_SCREEN,
    APP_START_SCREEN,
    APP_START_SCREEN_TRANSITION,
    APP_MODE_GPS_CREATE,
    APP_MODE_DOWNLOAD,
    APP_MODE_RUNNING,
} app_mode_t;

label_t* clock_label;
label_t* battery_label;
label_t* north_indicator_label;
label_t* wifi_indicator_label;
label_t* gps_indicator_label;
label_t* sd_indicator_label;

/* the global position object */
map_position_t* map_position;

typedef struct Render render_t;
struct Render {
    error_code_t (*render)(const display_t* dsp, void* component);
    void* comp;
    render_t* next;
};

/**
 * Render layers.
 * 
 * Each layer pipeline is called after the other until RL_MAX is reaced.
 */
enum RenderLayer {
    RL_PRE_RENDER,
    RL_BACKGROUND,
    RL_MAP,
    RL_PATH,
    RL_GUI_BACKGROUND,
    RL_GUI_ELEMENTS,
    RL_TOP,
    RL_MAX, // <- Number of Layers
};

void trigger_rendering();
void free_render_pipeline(enum RenderLayer layer);
void free_all_render_pipelines();
void wait_until_gui_ready();
render_t* add_to_render_pipeline(error_code_t (*render)(const display_t* dsp, void* component),
    void* comp,
    enum RenderLayer layer);
render_t* add_pre_render_callback(error_code_t (*cb)(const display_t* dsp, void* component));

void gui_set_app_mode(app_mode_t mode);

void start_screen_create(const display_t* display);
void test_screen_create(const display_t* display);
void start_screen_free();
void map_screen_create(const display_t* display);

/**
 * Callbacks for loading map tiles 
 */
error_code_t load_map_tile_on_demand(const display_t* dsp, void* image);
error_code_t check_if_map_tile_is_loaded(const display_t* dsp, void* image);

#endif /* INC_GUI_H_ */
