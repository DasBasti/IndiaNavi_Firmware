/*
 * Display settings the app sets
 *
 * Copyright (c) 2024, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_DISPLAY_SETTINGS_H_
#define NAVI_DISPLAY_SETTINGS_H_

#include <stdbool.h>
#include <stdint.h>

#include "Platinenmacher.h"
#include "ble_protocol.h"

#ifdef LINUX
static inline bool display_settings_show_track(void) { return true; }
static inline bool display_settings_show_height_graph(void) { return true; }
static inline color_t display_settings_track_color(void) { return (color_t)BLEP_TRACK_COLOR_DEFAULT; }
static inline uint16_t display_settings_update_interval(void) { return BLEP_UPDATE_INTERVAL_DEFAULT_S; }
#else
void display_settings_init(void);
bool display_settings_show_track(void);
bool display_settings_show_height_graph(void);
color_t display_settings_track_color(void);
uint16_t display_settings_update_interval(void);
blep_settings_t display_settings_get(void);
bool display_settings_set(const blep_settings_t* settings);
#endif

#endif /* NAVI_DISPLAY_SETTINGS_H_ */
