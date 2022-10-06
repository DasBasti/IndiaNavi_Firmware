
#include "display.h"
#include "gui.h"
#include "gui/graph.h"

#define NUM_POINTS 20
uint16_t points[NUM_POINTS] = { 5, 4, 1, 0, 8, 7, 9, 10, 0, 5, 5, 4, 1, 0, 8, 7, 9, 10, 0, 5 };

void test_screen_create(const display_t* display)
{
    graph_t* graph = graph_create(0, display->size.height - 45, display->size.width, 45, points, NUM_POINTS, &f8x8);
    graph_set_range(graph, 0, 10);
    graph->current_position = 4;

    add_to_render_pipeline(graph_renderer, graph, RL_GUI_ELEMENTS);
}
