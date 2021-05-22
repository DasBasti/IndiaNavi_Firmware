/*
 * geometric.h
 *
 *  Created on: 04.01.2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_DISPLAY_GUI_GEOMETRIC_H_
#define PLATINENMACHER_DISPLAY_GUI_GEOMETRIC_H_

#include <stdint.h>
typedef struct {
	uint16_t left;
	uint16_t top;
} point_t;

typedef struct {
	uint16_t left;
	uint16_t top;
	uint16_t width;
	uint16_t height;
} rect_t;

typedef enum {
	LEFT, CENTER, RIGHT, TOP, MIDDLE, BOTTOM
} alignment_t;

typedef enum {
	TOP_LEFT = 0x1, TOP_RIGHT = 0x2, BOTTOM_LEFT = 0x4, BOTTOM_RIGHT = 0x8
} corner_t;

typedef enum {
	NO_BORDER = 0,
	TOP_SOLID = 0x1,
	TOP_DOTTED = 0x2,
	BOTTOM_SOLID = 0x4,
	BOTTOM_DOTTET = 0x8,
	LEFT_SOLID = 0x10,
	LEFT_DOTTED = 0x20,
	RIGHT_SOLID = 0x40,
	RIGHT_DOTTET = 0x80,
	ALL_SOLID = 0x55,
	ALL_DOTTET = 0xaa,
} border_line_t;

#endif /* PLATINENMACHER_DISPLAY_GUI_GEOMETRIC_H_ */
