/*
 * Screen for an empty battery
 *
 * Drawn before the device goes to deep sleep. The e-ink display keeps the
 * image, so it is still visible while the device is off.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui.h"
#include "navi/button.h"
#include "navi/power.h"

/* battery symbol in the middle of the screen */
#define BATTERY_WIDTH 180
#define BATTERY_HEIGHT 90
#define BATTERY_BORDER 6
#define BATTERY_TIP_WIDTH 14
#define BATTERY_TIP_HEIGHT 36
#define BATTERY_REST_WIDTH 12 // the little charge that is left

static label_t* title;
static label_t* hint;

static char* hints[] = {
    "Connect the charger",
    "Charging - push the button to start",
};

void battery_empty_screen_free(void)
{
    free_all_render_pipelines();
    RTOS_Free(title);
    title = NULL;
    RTOS_Free(hint);
    hint = NULL;
}

static error_code_t render_background(const display_t* dsp, void* comp)
{
    return display_fill(dsp, WHITE);
}

static error_code_t render_battery(const display_t* dsp, void* comp)
{
    int16_t left = (dsp->size.width - BATTERY_WIDTH - BATTERY_TIP_WIDTH) / 2;
    int16_t top = (dsp->size.height - BATTERY_HEIGHT) / 2 - 40;

    display_rect_fill(dsp, left, top, BATTERY_WIDTH, BATTERY_HEIGHT, BLACK);
    display_rect_fill(dsp, left + BATTERY_BORDER, top + BATTERY_BORDER,
        BATTERY_WIDTH - 2 * BATTERY_BORDER, BATTERY_HEIGHT - 2 * BATTERY_BORDER, WHITE);
    display_rect_fill(dsp, left + BATTERY_WIDTH, top + (BATTERY_HEIGHT - BATTERY_TIP_HEIGHT) / 2,
        BATTERY_TIP_WIDTH, BATTERY_TIP_HEIGHT, BLACK);
    display_rect_fill(dsp, left + 2 * BATTERY_BORDER, top + 2 * BATTERY_BORDER,
        BATTERY_REST_WIDTH, BATTERY_HEIGHT - 4 * BATTERY_BORDER, RED);
    return PM_OK;
}

static error_code_t hint_onBeforeRender(const display_t* dsp, void* label)
{
    ((label_t*)label)->text = hints[power_is_charging() ? 1 : 0];
    return PM_OK;
}

/* only with the charger, the battery would be empty again right away */
static void turn_on_while_charging(void)
{
    if (!power_is_charging())
        return;
    gui_set_app_mode(APP_MODE_GPS_CREATE);
    trigger_rendering();
}

void battery_empty_screen_create(const display_t* dsp)
{
    add_to_render_pipeline(render_background, NULL, RL_BACKGROUND);
    add_to_render_pipeline(render_battery, NULL, RL_GUI_ELEMENTS);

    int16_t text_top = dsp->size.height / 2 + 30;
    title = label_create("Battery empty", &f8x16, 0, text_top, dsp->size.width, 24);
    if (title) {
        title->alignHorizontal = CENTER;
        title->alignVertical = MIDDLE;
        title->backgroundColor = WHITE;
        add_to_render_pipeline(label_render, title, RL_GUI_ELEMENTS);
    }

    hint = label_create(hints[0], &f8x8, 0, text_top + 30, dsp->size.width, 16);
    if (hint) {
        hint->alignHorizontal = CENTER;
        hint->alignVertical = MIDDLE;
        hint->backgroundColor = WHITE;
        hint->onBeforeRender = hint_onBeforeRender;
        add_to_render_pipeline(label_render, hint, RL_GUI_ELEMENTS);
    }

    set_screen_free_function(battery_empty_screen_free);
    set_short_press_event(turn_on_while_charging);
}
