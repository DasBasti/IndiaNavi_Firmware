/*
 * ESP32 implementation for handling GPIO
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#include "hw/gpio.h"
#include "error.h"
#include "memory.h"

#include <driver/gpio.h>

gpio_t *gpio_create(gpio_direction_t direction, void *port, uint32_t pin)
{
	gpio_t *gpio = RTOS_Malloc(sizeof(gpio_t));
	gpio->direction = direction;
	gpio->port = port;
	gpio->pin = pin;

	gpio_config_t esp_pin = {};
	if (gpio->direction == INPUT)
		esp_pin.mode = GPIO_MODE_INPUT;
	else if (gpio->direction == OUTPUT)
		esp_pin.mode = GPIO_MODE_OUTPUT;
	else if (gpio->direction == ANALOG_IN)
		return gpio;
	else if (gpio->direction == ANALOG_OUT)
		return gpio;
	else if (gpio->direction == DISABLED)
		esp_pin.mode = GPIO_MODE_INPUT;

	esp_pin.pin_bit_mask = BIT64(pin);
	gpio_config(&esp_pin);

	return gpio;
}
void gpio_deinit(gpio_t *gpio);
void gpio_write(gpio_t *gpio, gpio_value_t value)
{
	if (gpio->direction == OUTPUT)
	{
		gpio_set_level(gpio->pin, value);
	}
}
gpio_value_t gpio_read(gpio_t *gpio)
{
	return gpio_get_level(gpio->pin);
}
