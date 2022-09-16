/*
 * spi.h
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_HW_SPI_H_
#define PLATINENMACHER_HW_SPI_H_

#include <stdint.h>
#include "error.h"

typedef struct spi_transfer {
	uint16_t length;
	uint8_t *tx_buffer;
	uint8_t *rx_buffer;
	void (*onFinish)(struct spi_transfer* transfer);
	void (*onError)(struct spi_transfer* transfer);
} spi_transfer_t;

typedef struct {
	void *hw; // Hardware handler
	uint8_t read_dummy;
} spi_t;

spi_t* spi_init(void *hw);
error_code_t spi_transmit(spi_t *spi, spi_transfer_t* transmission);
spi_transfer_t* spi_create_transfer(uint16_t length);


#endif /* PLATINENMACHER_HW_SPI_H_ */
