/*
 * font.h
 *
 *  Created on: 03.01.2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_FONT_FONT_FONT_H_
#define PLATINENMACHER_FONT_FONT_FONT_H_

#include <stdint.h>
#include "error.h"
typedef enum
{
    FONT_ROTATE_0 = 0, /**< Rotate 0 degrees, clockwise. */
    FONT_ROTATE_90,    /**< Rotate 90 degrees, clockwise. */
    FONT_ROTATE_180,   /**< Rotate 180 degrees, clockwise. */
    FONT_ROTATE_270    /**< Rotate 270 degrees, clockwise. */
} font_rotation_t;

/**
 * @brief Font in c header format
 */
typedef struct
{
    const uint8_t *data;
    uint8_t width;
    uint8_t height;
    uint8_t asciiOffset;
    const char *name;
    font_rotation_t rotation;
} font_t;


error_code_t font_load_from_array(font_t *font, const uint8_t *data, const char *name);
uint32_t font_text_pixel_width(font_t *font, const char *text);
uint32_t font_text_pixel_height(font_t *font, const char *text);
uint32_t font_strlen(const char *c);



#endif /* PLATINENMACHER_FONT_FONT_FONT_H_ */
