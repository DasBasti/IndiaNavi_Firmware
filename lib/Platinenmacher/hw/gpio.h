/*
 * gpio.h
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_HW_GPIO_H_
#define PLATINENMACHER_HW_GPIO_H_

#include <stdint.h>

typedef enum
{
	GPIO_RESET,
	GPIO_SET
} gpio_value_t;

typedef enum
{
	PUSHPULL,
	OPENDRAIN
} gpio_pp_mode_t;

/**
 * GPIO directions
 * INPUT      - Read binary value RESET/SET
 * OUTPUT     - RESET/SET binary value
 * ANALOG_IN  - connect ADC read function
 * ANALOG_OUT - connect PWM set function
 * DISABLED   - reinitialize GPIO as DISABLED
 */
typedef enum
{
	INPUT,
	OUTPUT,
	ANALOG_IN,
	ANALOG_OUT,
	DISABLED
} gpio_direction_t;

typedef struct
{
	gpio_direction_t direction;
	void *port;
	uint32_t pin;
	gpio_value_t onValue;
	gpio_pp_mode_t pp;
} gpio_t;

gpio_t *gpio_create(gpio_direction_t direction, void *port, uint32_t pin);
void gpio_deinit(gpio_t *gpio);
void gpio_write(gpio_t *gpio, gpio_value_t value);
gpio_value_t gpio_read(gpio_t *gpio);

#endif /* PLATINENMACHER_HW_GPIO_H_ */
