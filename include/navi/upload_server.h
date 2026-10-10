/*
 * HTTP server for the upload of maps and tracks
 *
 * Copyright (c) 2021, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_UPLOAD_SERVER_H_
#define NAVI_UPLOAD_SERVER_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UPLOAD_IDLE,    /// no transfer announced
    UPLOAD_ACTIVE,  /// app announced a transfer, files are coming in
    UPLOAD_DONE,    /// all announced files are stored
    UPLOAD_ABORTED, /// no upload for too long
} upload_state_t;

typedef struct {
    upload_state_t state;
    uint32_t files_total;
    uint32_t files_done;
    uint64_t bytes_total;
    uint64_t bytes_done;
    bool result_shown; /// "done"/"aborted" was on the display for one refresh
} upload_progress_t;

void upload_server_start(void);
void upload_server_stop(void);
upload_progress_t upload_get_progress(void);
void upload_progress_result_shown(upload_state_t shown);

#endif /* NAVI_UPLOAD_SERVER_H_ */
