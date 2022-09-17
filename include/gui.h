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
	image_t *tile;
	char filename[25];
	uint8_t index;
	uint8_t status;
} image_request_t;

typedef enum
{
	APP_MODE_NONE,
	APP_START_SCREEN,
	APP_START_SCREEN_TRANSITION,
	APP_MODE_GPS,
	APP_MODE_DOWNLOAD,
} app_mode_t;

static label_t *clock_label;
static label_t *battery_label;
static label_t *north_indicator_label;
static label_t *wifi_indicator_label;
static label_t *gps_indicator_label;
static label_t *sd_indicator_label;

typedef struct Render render_t;
struct Render
{
	error_code_t (*render)(const display_t *dsp, void *component);
	void *comp;
	render_t *next;
};

enum RenderLayer
{
	RL_BACKGROUND,
	RL_MAP,
	RL_PATH,
	RL_GUI_BACKGROUND,
	RL_GUI_ELEMENTS,
	RL_TOP,
	RL_MAX, // <- Number of Layers
};

void trigger_rendering();
void pre_render_cb();
void free_render_pipeline(enum RenderLayer layer);
void wait_until_gui_ready();
render_t *add_to_render_pipeline(error_code_t (*render)(const display_t *dsp, void *component),
								 void *comp,
								 enum RenderLayer layer); /* screens */
void app_screen(const display_t *dsp);
void gui_set_app_mode(app_mode_t mode);

void start_screen_create(const display_t *display);
void start_screen_free();
void map_screen_create(const display_t *display);

error_code_t load_map_tile_on_demand(const display_t *dsp, void *image);
error_code_t check_if_map_tile_is_loaded(const display_t *dsp, void *image);

#endif /* INC_GUI_H_ */
