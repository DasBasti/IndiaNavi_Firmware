/*
 * Parts of the Bluetooth interface that ble.c and ble_ota.c share
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BLE_INTERNAL_H
#define BLE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ble_protocol.h"

/* from ble.c */
void ble_if_notify_ota_status(const uint8_t status[BLEP_OTA_STATUS_SIZE]);
uint16_t ble_if_mtu(void);
void ble_if_request_fast_link(bool fast);

/* from ble_ota.c. The write functions return 0 or an ATT error code. */
int ble_ota_control_write(const uint8_t* data, size_t len);
int ble_ota_data_write(const uint8_t* data, size_t len);
void ble_ota_status_read(uint8_t status[BLEP_OTA_STATUS_SIZE]);
void ble_ota_disconnected(void);
void ble_ota_stop(void);

#endif /* BLE_INTERNAL_H */
