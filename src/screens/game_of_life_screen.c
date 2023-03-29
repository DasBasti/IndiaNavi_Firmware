/*
 * Conways Game of Life screen. For fun and profit.
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "colors.h"

#include "gui.h"
#include "tasks.h"

#include "esp_log.h"
#include "esp_random.h"

static const display_t* dsp;
static image_t* world;
static uint8_t* world_data;
static size_t world_width;
static size_t world_height;

static uint32_t generation;
char label_text[30];

static const char* TAG = "conway";

#define EMPTY WHITE
#define FOOD GREEN
#define ORGANISM BLACK
#define WATER BLUE
#define DIRT ORANGE

static size_t get_offset(uint32_t x, uint32_t y)
{
    return y * world_width + x;
}

static uint8_t get_pixel_from_offset(uint32_t x, uint32_t y)
{
    if (x >= world_width || y >= world_height)
        return EMPTY;

    size_t offset = get_offset(x, y);
    if (offset & 0x01) {
        return world_data[offset >> 1] & 0x0f;
    } else {
        return (world_data[offset >> 1] & 0xf0) >> 4;
    }
}

static void set_pixel_at_offset(uint32_t x, uint32_t y, uint8_t value)
{
    if (x >= world_width || y >= world_height)
        return;

    size_t offset = get_offset(x, y);
    size_t offset_shifted = offset >> 1;
    if (offset & 0x01) {
        world_data[offset_shifted] = (world_data[offset_shifted] & 0xf0) + (value & 0x0f);
    } else {
        world_data[offset_shifted] = (world_data[offset_shifted] & 0x0f) + ((value & 0x0f) << 4);
    }
}

static error_code_t update_world(const display_t* dsp, void* image)
{
    generation++;
    ESP_LOGI(TAG, "render generation %lu", generation);
    for (size_t x = 1; x < world_width; x++) {
        for (size_t y = 1; y < world_height; y++) {
            uint8_t neighbours = 0;
            if (get_pixel_from_offset(x - 1, y - 1) == ORGANISM)
                neighbours++;
            if (get_pixel_from_offset(x - 1, y) == ORGANISM)
                neighbours++;
            if (get_pixel_from_offset(x - 1, y + 1) == ORGANISM)
                neighbours++;
            if (get_pixel_from_offset(x, y - 1) == ORGANISM)
                neighbours++;
            if (get_pixel_from_offset(x, y + 1) == ORGANISM)
                neighbours++;
            if (get_pixel_from_offset(x + 1, y - 1) == ORGANISM)
                neighbours++;
            if (get_pixel_from_offset(x + 1, y) == ORGANISM)
                neighbours++;
            if (get_pixel_from_offset(x + 1, y + 1) == ORGANISM)
                neighbours++;

            if (get_pixel_from_offset(x, y) == ORGANISM && neighbours < 2)
                set_pixel_at_offset(x, y, EMPTY);
            else if (get_pixel_from_offset(x, y) == ORGANISM && neighbours > 3)
                set_pixel_at_offset(x, y, EMPTY);
            else if (get_pixel_from_offset(x, y) == EMPTY && neighbours == 3)
                set_pixel_at_offset(x, y, ORGANISM);
        }
    }
    save_sprintf(label_text, "Generation %lu", generation);

    return PM_OK;
}

void conway_screen_create(const display_t* display)
{
    dsp = display;
    world_height = dsp->size.height;
    world_width = dsp->size.width;
    world_data = RTOS_Malloc((dsp->size.width-2) * (dsp->size.height-2) / 2);
    world = image_create(world_data, 1, 1, world_width-2, world_height-2);
    world->onBeforeRender = update_world;

    for (size_t i = 0; i < (dsp->size.width-2) * (dsp->size.height-2) / 2; i++) {
        world_data[i] = esp_random() % 8 ? ORGANISM : EMPTY;
    }

    generation=0;
    add_to_render_pipeline(image_render, world, RL_MAP);

    
    label_t *l=label_create(label_text,&f8x8,0,display->size.height-f8x8.height-4,display->size.width,f8x8.height+4);
    l->backgroundColor = WHITE;
    l->alignHorizontal = CENTER;
    l->textColor = BLACK;
    add_to_render_pipeline(label_render, l, RL_GUI_ELEMENTS);
    
}

void conway_screen_free()
{
    free_all_render_pipelines();
    RTOS_Free(world_data);
}

