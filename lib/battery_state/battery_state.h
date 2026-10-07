/*
 * When the battery counts as empty and when a charger is connected
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

/* the charger input (ADC reading) has to be this much above the battery for a connected charger */
#define CHARGER_MARGIN 5
/* readings in a row that have to agree before a charger counts as connected or removed, a single noisy
 * reading near the margin must not switch WiFi on and off */
#define CHARGER_CONFIRM_READINGS 3

typedef struct {
    bool charging;            /// confirmed state
    uint8_t changed_readings; /// readings in a row that disagree with charging
} charger_state_t;

typedef enum {
    CHARGER_NO_CHANGE,
    CHARGER_CONNECTED,
    CHARGER_REMOVED,
} charger_change_t;

/**
 * Take a new reading of the charger and battery input (raw ADC values).
 *
 * @return the change once CHARGER_CONFIRM_READINGS readings in a row showed it
 */
charger_change_t charger_state_update(charger_state_t* state, int charger, int battery);

/** true while readings disagree with the confirmed state, the next reading should come soon */
bool charger_state_is_pending(const charger_state_t* state);

#endif /* BATTERY_STATE_H */
