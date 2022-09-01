/*
 * font.c
 *
 *  Created on: Jan 3, 2021
 *      Author: bastian
 */

#include "font.h"
/**
 * @brief Loads values form array to font struct
 *
 * Loads width, height and start pointer from array
 * populates name pointer
 *
 * @return SUCCESS if data is consistent
 * @return OUT_OF_BOUNDS if sum of data does not match given values
 */
error_code_t font_load_from_array(font_t *font, const uint8_t *data,
		const char *name) {

	font->data = data + 4 * sizeof(uint8_t);
	font->width = data[0];
	font->height = data[1];
	//uint32_t size = data[0] * data[1];
	font->asciiOffset = data[2];
	font->name = name;
	font->rotation = data[3];

	return PM_OK;
}

/**
 * @brief Length of \0 terminated string
 *
 * @return number of characters until \0 is discovered
 */
uint32_t font_strlen(const char *c) {
	uint32_t l = 0;
	while (*c++ != '\0')
		l++;
	return l;
}

/**
 * @brief Width of text element
 *
 * Calculates pixels of width of rendered text with given font.
 *
 * @return number of pixels
 */
uint32_t font_text_pixel_width(font_t *font, const char *text) {
	return font->width * font_strlen(text);
}

/**
 * @brief Height of text element
 *
 * Calculates pixels of hight of rendered text with given font.
 *
 * @return number of pixels
 */
uint32_t font_text_pixel_height(font_t *font, const char *text) {
	return font->height;
}
