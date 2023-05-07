/*
 * Parser for Quectell GNSS SDK command responses
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nmea_parser.h"
#include "esp_log.h"

esp_err_t pq_detect(esp_gps_t* esp_gps);
esp_err_t pq_parse(esp_gps_t* esp_gps);
