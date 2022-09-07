/*
 * display.h
 *
 *  Created on: Jan 2, 2021
 *      Author: bastian
 */

#ifndef __DISPLAY_H_
#define __DISPLAY_H_

#include <stddef.h>

#include "colors.h"
#include "font.h"
#include "gui/geometric.h"
#include "error.h"
#include "memory.h"

typedef enum
{
	DISPLAY_ROTATE_0 = 0, /**< Rotate 0 degrees, clockwise. */
	DISPLAY_ROTATE_90,	  /**< Rotate 90 degrees, clockwise. */
	DISPLAY_ROTATE_180,	  /**< Rotate 180 degrees, clockwise. */
	DISPLAY_ROTATE_270	  /**< Rotate 270 degrees, clockwise. */
} display_rotation_t;

typedef struct display
{
	rect_t size;
	uint8_t *fb; // -> Framebuffer (size.width * size.height *bbp / 8) bytes
	uint32_t fb_size;
	uint8_t bpp; // -> Bits per pixel
	display_rotation_t rotation;
	error_code_t (*write_pixel)(struct display *dsp, int16_t x, int16_t y,
								uint8_t color);
	uint8_t (*decompress)(rect_t *size, int16_t x, int16_t y, uint8_t *data);

	void (*update)();
} display_t;

inline size_t sizeof_fb(rect_t size, uint8_t bpp)
{
	return (size.width * size.height * bpp / 8);
}

display_t *display_init(uint16_t width, uint16_t height, uint8_t bbp,
						display_rotation_t rotation);
error_code_t display_fill(display_t *dsp, color_t color);
error_code_t display_pixel_draw(display_t *dsp, int16_t x, int16_t y,
								color_t color);
error_code_t display_rect_draw(display_t *dsp, int16_t x, int16_t y,
							   uint16_t width, uint16_t height, uint8_t color);
error_code_t display_line_draw(display_t *dsp, int16_t x1, int16_t y1,
							   int16_t x2, int16_t y2, uint8_t color);
error_code_t display_circle_draw(display_t *dsp, int16_t x, int16_t y,
								 uint16_t r, uint8_t color);
error_code_t display_circle_draw_segment(display_t *dsp, int16_t x0,
										 int16_t y0, uint16_t r, uint8_t color, uint8_t segment);
error_code_t display_rect_fill(display_t *dsp, int16_t x, int16_t y,
							   uint16_t width, uint16_t height, uint8_t color);
error_code_t display_circle_fill(display_t *dsp, int16_t x, int16_t y,
								 uint16_t r, uint8_t color);

error_code_t display_text_draw(display_t *dsp, font_t *font, int16_t x0,
							   int16_t y0, const char *text, uint8_t color);
error_code_t display_text_draw_len(display_t *dsp, font_t *font, int16_t x0,
								   int16_t y0, uint8_t *text, uint32_t len);
error_code_t display_draw_image(display_t *dsp, uint8_t *data, int16_t x, int16_t y, uint16_t w, uint16_t h);
error_code_t display_commit_fb(display_t *dsp);
#endif /* __DISPLAY_H_ */
