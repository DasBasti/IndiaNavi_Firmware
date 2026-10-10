/*
 * Bluetooth interface to the app
 *
 * Copyright (c) 2024, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_BLE_H_
#define NAVI_BLE_H_

#include <stdbool.h>
#include <stdint.h>

void ble_if_recording_changed(void);
void ble_if_start(void);
void ble_if_stop(void);
bool ble_if_is_running(void);
bool ble_if_is_connected(void);
int32_t ble_if_passkey(void);
void ble_if_wifi_status_changed(void);

#endif /* NAVI_BLE_H_ */
