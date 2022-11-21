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

#include <stdio.h>

char min_str[10], max_str[10];

graph_t* graph_create(int16_t left, int16_t top, uint16_t width, uint16_t height, float* data, uint16_t data_len, font_t* font)
{
    graph_t* graph = RTOS_Malloc(sizeof(graph_t));
    graph->box.left = left;
    graph->box.top = top;
    graph->box.width = width;
    graph->box.height = height;
    graph->data = data;
    graph->data_len = data_len;
    graph->font = font;

    graph->max_label = label_create(max_str, graph->font, left + 2, top + 2, 0, 8);
    graph->min_label = label_create(min_str, graph->font, left + 2, top + height - 2 - 8, 0, 8);

    return graph;
}

error_code_t graph_renderer(const display_t* dsp, void* component)
{
    if (!component)
        return PM_FAIL;

    graph_t* graph = (graph_t*)component;
    
    if(graph->background_color != TRANSPARENT)
        display_rect_fill(dsp, graph->box.left, graph->box.top, graph->box.width, graph->box.height, graph->background_color);
    display_rect_draw(dsp, graph->box.left, graph->box.top, graph->box.width, graph->box.height, BLACK);

    if (graph->data_len < 2)
        return OUT_OF_BOUNDS;

    uint16_t inner_box_top = graph->box.top + 1;
    uint16_t inner_box_left = graph->box.left + 1;
    uint16_t inner_box_width = graph->box.width - 2;
    uint16_t inner_box_height = graph->box.height - 2;

    float x_step = inner_box_width / (float)(graph->data_len - 1);
    float y_step = inner_box_height / (float)(graph->max - graph->min);
    uint16_t last_x = 0, last_y = 0;

    // TODO: there can be more data points than pixel on the screen!
    for (uint16_t i = 0; i < graph->data_len; i++) {
        int32_t val = (uint32_t)(graph->data[i]) - graph->min;
        if (val < 0)
            val = 0;
        uint16_t new_x = inner_box_left + (uint16_t)(i * x_step);                     // x values grow in step;
        uint16_t new_y = inner_box_top + inner_box_height - (uint16_t)ceilf((val)*y_step); // y values are scaled from min to max
        if (i != 0){
            uint16_t delta_y = abs(last_y - new_y);
            color_t line_color = graph->line_color;
            if(delta_y > 2)
                line_color = RED;
            else if (delta_y > 1)
                line_color = GREEN;
            else if (delta_y == 1)
                line_color = BLUE;
            else if (delta_y == 0)
                line_color = BLACK;
            display_line_draw(dsp, last_x, last_y, new_x, new_y, line_color);
            display_line_draw(dsp, last_x, last_y-1, new_x, new_y, line_color);
        }
        last_x = new_x;
        last_y = new_y;
    }

    if (graph->current_position) {
        display_circle_fill(dsp,
            inner_box_left + (uint16_t)(graph->current_position * x_step),
            inner_box_top + inner_box_height - (uint16_t)(ceilf(graph->data[graph->current_position] - graph->min) * y_step),
            3, graph->current_position_color);
    }

    label_render(dsp, graph->max_label);
    label_render(dsp, graph->min_label);

    return PM_OK;
}

error_code_t graph_set_range(graph_t* graph, float min, float max)
{
    graph->min = floorf(min);
    graph->max = ceilf(max);
    // TODO: deuglify this!!!!
    snprintf(min_str, 10, "%dm", graph->min);
    snprintf(max_str, 10, "%dm", graph->max);

    return PM_OK;
}

error_code_t graph_update_data(graph_t* graph, float* data, uint16_t len)
{
    if (!data)
        return PM_FAIL;

    if (len < 2)
        return OUT_OF_BOUNDS;

    graph->data = data;
    graph->data_len = len;

    return PM_OK;
}