#ifndef PLATINENMACHER_DISPLAY_GUI_WAYPOINT_H_
#define PLATINENMACHER_DISPLAY_GUI_WAYPOINT_H_

#include "display.h"
#include "gui/geometric.h"
#include "colors.h"

typedef struct Waypoint waypoint_t;
struct Waypoint {
    void *child;			/// Pointer to child element.
    color_t color;
    float lat;
    float lon;
    uint32_t tile_x;
    uint32_t tile_y;
    uint16_t pos_x;
    uint16_t pos_y;
    uint16_t num;
    uint8_t active;
    error_code_t (*onBeforeRender)(const display_t *dsp, void *label);
	error_code_t (*onAfterRender)(const display_t *dsp, void *label);

    waypoint_t *next;
};

error_code_t waypoint_render_marker(const display_t* dsp, void* comp);

#endif