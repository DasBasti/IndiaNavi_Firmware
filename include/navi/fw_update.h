/*
 * Firmware update
 *
 * Copyright (c) 2024, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_FW_UPDATE_H_
#define NAVI_FW_UPDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FW_IDLE,
    FW_ACTIVE,
    FW_ERROR, /// failed, the error is shown once on the display
} fw_state_t;

typedef enum {
    FW_OK = 0,
    FW_ERR_BUSY,
    FW_ERR_SIZE,
    FW_ERR_FLASH,
    FW_ERR_INVALID,
} fw_result_t;

typedef struct {
    fw_state_t state;
    uint32_t bytes_total;
    uint32_t bytes_done;
    bool visible;      /// the progress is drawn on the display
    bool result_shown; /// the error was on the display for one refresh
} fw_progress_t;

fw_result_t fw_update_begin(uint32_t size, bool show_progress);
fw_result_t fw_update_write(const void* data, size_t len);
fw_result_t fw_update_finish(void);
void fw_update_abort(bool failed);
bool fw_update_active(void);
fw_progress_t fw_update_get_progress(void);
void fw_update_result_shown(void);

#endif /* NAVI_FW_UPDATE_H_ */
