/*
 * Events for the main task
 *
 * The main task starts and stops the other tasks. Other tasks and interrupts
 * ask for it with an event, they never create or delete a task themselves.
 *
 * Copyright (c) 2021, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_SYSTEM_EVENTS_H_
#define NAVI_SYSTEM_EVENTS_H_

#include <stdbool.h>

#include <freertos/FreeRTOS.h>

typedef enum {
    TASK_EVENT_NO_EVENT = 0,
    TASK_EVENT_ENTER_LOW_POWER = 50,
    TASK_EVENT_ENABLE_GPS,
    TASK_EVENT_DISABLE_GPS,
    TASK_EVENT_ENABLE_DISPLAY,
    TASK_EVENT_DISABLE_DISPLAY,
    TASK_EVENT_ENABLE_WIFI,
    TASK_EVENT_DISABLE_WIFI,
    TASK_EVENT_BUTTON, /// the button changed, the main task reads it when the contacts settled
    TASK_EVENT_START_CHARGING,
    TASK_EVENT_STOP_CHARGING,
    TASK_EVENT_ENABLE_BLE,
    TASK_EVENT_DISABLE_BLE,
    TASK_EVENT_ACCELEROMETER, /// interrupt of the accelerometer
} task_events_e;

/**
 * Send an event to the main task
 *
 * wait: ticks to wait for space in the queue
 * returns false if the queue stayed full
 */
bool system_post_event(task_events_e event, TickType_t wait);

/** Same from an interrupt */
bool system_post_event_from_isr(task_events_e event);

#endif /* NAVI_SYSTEM_EVENTS_H_ */
