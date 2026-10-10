/*
 * The button
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "gui.h"
#include "navi/button.h"
#include "navi/system_events.h"
#include "pins.h"

static const char* TAG = "BUTTON";

/* the contacts bounce, the button is read when they settled */
#define BUTTON_DEBOUNCE_MS 30
#define LONG_PRESS_US (3 * 1000 * 1000)

static esp_timer_handle_t button_timer;
// one button event in the queue at a time, a bouncing button would fill it and push out other events
static volatile bool button_event_pending;
static bool button_pressed; // state after debouncing
// set by the long press timer, the following button up is not a short press
static volatile bool long_press_handled;
static void (*_short_press)(void);
static void (*_long_press)(void);

static void IRAM_ATTR handleButtonPress(void* arg)
{
#ifdef WITH_ACC
    if (gpio_get_level(I2C_INT) == I2C_INT_LEVEL) {
        system_post_event_from_isr(TASK_EVENT_ACCELEROMETER);
        return;
    }
#endif
    if (button_event_pending)
        return;
    // the level is read by the main task when the contacts settled
    button_event_pending = true;
    if (!system_post_event_from_isr(TASK_EVENT_BUTTON))
        button_event_pending = false; // the next edge tries again
}

void button_handle_event(void)
{
    vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
    // an edge after this reads the button again
    button_event_pending = false;
    bool pressed = gpio_get_level(BTN) == BTN_LEVEL;
    if (pressed == button_pressed)
        return; // bounces that ended in the same state
    button_pressed = pressed;

    if (pressed) {
        ESP_LOGI(TAG, "Button down");
        long_press_handled = false;
        esp_timer_start_once(button_timer, LONG_PRESS_US);
    } else {
        ESP_LOGI(TAG, "Button up");
        esp_timer_stop(button_timer); // stop long press timer
        if (long_press_handled)
            long_press_handled = false; // releasing the long press
        else if (_short_press)
            _short_press();
    }
}

/**
 * Long press: change application to shut down
 *
 * The button keeps its interrupt. While charging the device stays on the off
 * screen and a short press has to turn it on again.
 */
static void button_timer_trigger(void* arg)
{
    // a release that got lost must not switch the device off
    if (gpio_get_level(BTN) != BTN_LEVEL) {
        ESP_LOGW(TAG, "Long press timer ran out, but the button is not pressed");
        return;
    }
    long_press_handled = true;
    if (_long_press)
        _long_press();
    else
        gui_set_app_mode(APP_MODE_TURN_OFF);
}

void button_init(void)
{
    // the timer exists before the first press
    esp_timer_create_args_t button_timer_args = {
        .callback = button_timer_trigger,
    };
    esp_timer_create(&button_timer_args, &button_timer);

    gpio_set_intr_type(BTN, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(BTN, handleButtonPress, NULL);

#ifdef WITH_ACC
    /* Set Accelerator IO to input, with PUI and falling edge IRQ */
    gpio_config_t esp_acc = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pin_bit_mask = BIT64(I2C_INT),
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&esp_acc);
    gpio_set_intr_type(I2C_INT, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(I2C_INT, handleButtonPress, NULL);
#endif
}

void set_short_press_event(void (*event)(void))
{
    _short_press = event;
}

void set_long_press_event(void (*event)(void))
{
    _long_press = event;
}
