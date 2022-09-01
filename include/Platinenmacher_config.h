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
#include "hw/regulator_gpio.h"
#include "hw/gpio.h"

/* WaveShare ACep 7 color ePaper display */
#include "display.h"
#include "display/eink/acep_5in65_7c.h"

/* GUI drawing */
#include "font.h"
#include "fonts/font8x8.h"
#include "fonts/font8x16.h"
font_t f8x8, f8x16;
#include "gui/image.h"
#include "gui/label.h"
#include "gui/waypoint.h"
/* SD Card */
//#include "hw/gpio.h"
//#include "storage/sdcard/sd_spi.h"

/* Command processor */
#include "parser/command.h"

#endif /* __PLATINENMACHER_CONFIG_H__ */
