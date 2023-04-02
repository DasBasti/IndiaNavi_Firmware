/*
 * Off screen component for GUI
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui.h"
#include "tasks.h"

static const display_t *dsp;
static char* infoText;
static label_t *infoBox;

void off_screen_create(const display_t *display)
{
    dsp = display;
    infoText = RTOS_Malloc(dsp->size.width / f8x8.width);
    xSemaphoreTake(print_semaphore, portMAX_DELAY);
    sprintf(infoText, GIT_HASH);
    xSemaphoreGive(print_semaphore);

    infoBox = label_create(infoText, &f8x8, 0, dsp->size.height - 14,
        dsp->size.width - 1, 13);
    infoBox->borderWidth = 1;
    infoBox->borderLines = ALL_SOLID;
    infoBox->alignVertical = MIDDLE;
    infoBox->backgroundColor = WHITE;

    add_to_render_pipeline(label_render, infoBox, RL_GUI_ELEMENTS);

}

void off_screen_free()
{
    free_all_render_pipelines();
    RTOS_Free(infoText);
    RTOS_Free(infoBox);
}