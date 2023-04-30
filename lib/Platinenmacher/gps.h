/*
 * GPS functions
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PLATINENMACHER_GPS_H
#define PLATINENMACHER_GPS_H

typedef enum {
    GPS_FIX_INVALID, /*!< Not fixed */
    GPS_FIX_GPS,     /*!< GPS */
    GPS_FIX_DGPS,    /*!< Differential GPS */
    GPS_FIX_DR = 6,  /*!< Dead Reckoning, valid fix */
} gps_fix_t;

#endif