
#include "display.h"
#include "gui.h"
#include "gui/graph.h"
#include "tasks.h"

#include <esp_log.h>



void test_screen_create(const display_t* display)
{
    graph_t* graph = graph_create(0, display->size.height - 45, display->size.width, 0, NULL, 2, &f8x8);
    graph_set_range(graph, 0, 10);
    graph->current_position = 4;

    add_to_render_pipeline(graph_renderer, graph, RL_GUI_ELEMENTS);
}
