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
    uint32_t world_x;       /// position on the world map, fraction of 2^32, set by map_add_waypoint
    uint32_t world_y;
    uint32_t tile_x;
    uint32_t tile_y;
    int16_t pos_x;
    int16_t pos_y;
    uint16_t num;
    uint8_t active;
    waypoint_t *arrow_to;   /// an arrow at this waypoint points towards this one, NULL for no arrow
    error_code_t (*onBeforeRender)(const display_t *dsp, void *label);
	error_code_t (*onAfterRender)(const display_t *dsp, void *label);

    waypoint_t *next;
};

#define WAYPOINT_ARROW_SPACING 80   /// pixels along the track from one direction arrow to the next
#define WAYPOINT_ARROW_LOOKAHEAD 12 /// pixels along the track the direction of an arrow is taken from

error_code_t waypoint_render_marker(const display_t* dsp, void* comp);
error_code_t waypoint_render_arrow(const display_t* dsp, void* comp);

/**
 * Choose the waypoints that get an arrow in the direction of the track.
 * visible are the active waypoints in the order of the track, their positions on the screen
 * (pos_x, pos_y, active) have to be up to date. The arrows of the other waypoints are not changed.
 */
void waypoint_place_arrows(waypoint_t* const* visible, uint32_t count, uint16_t spacing, uint16_t lookahead);

#endif