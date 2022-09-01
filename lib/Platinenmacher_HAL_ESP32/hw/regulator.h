/*
 * regulator.h
 *
 *  Created on: Jan 8, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_REGULATOR_H_
#define PLATINENMACHER_REGULATOR_H_

#include <stdint.h>

typedef enum {
	OFF, ON, FAULT
} regulator_status_t;

typedef struct regulator {
	void (*enable)(struct regulator *reg);
	void (*disable)(struct regulator *reg);
	void *driver;
	regulator_status_t status;
	uint8_t usage;

} regulator_t;

regulator_t* regulator_create();

#endif /* PLATINENMACHER_REGULATOR_H_ */
