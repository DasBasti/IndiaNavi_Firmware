/*
 * label.c
 *
 *  Created on: 04.01.2021
 *      Author: bastian
 */

#include "label.h"
#include "display/display.h"

/*
 * Create a label and returns a pointer to the label_t
 */
label_t *label_create(char *text, font_t *font, uint16_t left, uint16_t top,
					  uint16_t width, uint16_t height)
{
	label_t *label = RTOS_Malloc(sizeof(label_t));
	label->onBeforeRender = NULL;
	label->onAfterRender = NULL;
	label->text = text;
	label->font = font;
	label->box.left = left;
	label->box.top = top;
	label->box.width = width;
	label->box.height = height;
	label->backgroundColor = TRANSPARENT;
	label->textColor = BLACK;
	label->borderColor = BLACK;
	label->borderWidth = 0;
	label->borderLines = NO_BORDER;
	label->textPosition.left = 0;
	label->textPosition.top = 0;
	label->roundedCorners = 0;
	label->roundedRadius = 0;

	return label;
}

error_code_t label_render(display_t *dsp, void *component)
{
	label_t *label = (label_t *)component;
	if (label->onBeforeRender)
		if (label->onBeforeRender(dsp, label) != PM_OK)
		{
			return ABORT;
		}

	if (label->backgroundColor != TRANSPARENT)
		display_rect_fill(dsp, label->box.left, label->box.top,
						  label->box.width, label->box.height, label->backgroundColor);

	if (label->alignHorizontal == LEFT)
		label->textPosition.left = 1;
	if (label->alignHorizontal == CENTER)
	{
		label->textPosition.left = 1 + (label->box.width - font_text_pixel_width(label->font, label->text)) / 2;
	}
	if (label->alignHorizontal == RIGHT)
	{
		label->textPosition.left = label->box.width - font_text_pixel_width(label->font, label->text) - 1;
	}

	if (label->alignVertical == TOP)
		label->textPosition.top = 1;
	if (label->alignVertical == MIDDLE)
	{
		label->textPosition.top = 1 + (label->box.height - font_text_pixel_height(label->font, label->text)) / 2;
	}
	if (label->alignVertical == BOTTOM)
	{
		label->textPosition.top = (label->box.height - font_text_pixel_height(label->font, label->text)) - 1;
	}

	if (font_strlen(label->text))
		display_text_draw(dsp, label->font,
						  label->box.left + label->textPosition.left,
						  label->box.top + label->textPosition.top, label->text,
						  label->textColor);

	if (label->borderWidth)
	{
		/* Draw all borders */
		if (label->roundedCorners == 0)
		{
			if (label->borderLines & TOP_SOLID)
				display_line_draw(dsp, label->box.left, label->box.top,
								  label->box.left + label->box.width, label->box.top,
								  label->borderColor);

			if (label->borderLines & BOTTOM_SOLID)
				display_line_draw(dsp, label->box.left, label->box.top + label->box.height,
								  label->box.left + label->box.width,
								  label->box.top + label->box.height, label->borderColor);

			if (label->borderLines & LEFT_SOLID)
				display_line_draw(dsp, label->box.left, label->box.top,
								  label->box.left,
								  label->box.top + label->box.height, label->borderColor);

			if (label->borderLines & RIGHT_SOLID)
				display_line_draw(dsp, label->box.left + label->box.width, label->box.top,
								  label->box.left + label->box.width,
								  label->box.top + label->box.height, label->borderColor);
		}
		else
		{ // Set all 4 Lines onto the 4 corners

			/*  t1/l1 ------- t2/r1
			 *    |             |
			 *  b1/l2 ------- b2/r2
			 */

			point_t t1 = {label->box.left, label->box.top};
			point_t l1 = {label->box.left, label->box.top};

			point_t t2 = {label->box.left + label->box.width, label->box.top};
			point_t r1 = {label->box.left + label->box.width, label->box.top};

			point_t b1 = {label->box.left, label->box.top + label->box.height};
			point_t l2 = {label->box.left, label->box.top + label->box.height};

			point_t b2 = {label->box.left + label->box.width, label->box.top + label->box.height};
			point_t r2 = {label->box.left + label->box.width, label->box.top + label->box.height};

			if (label->roundedCorners & TOP_LEFT)
			{
				display_circle_draw_segment(dsp, t1.left + label->roundedRadius,
											t1.top + label->roundedRadius, label->roundedRadius,
											label->borderColor, 192);
				t1.left += label->roundedRadius;
				l1.top += label->roundedRadius;
			}

			if (label->roundedCorners & TOP_RIGHT)
			{
				display_circle_draw_segment(dsp, t2.left - label->roundedRadius,
											t2.top + label->roundedRadius, label->roundedRadius,
											label->borderColor, 3);
				t2.left -= label->roundedRadius;
				r1.top += label->roundedRadius;
			}

			if (label->roundedCorners & BOTTOM_LEFT)
			{
				display_circle_draw_segment(dsp, b1.left + label->roundedRadius,
											b1.top - label->roundedRadius, label->roundedRadius,
											label->borderColor, 48);
				b1.left += label->roundedRadius;
				l2.top -= label->roundedRadius;
			}

			if (label->roundedCorners & BOTTOM_RIGHT)
			{
				display_circle_draw_segment(dsp, b2.left - label->roundedRadius,
											b2.top - label->roundedRadius, label->roundedRadius,
											label->borderColor, 12);
				b2.left -= label->roundedRadius;
				r2.top -= label->roundedRadius;
			}

			display_line_draw(dsp, t1.left, t1.top, t2.left, t2.top,
							  label->borderColor);
			display_line_draw(dsp, b1.left, b1.top, b2.left, b2.top,
							  label->borderColor);
			display_line_draw(dsp, l1.left, l1.top, l2.left, l2.top,
							  label->borderColor);
			display_line_draw(dsp, r1.left, r1.top, r2.left, r2.top,
							  label->borderColor);
		}
	}
	if (label->onAfterRender)
		if (label->onAfterRender(dsp, label) != PM_OK)
		{
			return ABORT;
		}
	return PM_OK;
}

error_code_t label_shrink_to_text(label_t *label)
{
	label->box.width = font_text_pixel_width(label->font, label->text);
	label->box.height = font_text_pixel_height(label->font, label->text);
	return PM_OK;
}
