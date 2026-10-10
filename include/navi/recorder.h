/*
 * Track recordings
 *
 * Copyright (c) 2024, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_RECORDER_H_
#define NAVI_RECORDER_H_

#include <stddef.h>
#include <stdint.h>

#include "ble_protocol.h"

typedef enum {
    RECORDER_OK = 0,
    RECORDER_ERR_STATE,     /// already recording, not recording, or the recording is running
    RECORDER_ERR_TIME,      /// the clock is not set
    RECORDER_ERR_NOT_FOUND, /// no such recording
    RECORDER_ERR_SD,        /// the SD card can not be used
} recorder_result_t;

#define RECORDER_PATH_LEN 32 /// "//TRACKS/XXXXXXXX.GPX" + '\0'

void recorder_init(void);
void recorder_path(char* out, size_t size, uint32_t id);
recorder_result_t recorder_start(void);
recorder_result_t recorder_stop(void);
recorder_result_t recorder_delete(uint32_t id);
blep_recording_status_t recorder_status(void);
uint32_t recorder_active(uint32_t* current_generation);
void recorder_point_written(uint32_t id, uint32_t size, uint32_t time);
int recorder_list(uint16_t first, blep_recording_entry_t* entries, size_t max, uint16_t* total);

#endif /* NAVI_RECORDER_H_ */
