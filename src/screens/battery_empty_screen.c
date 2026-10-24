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
#define HINT_TOP 8
#define LINE_GAP 8
#define MARGIN 24

static void battery_empty_screen_free(void)
{
    free_all_render_pipelines();
}

static error_code_t render_background(const display_t* dsp, void* comp)
{
    return display_fill(dsp, WHITE);
}

static error_code_t render_battery(const display_t* dsp, void* comp)
{
    int16_t left = (dsp->size.width - BATTERY_WIDTH - BATTERY_TIP_WIDTH) / 2;
    int16_t top = (dsp->size.height - BATTERY_HEIGHT) / 2 - 80;

    display_rect_fill(dsp, left, top, BATTERY_WIDTH, BATTERY_HEIGHT, BLACK);
    display_rect_fill(dsp, left + BATTERY_BORDER, top + BATTERY_BORDER,
        BATTERY_WIDTH - 2 * BATTERY_BORDER, BATTERY_HEIGHT - 2 * BATTERY_BORDER, WHITE);
    display_rect_fill(dsp, left + BATTERY_WIDTH, top + (BATTERY_HEIGHT - BATTERY_TIP_HEIGHT) / 2,
        BATTERY_TIP_WIDTH, BATTERY_TIP_HEIGHT, BLACK);
    display_rect_fill(dsp, left + 2 * BATTERY_BORDER, top + 2 * BATTERY_BORDER,
        BATTERY_REST_WIDTH, BATTERY_HEIGHT - 4 * BATTERY_BORDER, RED);
    return PM_OK;
}

static void draw_centered(const display_t* dsp, font_t* font, int16_t top, const char* text, color_t color)
{
    int16_t width = font_text_pixel_width(font, text);
    display_text_draw(dsp, font, (dsp->size.width - width) / 2, top, text, color);
}

/*
 * Title in red, what to do in plain words, below it how far the charge is. With the charger
 * the battery level shows in segments, a tenth each, up to the level where the display starts.
 */
static error_code_t render_text(const display_t* dsp, void* comp)
{
    int16_t y = (dsp->size.height - BATTERY_HEIGHT) / 2 - 80 + BATTERY_HEIGHT + 56;
    bool charging = power_is_charging();
    char line[48];

    draw_centered(dsp, &f8x16, y, "BATTERY EMPTY", RED);
    y += f8x16.height + 2 * LINE_GAP;
    draw_centered(dsp, &f8x16, y, charging ? "Charging" : "Connect a charger.", BLACK);
    y += f8x16.height + LINE_GAP;
    snprintf(line, sizeof(line), "The device starts at %d%%.", DISPLAY_MIN_BATTERY_LEVEL);
    draw_centered(dsp, &f8x8, y, line, BLACK);
    y += f8x8.height + 2 * LINE_GAP;

    int32_t level = power_state_known() ? power_battery_level() : 0;
    ui_segment_bar(dsp, MARGIN, y, dsp->size.width - 2 * MARGIN, 16,
        charging && level > 0 ? (uint8_t)(level > 100 ? 100 : level) : 0, GREEN);

    // without the charger a press does nothing, so there is no hint
    if (charging)
        ui_button_hint(dsp, HINT_TOP, &f8x16, "PRESS", &f8x8, "to start");
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
    add_to_render_pipeline(render_text, NULL, RL_GUI_ELEMENTS);

    set_screen_free_function(battery_empty_screen_free);
    set_short_press_event(turn_on_while_charging);
}
