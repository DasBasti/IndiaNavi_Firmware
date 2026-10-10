/*
 * WiFi access point for the upload of maps
 *
 * Copyright (c) 2021, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_WIFI_H_
#define NAVI_WIFI_H_

#include <stdbool.h>
#include <stdint.h>

bool isConnected();
void wifi_ap_credentials_init(void);
const char* wifi_ap_ssid(void);
const char* wifi_ap_password(void);
bool wifi_ap_running(void);
uint8_t wifi_ap_station_count(void);
const uint8_t* wifi_ap_qrcode(void);
void wifi_notify_activity(void);
/* starts the WiFi task if it is not running */
void wifi_start_task(void);
/* ask the WiFi task to switch WiFi off and delete itself */
void wifi_request_stop(void);

#endif /* NAVI_WIFI_H_ */
