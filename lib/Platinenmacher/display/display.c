/*
 * Display Driver
 *
 *  Created on: Jan 2, 2021
 *      Author: bastian
 */

#include "display.h"
#include "display/font.h"

/*
 * Intialize Display memory
 *
 * Generates display and framebuffer memory
 */
display_t *display_init(uint16_t width, uint16_t height, uint8_t bpp,
						display_rotation_t rotation)
{
	display_t *disp = RTOS_Malloc(sizeof(display_t));
	disp->size.width = width;
	disp->size.height = height;
	disp->bpp = bpp;
	disp->rotation = rotation;

	return disp;
}

/*
 * Commit FB content to hardware display
 */
error_code_t display_commit_fb(display_t *dsp)
{
	dsp->update(dsp);
	return PM_OK;
}

/**
 * @brief Fills ever pixel with color
 *
 * @return PM_OK
 *
 */
error_code_t display_fill(display_t *dsp, color_t color)
{
	for (int x = 0; x < dsp->size.width; x++)
		for (int y = 0; y < dsp->size.height; y++)
		{
			display_pixel_draw(dsp, x, y, color);
		}
	return PM_OK;
}
/**
 * @brief Draws pixel to framebuffer
 *
 * Draws a pixel with color to position x,y on the framebuffer
 *
 * @return PM_OK if driver is unloaded
 * @return OUT_OF_BOUNDS if one or more pixels where out of bounds
 *
 */
error_code_t display_pixel_draw(display_t *dsp, uint16_t x, uint16_t y,
								color_t color)
{
	if (x >= dsp->size.width || y >= dsp->size.height)
		return OUT_OF_BOUNDS;

	if (color == TRANSPARENT)
		return PM_OK;

	dsp->write_pixel(dsp, x, y, color);

	return PM_OK;
}

/**
 * @brief Draws a rectangle on the framebuffer
 *
 * Draws a rectangle on the framebuffer using the line function.
 *
 * @return PM_OK if driver is unloaded
 * @return OUT_OF_BOUNDS if one or more pixels where out of bounds
 *
 */
error_code_t display_rect_draw(display_t *dsp, uint16_t x, uint16_t y,
							   uint16_t width, uint16_t height, uint8_t color)
{
	display_line_draw(dsp, x, y, x, y + height, color);
	display_line_draw(dsp, x, y + height, x + width, y + height, color);
	display_line_draw(dsp, x + width, y + height, x + width, y, color);
	display_line_draw(dsp, x + width, y, x, y, color);

	return PM_OK;
}

static void display_line_draw_low(display_t *dsp, uint16_t x1, uint16_t y1,
								  uint16_t x2, uint16_t y2, uint8_t color)
{
	int32_t dX = x2 - x1;
	int32_t dY = y2 - y1;
	int32_t yi = 1;

	if (dY < 0)
	{
		yi = -1;
		dY = -dY;
	}

	int32_t D = (2 * dY) - dX;
	uint32_t y = y1;
	for (uint32_t x = x1; x <= x2; x++)
	{
		display_pixel_draw(dsp, x, y, color);
		if (D > 0)
		{
			y = y + yi;
			D = D - (2 * dX);
		}
		D = D + (2 * dY);
	}
}
static void display_line_draw_height(display_t *dsp, uint16_t x1, uint16_t y1,
									 uint16_t x2, uint16_t y2, uint8_t color)
{
	int32_t dX = x2 - x1;
	int32_t dY = y2 - y1;
	int32_t xi = 1;

	if (dX < 0)
	{
		xi = -1;
		dX = -dX;
	}

	int32_t D = (2 * dX) - dY;
	uint32_t x = x1;
	for (uint32_t y = y1; y <= y2; y++)
	{
		display_pixel_draw(dsp, x, y, color);
		if (D > 0)
		{
			x = x + xi;
			D = D - (2 * dY);
		}
		D = D + (2 * dX);
	}
}

#ifndef abs
int abs(int value)
{
	if (value < 0)
		return -value;

	return value;
}
#endif

/**
 * @brief Draws a line on the framebuffer
 *
 * Draws a line on the framebuffer starting from
 * x1,y1 to x2,y2 with color.
 *
 * @return PM_OK
 * @return OUT_OF_BOUNDS if one or more pixels where out of bounds
 *
 */
error_code_t display_line_draw(display_t *dsp, uint16_t x1, uint16_t y1,
							   uint16_t x2, uint16_t y2, uint8_t color)
{
	if (x1 > dsp->size.width || x2 > dsp->size.width || y1 > dsp->size.height || y2 > dsp->size.height)
		return OUT_OF_BOUNDS;

	if (x1 - x2 == 0)
	{ // staight line in y direction
		if (y1 > y2)
		{ // flip direction
			uint32_t b = y2;
			y2 = y1;
			y1 = b;
		}
		for (int y = y1; y <= y2; y++)
			display_pixel_draw(dsp, x1, y, color);
		return PM_OK;
	}
	if (y1 - y2 == 0)
	{ // staight line in x direction
		if (x1 > x2)
		{ // flip direction
			uint32_t b = x2;
			x2 = x1;
			x1 = b;
		}
		for (int x = x1; x <= x2; x++)
			display_pixel_draw(dsp, x, y1, color);
		return PM_OK;
	}

	// case for line going down
	if (abs(y2 - y1) < abs(x2 - x1))
	{
		if (x1 > x2)
			display_line_draw_low(dsp, x2, y2, x1, y1, color);
		else
			display_line_draw_low(dsp, x1, y1, x2, y2, color);
	}
	else
	{
		if (y1 > y2)
			display_line_draw_height(dsp, x2, y2, x1, y1, color);
		else
			display_line_draw_height(dsp, x1, y1, x2, y2, color);
	}

	// case for line going up

	return PM_OK;
}

error_code_t display_circle_fill(display_t *dsp, uint16_t x0, uint16_t y0,
								 uint16_t r, uint8_t color)
{
	display_pixel_draw(dsp, x0, y0, color);
	for (uint8_t r1 = 1; r1 <= r; r1++)
	{
		display_circle_draw(dsp, x0, y0, r1, color);
	}
	return PM_OK;
}

/**
 * @brief Draws a circle on the framebuffer
 *
 * Draws a circle on the framebuffer at x,y
 * with radius r and color.
 *
 * @return PM_OK if driver is unloaded
 * @return OUT_OF_BOUNDS if one or more pixels where out of bounds
 *
 */
error_code_t display_circle_draw(display_t *dsp, uint16_t x0, uint16_t y0,
								 uint16_t r, uint8_t color)
{
	return display_circle_draw_segment(dsp, x0, y0, r, color, 0xff);
}
error_code_t display_circle_draw_segment(display_t *dsp, uint16_t x0,
										 uint16_t y0, uint16_t r, uint8_t color, uint8_t segment)
{
	int32_t x = r - 1;
	int32_t y = 0;
	int32_t dX = 1;
	int32_t dY = 1;
	int32_t err = dX - (r << 1); // radius / 2

	while (x >= y)
	{
		if (segment & 1)
			display_pixel_draw(dsp, x0 + y, y0 - x, color); //12 Uhr - 1.5Uhr
		if (segment & 2)
			display_pixel_draw(dsp, x0 + x, y0 - y, color); // 1.5Uhr - 3 Uhr
		if (segment & 4)
			display_pixel_draw(dsp, x0 + x, y0 + y, color); // 3 - 4.5 Uhr
		if (segment & 8)
			display_pixel_draw(dsp, x0 + y, y0 + x, color); // 4.5 - 6 Uhr
		if (segment & 16)
			display_pixel_draw(dsp, x0 - y, y0 + x, color); // 6 - 7.5 Uhr
		if (segment & 32)
			display_pixel_draw(dsp, x0 - x, y0 + y, color); // 7,5 - 9 Uhr
		if (segment & 64)
			display_pixel_draw(dsp, x0 - x, y0 - y, color); // 9 - 10.5Uhr
		if (segment & 128)
			display_pixel_draw(dsp, x0 - y, y0 - x, color); //10.5 - 12 Uhr

		if (err <= 0)
		{
			y++;
			err += dY;
			dY += 2;
		}

		if (err > 0)
		{
			x--;
			dX += 2;
			err += dX - (r << 1); // radius /2
		}
	}

	return PM_OK;
}

/**
 * @brief Draws a filled rectangle on the framebuffer
 *
 * Draws a rectangle on the framebuffer filling it's area.
 *
 * @return PM_OK if driver is unloaded
 * @return OUT_OF_BOUNDS if one or more pixels where out of bounds
 *
 */
error_code_t display_rect_fill(display_t *dsp, uint16_t x0, uint16_t y0,
							   uint16_t width, uint16_t height, uint8_t color)
{
	for (uint16_t x = 0; x < width; x++)
		for (uint16_t y = 0; y < height; y++)
		{
			display_pixel_draw(dsp, x0 + x, y0 + y, color);
		}
	return PM_OK;
}

error_code_t display_draw_raw_rot(display_t *dsp, uint8_t *img,
								  uint16_t x0, uint16_t y0, uint16_t width, uint16_t height,
								  uint8_t color1, uint8_t color2, display_rotation_t rot)
{
	for (uint32_t p = 0; p < ((width * height) / 8); p++)
	{
		uint8_t field = img[p]; // offset for picture size bytes in front of image data
		for (uint8_t i = 0; i < 8; i++)
		{
			if (field & 1 << i)
			{
				if (rot == DISPLAY_ROTATE_90)
					display_pixel_draw(dsp, x0 + ((p / width) * 8 + i),
									   y0 + (p % width), color1);
				else
					display_pixel_draw(dsp, x0 + (p % width),
									   y0 + ((p / width) * 8 + i), color1);
			}
			else
			{
				if (rot == DISPLAY_ROTATE_90)
					display_pixel_draw(dsp, x0 + ((p / width) * 8 + i),
									   y0 + (p % width), color2);
				else
					display_pixel_draw(dsp, x0 + (p % width),
									   y0 + ((p / width) * 8 + i), color2);
			}
		}
	}
	return PM_OK;
}

inline error_code_t display_draw_raw(display_t *dsp, uint8_t *img,
									 uint16_t x0, uint16_t y0, uint16_t width, uint16_t height,
									 uint8_t color1, uint8_t color2)
{
	return display_draw_raw_rot(dsp, img, x0, y0, width, height, color1, color2,
								DISPLAY_ROTATE_0);
}

/**
 * @brief Renders text on display starting at x,y
 *
 * Text is a fixed size
 *
 * @return SUCCESS
 */
error_code_t display_text_draw(display_t *dsp, font_t *font, uint16_t x,
							   uint16_t y, const char *text, uint8_t color)
{
	uint32_t i = 0;
	uint32_t pos = 0;
	uint8_t *c;
	while (text[i])
	{ // for every character in char array until \0 is encountered
		pos = (text[i] - font->asciiOffset) * font->height * font->width / 8;
		c = (uint8_t *)&font->data[pos];
		display_draw_raw_rot(dsp, c, x + (i * 8), y, font->width, font->height,
							 color, TRANSPARENT, font->rotation);
		i++; // next char
	}

	return PM_OK;
}

error_code_t display_draw_image(display_t *dsp, uint8_t *data, uint16_t x,
								uint16_t y, uint16_t w, uint16_t h)
{
	if (x * y > dsp->size.width * dsp->size.height)
		return OUT_OF_BOUNDS;

	rect_t box = {x, y, w, h};

	for (uint16_t y0 = 0; y0 < h; y0++)
		for (uint16_t x0 = 0; x0 < w; x0++)
		{
			display_pixel_draw(dsp, x0 + x, y0 + y,
							   dsp->decompress(&box, x0, y0, data));
		}

	return PM_OK;
}
