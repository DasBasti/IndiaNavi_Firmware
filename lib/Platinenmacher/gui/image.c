/*
 * image.c
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#include "gui/image.h"
#include "display.h"

image_t *image_create(uint8_t *data, int16_t left, int16_t top,
					  uint16_t width, uint16_t height)
{
	image_t *image = RTOS_Malloc(sizeof(image_t));
	image->data = data;
	image->box.height = height;
	image->box.width = width;
	image->box.top = top;
	image->box.left = left;
	image->loaded = 1;
	image->onBeforeRender = NULL;
	image->onAfterRender = NULL;

	return image;
}

/**
 * Draws the image to the display if data is not NULL
 */
error_code_t image_render(const display_t *dsp, void *component)
{
	image_t *image = (image_t *)component;
	if (image->onBeforeRender)
		if(image->onBeforeRender(dsp, image) != PM_OK)
		{
			return ABORT;
		}

	if (image->data != NULL)
		display_draw_image(dsp, image->data, image->box.left,
						   image->box.top, image->box.width, image->box.height);

	if (image->onAfterRender)
		if(image->onAfterRender(dsp, image) != PM_OK)
		{
			return ABORT;
		}

	return PM_OK;
}
