/*
 * Content-Range of an upload that continues a file, see content_range.h
 *
 * SPDX-License-Identifier: MIT
 */

#include "content_range.h"

#include <ctype.h>
#include <string.h>

/* a decimal number of up to 19 digits, text points behind it afterwards */
static bool parse_number(const char** text, uint64_t* value)
{
    const char* c = *text;
    while (*c == ' ')
        c++;
    if (!isdigit((unsigned char)*c))
        return false;
    uint64_t number = 0;
    int digits = 0;
    for (; isdigit((unsigned char)*c); c++) {
        if (++digits > 19)
            return false;
        number = number * 10 + (uint64_t)(*c - '0');
    }
    while (*c == ' ')
        c++;
    *value = number;
    *text = c;
    return true;
}

bool content_range_parse(const char* text, content_range_t* range)
{
    static const char unit[] = "bytes ";
    if (strncmp(text, unit, sizeof(unit) - 1) != 0)
        return false;
    const char* c = text + sizeof(unit) - 1;
    content_range_t r;
    if (!parse_number(&c, &r.start) || *c++ != '-' || !parse_number(&c, &r.end) || *c++ != '/'
        || !parse_number(&c, &r.total) || *c != 0)
        return false;
    if (r.start > r.end || r.end >= r.total)
        return false;
    *range = r;
    return true;
}
