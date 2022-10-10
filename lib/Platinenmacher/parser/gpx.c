/*
 * GPX parser
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gpx.h"

#include "sxml.h"

#ifdef TESTING
#    include <assert.h>
#    include <stdlib.h>
#    ifndef atoff
#        define atoff(s) (float)atof(s)
#    endif
#endif

#define BUFFER_MAXLEN 1024
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef enum {
    SKIP,
    TRK,
    TRK_NAME,
    TRKSEG,
    TRKPT,
    ELE,
} gpx_state_e;

typedef enum {
    NONE,
    LON,
    LAT,
} gpx_cdata_e;

/* Input XML text */
static char buffer[BUFFER_MAXLEN];
static uint32_t bufferlen = 0;
static size_t file_pos = 0;
static gpx_state_e state = SKIP;
static gpx_cdata_e cdata = NONE;
static uint32_t waypoint_num = 0;
static waypoint_t* wp = NULL;
waypoint_t* first_wp = NULL;
uint32_t (*add_waypoint)(waypoint_t* wp);

void ff_xml(sxml_t* parser, const char* gpx)
{
    /*
     Example of how to reuse buffer array.
     Move unprocessed buffer content to start of array
    */
    bufferlen -= parser->bufferpos;
    memmove(buffer, buffer + parser->bufferpos, bufferlen);

    /*
     If your buffer is smaller than the size required to complete a token the parser will endlessly call SXML_ERROR_BUFFERDRY.
     You will most likely encounter this problem if you have XML comments longer than BUFFER_MAXLEN in size.
     SXML_CHARACTER solves this problem by dividing the data over multiple tokens, but other token types remain affected.
    */
    assert(bufferlen < BUFFER_MAXLEN);

    /* Fill remaining buffer with new data from file */
    const char* new_gpx = strncpy(buffer + bufferlen, gpx + file_pos, BUFFER_MAXLEN - bufferlen);
    // assert (0 < len);
    size_t str_len = strlen(new_gpx);
    file_pos += str_len;
    bufferlen += str_len;
}

void process_tokens(char* buffer, sxmltok_t* tokens, sxml_t* parser)
{
    char buf[255];
    for (uint32_t i = 0; i < parser->ntokens; i++) {
        strncpy(buf, buffer + tokens[i].startpos, tokens[i].endpos - tokens[i].startpos);
        buf[tokens[i].endpos - tokens[i].startpos] = 0;
        switch (tokens[i].type) {
        case SXML_STARTTAG:
            if (state == SKIP && strcmp("trk", buf) == 0)
                state = TRK;
            else if (state == TRK && strcmp("name", buf) == 0)
                state = TRK_NAME;
            else if (state == TRK && strcmp("trkseg", buf) == 0)
                state = TRKSEG;
            else if (state == TRKSEG && strcmp("trkpt", buf) == 0) {
                state = TRKPT;
                wp = RTOS_Malloc(sizeof(waypoint_t));
                if (wp && first_wp == 0)
                    first_wp = wp;
#ifndef TESTING
                if (!wp)
                    ESP_LOGE("gpx_parser", "No Waypoint allocated");
#endif
            } else if (state == TRKPT && strcmp("ele", buf) == 0)
                state = ELE;
            break;
        case SXML_ENDTAG:
            if (state == TRK && strcmp("trk", buf) == 0)
                state = SKIP;
            else if (state == TRK_NAME && strcmp("name", buf) == 0)
                state = TRK;
            else if (state == TRKSEG && strcmp("trkseg", buf) == 0)
                state = TRK;
            else if (state == TRKPT && strcmp("trkpt", buf) == 0) {
                state = TRKSEG;
                if (wp)
                    waypoint_num = add_waypoint(wp);
            } else if (state == ELE && strcmp("ele", buf) == 0)
                state = TRKPT;
            break;
        case SXML_CHARACTER:
            if (state == TRK_NAME) {
#ifndef TESTING
                ESP_LOGI("xml_data", "name: %s", buf);
#endif
            } else if (state == ELE) {
                if (wp)
                    wp->ele = atoff(buf);
            } else if (state == TRKPT) {
                if (cdata == LAT) {
                    if (wp)
                        wp->lat = atoff(buf);
                    cdata = NONE;
                }
                if (cdata == LON) {
                    if (wp)
                        wp->lon = atoff(buf);
                    cdata = NONE;
                }
            }
            break;
        case SXML_CDATA:
            if (state == TRKPT && strcmp("lat", buf) == 0)
                cdata = LAT;
            else if (state == TRKPT && strcmp("lon", buf) == 0)
                cdata = LON;
            break;
        case SXML_INSTRUCTION:
            // ESP_LOGI("xml_instruction", "%s", buf);
            break;
        case SXML_DOCTYPE:
            // ESP_LOGI("xml_doctype", "%s", buf);
            break;
        case SXML_COMMENT:
            // ESP_LOGI("xml_starttag", "%s", buf);
            break;
        default:
            break;
        }
#ifndef TESTING
        vPortYield();
#endif
    }
}

waypoint_t* gpx_parser(const char* gpx_file_data, uint32_t (*add_waypoint_cb)(waypoint_t* wp), uint16_t* num_wp)
{
    add_waypoint = add_waypoint_cb;
    /* Output token table */
    sxmltok_t tokens[128];

    /* Parser object stores all data required for SXML to be reentrant */
    sxml_t parser;
    sxml_init(&parser);

    for (;;) {
        sxmlerr_t err = sxml_parse(&parser, buffer, bufferlen, tokens, COUNT(tokens));
        if (err == SXML_SUCCESS)
            break;

        switch (err) {
        case SXML_ERROR_TOKENSFULL: {
            /*
             Need to give parser more space for tokens to continue parsing.
             We choose here to reuse the existing token table once tokens have been processed.

             Example of some processing of the token data.
             Instead you might be interested in creating your own DOM structure
             or other processing of XML data useful to your application.
            */
            process_tokens(buffer, tokens, &parser);

            /* Parser can now safely reuse all of the token table */
            parser.ntokens = 0;
            break;
        }

        case SXML_ERROR_BUFFERDRY: {
            /* Need to processs existing tokens before buffer is overwritten with new data */
            process_tokens(buffer, tokens, &parser);

            parser.ntokens = 0;

            ff_xml(&parser, gpx_file_data);

            /* Parser will now have to read from beginning of buffer to contiue */
            parser.bufferpos = 0;
            break;
        }

        case SXML_ERROR_XMLINVALID: {

            /* Example of some simple error reporting */
            // lineno+= count_lines (buffer, parser.bufferpos);
            // fprintf(stderr, "Error while parsing line\n");

            /* Print out contents of line containing the error */
            // sprintf (fmt, "%%.%ds", MIN (bufferlen - parser.bufferpos, 72));
            //  fprintf (stderr, fmt, buffer + parser.bufferpos);
#ifndef TESTING
            ESP_LOGI("xml_error", "%.3s", buffer + parser.bufferpos);
#endif
            // abort();
            break;
        }

        default:
            assert(0);
            break;
        }
#ifndef TESTING
        vPortYield();
#endif
    }
    if (num_wp)
        *num_wp = waypoint_num;
    return first_wp;
}