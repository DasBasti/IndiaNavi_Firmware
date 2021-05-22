/*
 * ACEP_5IN65.h
 *
 *  Created on: Jan 2, 2021
 *      Author: bastian
 */
#ifndef __EPD_5IN65F_H__
#define __EPD_5IN65F_H__

#include "display/display.h"
#include "error.h"

// TODO: SPI transaktion kapseln!
#include <driver/spi_master.h>

#define ACEP_5IN65_WIDTH 600
#define ACEP_5IN65_HEIGHT 448

#define EINK_DC 25
#define EINK_CS 5
#define EINK_BUSY 39

#define EINK_SPI_HOST HSPI_HOST

display_t *ACEP_5IN65_Init(display_rotation_t rotation);
error_code_t ACEP_5IN65_Write(display_t *dsp, uint16_t x, uint16_t y, uint8_t color);
uint8_t ACEP_5IN65_Decompress_Pixel(rect_t *size, uint16_t x, uint16_t y, uint8_t *data);

#endif
