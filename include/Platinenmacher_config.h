/*
 * Platinenmacher_config.h
 *
 *  Created on: 03.01.2021
 *      Author: bastian
 */

#ifndef __PLATINENMACHER_CONFIG_H__
#define __PLATINENMACHER_CONFIG_H__

#include <stdio.h>
#define __weak __attribute__((weak))

/* Power management */
#ifdef LINUX
#else
#include "hw/gpio.h"
#include "hw/regulator_gpio.h"
#endif

/* WaveShare ACep 7 color ePaper display */
#include "display.h"
#ifdef LINUX
#else
#include "display/eink/acep_5in65_7c.h"
#endif

/* GUI drawing */
#include "font.h"
#include "fonts/font8x16.h"
#include "fonts/font8x8.h"
extern font_t f8x8, f8x16;
#include "gui/image.h"
#include "gui/label.h"
#include "gui/map.h"
#include "gui/waypoint.h"

#define BATLEVEL_IMAGES_DEFAULT
#include "gui/battery_indicator.h"
/* SD Card */
//#include "hw/gpio.h"
//#include "storage/sdcard/sd_spi.h"

/* Command processor */
#include "parser/command.h"
#include "parser/config.h"

#endif /* __PLATINENMACHER_CONFIG_H__ */
