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

typedef struct
{
	uint32_t x;
	uint32_t y;
	image_t *image;
	label_t *label;
	uint8_t z;
	uint8_t loaded;
} map_tile_t;

/* 6 map tiles */
map_tile_t map_tiles[6];
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

void trigger_rendering();
void wait_until_gui_ready();
uint8_t add_to_render_pipeline(error_code_t (*render)(display_t *dsp, void *component), void *comp);
/* screens */
void start_screen(display_t *dsp);
void app_screen(display_t *dsp);

#endif /* INC_GUI_H_ */
