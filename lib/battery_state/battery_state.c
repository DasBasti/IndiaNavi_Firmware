/*
 * When the battery counts as empty
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "battery_state.h"

bool battery_state_update(battery_state_t* state, int32_t level_percent, bool charging)
{
    if (charging || level_percent > BATTERY_EMPTY_LEVEL) {
        state->low_readings = 0;
        return false;
    }
    if (state->low_readings < BATTERY_EMPTY_READINGS)
        state->low_readings++;
    return state->low_readings >= BATTERY_EMPTY_READINGS;
}

bool battery_state_is_low(const battery_state_t* state)
{
    return state->low_readings > 0;
}
