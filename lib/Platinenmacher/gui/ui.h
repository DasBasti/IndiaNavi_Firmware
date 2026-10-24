/*
 * Parts shared by the screens
 *
 * The button sits at the right edge of the device, at the top. Every screen keeps that corner free
 * for the button hint, a box that names what the button does.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PLATINENMACHER_DISPLAY_GUI_UI_H_
#define PLATINENMACHER_DISPLAY_GUI_UI_H_

#include "display.h"
#include "font.h"

#define UI_BORDER 2 // outline of boxes and segments
#define UI_PAD 4    // white space between a border and the text
#define UI_ARROW_SIZE 12
#define UI_SEGMENTS 10 // a segment bar is a tenth per segment
#define UI_SEGMENT_GAP 3

/** Width of the button hint with this text (and sub text, may be NULL) */
int16_t ui_button_hint_width(font_t* font, const char* text, font_t* sub_font, const char* sub);
/** Height of the button hint, with or without sub text */
int16_t ui_button_hint_height(font_t* font, font_t* sub_font, const char* sub);

/**
 * Box at the right edge of the display, below top, with a red arrow that points to the button.
 *
 * Draws the text with the given font, below it the optional sub text with its own (smaller) font.
 */
error_code_t ui_button_hint(const display_t* dsp, int16_t top, font_t* font, const char* text,
    font_t* sub_font, const char* sub);

/** Box with an outline of UI_BORDER */
error_code_t ui_box(const display_t* dsp, int16_t left, int16_t top, uint16_t width, uint16_t height,
    color_t fill);

/**
 * Bar of UI_SEGMENTS segments. A segment is filled when its tenth is reached,
 * 55% fills five, 100% fills all of them.
 */
error_code_t ui_segment_bar(const display_t* dsp, int16_t left, int16_t top, uint16_t width, uint16_t height,
    uint8_t percent, color_t color);

/** Number of filled segments for a percentage, 0 to UI_SEGMENTS */
uint8_t ui_segments_filled(uint8_t percent);

#endif /* PLATINENMACHER_DISPLAY_GUI_UI_H_ */
