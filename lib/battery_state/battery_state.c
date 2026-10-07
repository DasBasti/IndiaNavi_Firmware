/*
 * When the battery counts as empty and when a charger is connected
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

bool battery_state_recovered(int32_t level_percent, bool charging)
{
    return charging && level_percent >= BATTERY_RECOVERED_LEVEL;
}

charger_change_t charger_state_update(charger_state_t* state, int charger, int battery)
{
    bool connected;
    if (charger - CHARGER_MARGIN > battery)
        connected = true;
    else if (charger - CHARGER_MARGIN < battery)
        connected = false;
    else
        connected = state->charging; // exactly at the margin, no change

    if (connected == state->charging) {
        state->changed_readings = 0;
        return CHARGER_NO_CHANGE;
    }
    if (++state->changed_readings < CHARGER_CONFIRM_READINGS)
        return CHARGER_NO_CHANGE;

    state->changed_readings = 0;
    state->charging = connected;
    return connected ? CHARGER_CONNECTED : CHARGER_REMOVED;
}

bool charger_state_is_pending(const charger_state_t* state)
{
    return state->changed_readings > 0;
}
