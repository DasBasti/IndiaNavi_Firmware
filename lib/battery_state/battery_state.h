/*
 * When the battery counts as empty
 *
 * The device switches off while some charge is left, so the GPS module can
 * keep its clock running in standby and finds the satellites faster after
 * charging. No hardware dependency, so it can be tested on the host.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BATTERY_STATE_H
#define BATTERY_STATE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Battery level in percent (of the ADC range of readBatteryPercent()) at and
 * below which the battery is empty. To be checked with the voltage at which
 * the battery protection switches off, see issue #42.
 */
#define BATTERY_EMPTY_LEVEL 5

/* readings in a row that have to be empty, a refresh of the e-ink display pulls the voltage down for a moment */
#define BATTERY_EMPTY_READINGS 3

/* time between two readings while the battery is low, so it is decided fast */
#define BATTERY_EMPTY_CHECK_INTERVAL_MS 5000

/*
 * Level in percent a charging device has to reach on the battery empty screen
 * to turn on again. Above BATTERY_EMPTY_LEVEL, so it does not switch back and
 * forth when the voltage drops for a moment.
 */
#define BATTERY_RECOVERED_LEVEL 10

/* a device that sleeps with an empty battery looks for a charger this often */
#define BATTERY_EMPTY_WAKEUP_INTERVAL_S 60

typedef struct {
    uint8_t low_readings; /// empty readings in a row
} battery_state_t;

/**
 * Take a new reading of the battery level.
 *
 * @return true if the battery is empty: BATTERY_EMPTY_READINGS readings in a
 *         row at or below BATTERY_EMPTY_LEVEL without a charger
 */
bool battery_state_update(battery_state_t* state, int32_t level_percent, bool charging);

/** true while the battery is low, the next reading should come after BATTERY_EMPTY_CHECK_INTERVAL_MS */
bool battery_state_is_low(const battery_state_t* state);

/** true if a charger is connected and the battery has enough charge to leave the battery empty screen */
bool battery_state_recovered(int32_t level_percent, bool charging);

#endif /* BATTERY_STATE_H */
