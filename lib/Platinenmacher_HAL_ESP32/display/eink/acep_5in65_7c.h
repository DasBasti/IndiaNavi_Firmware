/*
 * ACEP_5IN65.h
 *
 *  Created on: Jan 2, 2021
 *      Author: bastian
 */
#ifndef __EPD_5IN65F_H__
#define __EPD_5IN65F_H__

#include "display.h"
#include "error.h"

// TODO: SPI transaktion kapseln!
#include <driver/spi_master.h>

#define ACEP_5IN65_WIDTH 600
#define ACEP_5IN65_HEIGHT 448

typedef struct {
    uint8_t dc;
    uint8_t select;
    uint8_t clk;
    uint8_t mosi;
    uint8_t busy;
    spi_host_device_t host;
} acep_5in65_dev_t;

display_t *ACEP_5IN65_Init(acep_5in65_dev_t* dev, display_rotation_t rotation);
error_code_t ACEP_5IN65_Write(const display_t *dsp, int16_t x, int16_t y, uint8_t color);
uint8_t ACEP_5IN65_Decompress_Pixel(rect_t *size, int16_t x, int16_t y, const uint8_t *data);

#endif
