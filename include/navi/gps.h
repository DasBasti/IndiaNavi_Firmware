/*
 * GPS module and track log
 *
 * Copyright (c) 2021, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_GPS_H_
#define NAVI_GPS_H_

#include <stdbool.h>
#include <stdint.h>

#include "Platinenmacher.h"
#include "ble_protocol.h"

/* map_position_t.fix of a position that comes from the phone, not from the GPS module */
#define GPS_FIX_PHONE BLEP_FIX_PHONE

/* position of the GPS module or the phone, NULL before the GPS task started */
const map_position_t* gps_get_position(void);

/* starts the GPS task if it is not running */
void gps_start_task(void);
/* ask the GPS task to switch the module off and delete itself */
void gps_request_stop(void);

void gps_screen_element(const display_t* dsp);
bool gps_is_position_known();
bool gps_has_satellite_fix(void);
bool gps_set_time_from_phone(int64_t epoch);
bool gps_set_position_from_phone(const blep_position_in_t* position);
void gps_stop_parser();
void gps_enter_standby();

#endif /* NAVI_GPS_H_ */
