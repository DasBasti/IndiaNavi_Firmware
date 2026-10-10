/*
 * Graph component for showing time based data
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "graph.h"
#include "font.h"
#include "memory.h"

#include <stdint.h>
#include <stdio.h>

char min_str[10], max_str[10];

graph_t* graph_create(int16_t left, int16_t top, uint16_t width, uint16_t height, graph_point_t* data, uint16_t data_len, font_t* font)
{
    graph_t* graph = RTOS_Malloc(sizeof(graph_t));
    if (!graph)
        return NULL;
    graph->box.left = left;
    graph->box.top = top;
    graph->box.width = width;
    graph->box.height = height;
    graph->data = data;
    graph->data_len = data_len;
    graph->font = font;
    graph->background_color = WHITE;
    graph->current_position = 0;
    graph->static_data = false;
    graph->cache = NULL;

    graph->max_label = label_create(max_str, graph->font, left + 2, top + 2, 0, 8);
    graph->min_label = label_create(min_str, graph->font, left + 2, top + height - 2 - 8, 0, 8);

    return graph;
}

static bool graph_has_line(const graph_t* graph)
{
    return graph->data && graph->data_len >= 2 && graph->max > graph->min;
}

/* background, frame and line with the top left corner at left/top */
static void graph_draw(const display_t* dsp, const graph_t* graph, int16_t left, int16_t top)
{
    if (graph->background_color != TRANSPARENT)
        display_rect_fill(dsp, left, top, graph->box.width, graph->box.height, graph->background_color);
    display_rect_draw(dsp, left, top, graph->box.width, graph->box.height, BLACK);

    if (!graph_has_line(graph))
        return;

    uint16_t inner_box_top = top + 1;
    uint16_t inner_box_left = left + 1;
    uint16_t inner_box_width = graph->box.width - 2;
    uint16_t inner_box_height = graph->box.height - 2;

    float x_step = inner_box_width / (float)(graph->data_len - 1);
    float y_step = inner_box_height / (float)(graph->max - graph->min);
    uint16_t last_x = 0, last_y = 0;

    for (uint16_t i = 0; i < graph->data_len; i++) {
        float val = graph->data[i].value - graph->min;
        if (val < 0)
            val = 0;
        uint16_t new_x = inner_box_left + (uint16_t)(i * x_step);                          // x values grow in step;
        uint16_t new_y = inner_box_top + inner_box_height - (uint16_t)ceilf((val)*y_step); // y values are scaled from min to max
        if (i != 0) {
            display_line_draw(dsp, last_x, last_y, new_x, new_y, graph->data[i].color);
            display_line_draw(dsp, last_x, last_y - 1, new_x, new_y - 1, graph->data[i].color);
        }
        last_x = new_x;
        last_y = new_y;
    }
}

error_code_t graph_renderer(const display_t* dsp, void* component)
{
    if (!component)
        return PM_FAIL;

    graph_t* graph = (graph_t*)component;

    // a track has thousands of points, its line is drawn once and copied on every render
    if (graph->static_data && !graph->cache && (graph->cache = display_canvas_create(graph->box.width, graph->box.height)))
        graph_draw(graph->cache, graph, 0, 0);
    if (graph->cache)
        display_draw_image(dsp, graph->cache->fb, graph->box.left, graph->box.top, graph->box.width, graph->box.height);
    else
        graph_draw(dsp, graph, graph->box.left, graph->box.top);

    if (!graph_has_line(graph))
        return OUT_OF_BOUNDS;

    uint16_t inner_box_top = graph->box.top + 1;
    uint16_t inner_box_left = graph->box.left + 1;
    uint16_t inner_box_width = graph->box.width - 2;
    uint16_t inner_box_height = graph->box.height - 2;
    float x_step = inner_box_width / (float)(graph->data_len - 1);
    float y_step = inner_box_height / (float)(graph->max - graph->min);

    if (graph->current_position && graph->current_position < graph->data_len) {
        float val = graph->data[graph->current_position].value - graph->min;
        if (val < 0)
            val = 0;
        display_circle_fill(dsp,
            inner_box_left + (uint16_t)(graph->current_position * x_step),
            inner_box_top + inner_box_height - (uint16_t)ceilf(val * y_step),
            3, graph->current_position_color);
    }

    label_render(dsp, graph->max_label);
    label_render(dsp, graph->min_label);

    return PM_OK;
}

/* the line has to be drawn again */
static void graph_drop_cache(graph_t* graph)
{
    display_canvas_free(graph->cache);
    graph->cache = NULL;
}

error_code_t graph_set_range(graph_t* graph, float min, float max)
{
    if (min < INT16_MIN)
        min = INT16_MIN;
    if (max > INT16_MAX)
        max = INT16_MAX;
    graph->min = floorf(min);
    graph->max = ceilf(max);
    // a flat track still gets a line
    if (graph->max <= graph->min) {
        if (graph->min < INT16_MAX)
            graph->max = graph->min + 1;
        else
            graph->min = graph->max - 1;
    }
    graph_drop_cache(graph);
    // TODO: deuglify this!!!!
    snprintf(min_str, sizeof(min_str), "%dm", graph->min);
    snprintf(max_str, sizeof(max_str), "%dm", graph->max);

    return PM_OK;
}

error_code_t graph_update_data(graph_t* graph, graph_point_t* data, uint16_t len)
{
    if (!data)
        return PM_FAIL;

    if (len < 2)
        return OUT_OF_BOUNDS;

    graph->data = data;
    graph->data_len = len;
    graph_drop_cache(graph);

    return PM_OK;
}

void graph_free(graph_t* graph)
{
    if (!graph)
        return;
    graph_drop_cache(graph);
    RTOS_Free(graph->min_label);
    RTOS_Free(graph->max_label);
    RTOS_Free(graph);
}