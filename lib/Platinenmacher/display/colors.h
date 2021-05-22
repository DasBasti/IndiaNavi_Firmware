/*
 * colors.h
 *
 *  Created on: Jan 2, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_DISPLAY_COLORS_H_
#define PLATINENMACHER_DISPLAY_COLORS_H_

#ifdef EINK_7COLOR
/**********************************
 * Available Colors on ACeP 5.65"
 *********************************/
typedef enum {
	BLACK, 	/// 000
	WHITE,	///	010
	GREEN,	///	011
	BLUE,	///	001
	RED,	///	100
	YELLOW,	///	101
	ORANGE,	///	110
	TRANSPARENT,	///	111   unavailable  Afterimage
} color_t;
#else
typedef enum {
	BLACK,
	WHITE,
	TRANSPARENT,
} color_t;
#endif

#endif /* PLATINENMACHER_DISPLAY_COLORS_H_ */
