/*
 * The button
 *
 * The interrupt sends TASK_EVENT_BUTTON to the main task, which calls
 * button_handle_event(). The callbacks run in the main task, the long
 * press callback in the esp_timer task.
 *
 * Copyright (c) 2021, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_BUTTON_H_
#define NAVI_BUTTON_H_

/* the GPIO ISR service must be installed */
void button_init(void);
/* TASK_EVENT_BUTTON arrived: wait until the contacts settled and act */
void button_handle_event(void);

void set_short_press_event(void (*event)(void));
void set_long_press_event(void (*event)(void));

#endif /* NAVI_BUTTON_H_ */
