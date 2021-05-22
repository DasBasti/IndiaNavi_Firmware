/*
 * STM32 implementation for handling SPI
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#include "hw/spi.h"
#include "memory.h"

static void onFinish(struct spi_transfer* transfer){}
static void onError(struct spi_transfer* transfer){}

spi_t* spi_init(void *hw){
	spi_t *spi = RTOS_Malloc(sizeof(spi_t));

	return spi;
}
error_code_t spi_transmit(spi_t *spi, spi_transfer* transmission);
spi_transfer* spi_create_transfer(uint16_t length);
