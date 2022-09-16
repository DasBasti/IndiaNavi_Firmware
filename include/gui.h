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
	APP_MODE_DOWNLOAD,
	APP_MODE_GPS,
} app_mode_t;

/* 6 map tiles */
label_t *positon_marker;

/* public GUI elements */
label_t *clock_label;
label_t *battery_label;
label_t *north_indicator_label;
label_t *wifi_indicator_label;
label_t *gps_indicator_label;
label_t *sd_indicator_label;
label_t *infoBox;
label_t *scaleBox;
map_t *map;

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
void start_screen(const display_t *dsp);
void app_screen(const display_t *dsp);
void gui_set_app_mode(app_mode_t mode);

error_code_t load_map_tile_on_demand(const display_t *dsp, void *image);
error_code_t check_if_map_tile_is_loaded(const display_t *dsp, void *image);

#endif /* INC_GUI_H_ */
