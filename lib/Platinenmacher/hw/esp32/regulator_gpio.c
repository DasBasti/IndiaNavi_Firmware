/*
 * regulator_gpio.c
 *
 *  Created on: Jan 8, 2021
 *      Author: bastian
 */

#include "hw/regulator_gpio.h"
#include "hw/gpio.h"
#include "memory.h"

void regulator_gpio_enable(regulator_t *reg)
{
	gpio_t *gpio = (gpio_t *)reg->driver;
	if (reg->driver)
	{
		reg->usage++;
		if (reg->status != ON)
		{
			gpio_t *gpio = (gpio_t *)reg->driver;
			gpio_write(gpio, gpio->onValue);
			reg->status = ON;
		}
	}
}

void regulator_gpio_disable(regulator_t *reg)
{
	if (reg->driver)
	{
		gpio_t *gpio = (gpio_t *)reg->driver;
		if (reg->status != OFF)
		{
			reg->usage--;
		}
		if (reg->usage == 0)
		{
			gpio_write(gpio, !gpio->onValue);
			reg->status = OFF;
		}
	}
}

regulator_t *regulator_gpio_create(gpio_t *gpio)
{
	regulator_t *reg = RTOS_Malloc(sizeof(regulator_t));
	reg->driver = gpio;
	reg->enable = regulator_gpio_enable;
	reg->disable = regulator_gpio_disable;
	reg->disable(reg);
	return reg;
}
