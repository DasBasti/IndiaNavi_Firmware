/*
 * Content-Range of an upload that continues a file: "bytes {start}-{end}/{total}"
 *
 * The app sends a vector map of hundreds of MB in one request. If the WiFi breaks, the device keeps
 * what it received and the app sends the rest later, with the range of the bytes in the request.
 * See IndiaNavi_App/docs/wifi_upload_api.md.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CONTENT_RANGE_H
#define CONTENT_RANGE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t start; /// first byte of the request in the file
    uint64_t end;   /// last byte of the request in the file, included
    uint64_t total; /// size of the whole file
} content_range_t;

/*
 * Parses "bytes {start}-{end}/{total}" (spaces around the numbers allowed). false for anything else,
 * and when start > end or end >= total.
 */
bool content_range_parse(const char* text, content_range_t* range);

#endif /* CONTENT_RANGE_H */
