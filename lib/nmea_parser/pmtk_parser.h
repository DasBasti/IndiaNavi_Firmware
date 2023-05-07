/*
 * Parser for PMTK responses
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nmea_parser.h"
#include "esp_log.h"

esp_err_t pmtk_detect(esp_gps_t* esp_gps);
esp_err_t pmtk_parse(esp_gps_t* esp_gps);
