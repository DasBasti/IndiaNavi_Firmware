/*
 * Graph component for showing time based data
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PLATINENMACHER_GUI_GRAPH_H
#define PLATINENMACHER_GUI_GRAPH_H

#include "geometric.h"
#include "error.h"
#include "display.h"
#include "label.h"
#include "font.h"

typedef struct {
    rect_t box;
    uint16_t min;
    uint16_t max;
    uint16_t data_len;
    uint16_t *data;
    uint16_t current_position;
    label_t *min_label;
    label_t *max_label;
    font_t *font;
} graph_t;

graph_t* graph_create(int16_t left, int16_t top, uint16_t width, uint16_t height, uint16_t* data, uint16_t data_len, font_t*font);
error_code_t graph_renderer(const display_t *dsp, void *component);
error_code_t graph_set_range(graph_t* graph, uint16_t min, uint16_t max);


#endif //PLATINENMACHER_GUI_GRAPH_H