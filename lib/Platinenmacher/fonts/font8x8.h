/**
* 8x8 monochrome bitmap fonts for rendering
* Author: Daniel Hepper <daniel@hepper.net>
*
* License: Public Domain
*
* Based on:
* // Summary: font8x8.h
* // 8x8 monochrome bitmap fonts for rendering
* //
* // Author:
* // Marcel Sondaar
* // International Business Machines (public domain VGA fonts)
* //
* // License:
* // Public Domain
*
* Fetched from: http://dimensionalrift.homelinux.net/combuster/mos3/?p=viewsource&file=/modules/gfx/font8_8.asm
**/

#ifndef DISPLAY_FONT_8x8_H
#define DISPLAY_FONT_8x8_H

#include <stdint.h>

extern const uint8_t font8x8_offset;
extern const char *font8x8_name;
extern const uint8_t font8x8[];

#endif // DISPLAY_FONT_8x8_H
