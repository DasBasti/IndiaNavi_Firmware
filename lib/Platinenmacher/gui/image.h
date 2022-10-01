/*
 * image.h
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_DISPLAY_GUI_IMAGE_H_
#define PLATINENMACHER_DISPLAY_GUI_IMAGE_H_

#include "error.h"
#include "gui/geometric.h"
#include "display.h"

enum LoadStatus
{
	NOT_LOADED,
	LOADING,
	LOADED,
	NOT_FOUND,
	ERROR,
};

typedef struct
{
	uint8_t *data;
	rect_t box;

	void *child;  /// Pointer to child element.
	void *parent; /// Pointer to parent element.
	enum LoadStatus loaded;

	error_code_t (*onBeforeRender)(const display_t *dsp, void *image);
	error_code_t (*onAfterRender)(const display_t *dsp, void *image);
} image_t;

image_t *image_create(const uint8_t *data, int16_t left, int16_t top,
					  uint16_t width, uint16_t height);
error_code_t image_render(display_t *dsp, void *image);

#endif /* PLATINENMACHER_DISPLAY_GUI_IMAGE_H_ */
