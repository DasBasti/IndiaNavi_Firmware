#ifndef PLATINENMACHER_DISPLAY_GUI_WAYPOINT_H_
#define PLATINENMACHER_DISPLAY_GUI_WAYPOINT_H_

#include "colors.h"
#include "display.h"
#include "gui/geometric.h"

typedef struct Waypoint waypoint_t;
struct Waypoint {
    void *child;			/// Pointer to child element.
    color_t color;
    uint8_t line_thickness;
    float lat;
    float lon;
    float ele;
    uint32_t tile_x;
    uint32_t tile_y;
    int16_t pos_x;
    int16_t pos_y;
    uint16_t num;
    uint8_t active;
    error_code_t (*onBeforeRender)(const display_t *dsp, void *label);
	error_code_t (*onAfterRender)(const display_t *dsp, void *label);

    waypoint_t *next;
};

error_code_t waypoint_render_marker(const display_t* dsp, void* comp);

#endif