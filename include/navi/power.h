/*
 * Battery, charger and deep sleep
 *
 * The power task reads the battery and the charger. Other modules only read
 * the values it found.
 *
 * Copyright (c) 2021, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_POWER_H_
#define NAVI_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#include "Platinenmacher.h"

void power_start_task(void);

/* battery level in percent */
int32_t power_battery_level(void);
bool power_is_charging(void);
/* the power task read the battery level and knows if a charger is connected */
bool power_state_known(void);

/**
 * The device woke up from the timer of an empty battery: look for a charger
 * before anything is started. Returns true if one is connected.
 */
bool power_charger_connected_at_wakeup(void);

/* while set, deep sleep wakes up regularly to look for a charger */
void power_set_wake_up_for_charger(bool wake_up);

/* returns DEFERRED if a charger is connected, otherwise it does not return */
error_code_t enter_deep_sleep_if_not_charging();

#endif /* NAVI_POWER_H_ */
