/*
 * Map screen component for GUI
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui.h"
#include "tasks.h"

#include "esp_log.h"

static const display_t *dsp;
static image_t *world;
    uint8_t* world_data;

static const char* tAG = "conway";

static void update_world(const display_t *dsp, void *image)
{
    ESP_LOGI(TAG, "render!");
}

void start_screen_create(const display_t *display)
{
    dsp = display;
    world_data = RTOS_Malloc(sizeof(*world_data)*dsp->size.width*dsp->size.height);
    world = image_create(world_data, 0,0,dsp->size.width, dsp->size.height);
    world->onBeforeRender = update_world;

    add_to_render_pipeline(image_render, world, RL_MAP);

}

void start_screen_free()
{
    free_all_render_pipelines();
}