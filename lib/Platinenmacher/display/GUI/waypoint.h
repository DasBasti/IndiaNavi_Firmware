#ifndef PLATINENMACHER_DISPLAY_GUI_WAYPOINT_H_
#define PLATINENMACHER_DISPLAY_GUI_WAYPOINT_H_

#include "display/display.h"
#include "display/GUI/geometric.h"
#include "display/colors.h"

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
    error_code_t (*onBeforeRender)(display_t *dsp, void *label);
	error_code_t (*onAfterRender)(display_t *dsp, void *label);

    waypoint_t *next;
};

#endif