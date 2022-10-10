/*
 * GPX parser
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PLATINENMACHER_PARSER_GPX_H
#define PLATINENMACHER_PARSER_GPX_H

#include "../gui/waypoint.h"

waypoint_t *gpx_parser(const char* gpx_file_data, uint32_t (*add_waypoint_cb)(waypoint_t* wp));

#endif //PLATINENMACHER_PARSER_GPX_H