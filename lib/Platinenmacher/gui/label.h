/*
 * label.h
 *
 *  Created on: 04.01.2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_DISPLAY_GUI_LABEL_H_
#define PLATINENMACHER_DISPLAY_GUI_LABEL_H_

#include "display.h"
#include "geometric.h"
#include "colors.h"
#include "font.h"

typedef struct label {
	char *text;
	point_t textPosition;
	rect_t box;
	font_t *font;
	color_t textColor;
	color_t backgroundColor;
	uint8_t borderWidth;
	color_t borderColor;
	border_line_t borderLines;
	alignment_t alignVertical;
	alignment_t alignHorizontal;
	corner_t roundedCorners;
	uint8_t roundedRadius;
	void *child;			/// Pointer to child element.

	error_code_t (*onBeforeRender)(display_t *dsp, void *label);
	error_code_t (*onAfterRender)(display_t *dsp, void *label);
} label_t;

label_t* label_create(char *text, font_t *font, int16_t left, int16_t top,
		uint16_t width, uint16_t height);
error_code_t label_render(display_t *dsp, void *label);
error_code_t label_shrink_to_text(label_t *label);

#endif /* PLATINENMACHER_DISPLAY_GUI_LABEL_H_ */
