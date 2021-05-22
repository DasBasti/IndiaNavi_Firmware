/*
 * regulator_gpio.h
 *
 *  Created on: Jan 8, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_HW_REGULATOR_GPIO_H_
#define PLATINENMACHER_HW_REGULATOR_GPIO_H_

#include "hw/regulator.h"
#include "hw/gpio.h"

regulator_t *regulator_gpio_create(gpio_t *gpio);

#endif /* PLATINENMACHER_HW_REGULATOR_GPIO_H_ */
