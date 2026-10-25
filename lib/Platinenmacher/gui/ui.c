/*
 * Parts shared by the screens
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui.h"

extern font_t f8x16;

/* the host tests are built for a black and white display */
#ifdef EINK_7COLOR
#define UI_ARROW_COLOR RED
#else
#define UI_ARROW_COLOR BLACK
#endif

static int16_t max16(int16_t a, int16_t b)
{
    return a > b ? a : b;
}

int16_t ui_button_hint_width(font_t* font, const char* text, font_t* sub_font, const char* sub)
{
    int16_t text_width = text ? (int16_t)font_text_pixel_width(font, text) : 0;
    if (sub && sub_font)
        text_width = max16(text_width, (int16_t)font_text_pixel_width(sub_font, sub));
    return UI_BORDER + UI_PAD + text_width + UI_PAD + UI_ARROW_SIZE + UI_PAD;
}

int16_t ui_button_hint_height(font_t* font, font_t* sub_font, const char* sub)
{
    int16_t height = UI_BORDER + UI_PAD + font->height + UI_PAD + UI_BORDER;
    if (sub && sub_font)
        height += sub_font->height + 2;
    return height;
}

error_code_t ui_box(const display_t* dsp, int16_t left, int16_t top, uint16_t width, uint16_t height,
    color_t fill)
{
    display_rect_fill(dsp, left, top, width, height, fill);
    for (int16_t i = 0; i < UI_BORDER; i++)
        display_rect_draw(dsp, left + i, top + i, width - 2 * i, height - 2 * i, BLACK);
    return PM_OK;
}

/* arrow that points up and to the right, 3 pixels thick */
static void draw_arrow(const display_t* dsp, int16_t left, int16_t top)
{
    const int16_t s = UI_ARROW_SIZE;
    display_rect_fill(dsp, left + 3, top, s - 3, 3, UI_ARROW_COLOR);         // head, top edge
    display_rect_fill(dsp, left + s - 3, top, 3, s - 3, UI_ARROW_COLOR);     // head, right edge
    for (int16_t i = 0; i < 3; i++)                               // shaft
        display_line_draw(dsp, left + i, top + s - 1, left + s - 3 + i, top + 2, UI_ARROW_COLOR);
}

error_code_t ui_button_hint(const display_t* dsp, int16_t top, font_t* font, const char* text,
    font_t* sub_font, const char* sub)
{
    int16_t width = ui_button_hint_width(font, text, sub_font, sub);
    int16_t height = ui_button_hint_height(font, sub_font, sub);
    // the box touches the right edge of the display, there the button is
    int16_t left = dsp->size.width - width;

    ui_box(dsp, left, top, width, height, WHITE);
    int16_t text_left = left + UI_BORDER + UI_PAD;
    int16_t text_top = top + UI_BORDER + UI_PAD;
    display_text_draw(dsp, font, text_left, text_top, text, BLACK);
    if (sub && sub_font)
        display_text_draw(dsp, sub_font, text_left, text_top + font->height + 2, sub, BLACK);
    draw_arrow(dsp, left + width - UI_BORDER - UI_PAD - UI_ARROW_SIZE, top + UI_BORDER + UI_PAD);
    return PM_OK;
}

uint8_t ui_segments_filled(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    return percent / (100 / UI_SEGMENTS);
}

error_code_t ui_segment_bar(const display_t* dsp, int16_t left, int16_t top, uint16_t width, uint16_t height,
    uint8_t percent, color_t color)
{
    uint8_t filled = ui_segments_filled(percent);
    int16_t segment_width = (width - (UI_SEGMENTS - 1) * UI_SEGMENT_GAP) / UI_SEGMENTS;
    for (uint8_t i = 0; i < UI_SEGMENTS; i++) {
        int16_t x = left + i * (segment_width + UI_SEGMENT_GAP);
        display_rect_fill(dsp, x, top, segment_width, height, i < filled ? color : WHITE);
        for (int16_t b = 0; b < UI_BORDER; b++)
            display_rect_draw(dsp, x + b, top + b, segment_width - 2 * b, height - 2 * b, BLACK);
    }
    return PM_OK;
}

error_code_t ui_progress_box(const display_t* dsp, int16_t left, int16_t top, uint16_t width, const char* text,
    uint64_t done, uint64_t total, color_t bar_color)
{
    display_rect_fill(dsp, left, top, width, UI_PROGRESS_HEIGHT, WHITE);
    display_rect_draw(dsp, left, top, width, UI_PROGRESS_HEIGHT, BLACK);

    int16_t text_width = font_text_pixel_width(&f8x16, text);
    display_text_draw(dsp, &f8x16, left + (width - text_width) / 2, top + 4, text, BLACK);

    // progress bar below the text
    int16_t bar_left = left + 5;
    int16_t bar_top = top + 26;
    int16_t bar_width = width - 10;
    int16_t bar_height = 12;
    if (done > total)
        done = total;
    display_rect_draw(dsp, bar_left, bar_top, bar_width, bar_height, BLACK);
    if (total)
        display_rect_fill(dsp, bar_left + 1, bar_top + 1,
            (uint32_t)((uint64_t)(bar_width - 2) * done / total), bar_height - 2, bar_color);
    return PM_OK;
}
