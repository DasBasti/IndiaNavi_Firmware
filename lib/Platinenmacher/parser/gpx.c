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
static gpx_state_e state = SKIP;
static gpx_cdata_e cdata = NONE;
static uint32_t waypoint_num = 0;
static waypoint_t* wp = NULL;
waypoint_t* first_wp = NULL;
uint32_t (*add_waypoint)(waypoint_t* wp);

gpx_t* gpx;


void process_tokens(const char* buffer, sxmltok_t* tokens, sxml_t* parser)
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
                gpx->track_name = RTOS_Malloc(sizeof(char) * (strlen(buf)+1));
                strcpy(gpx->track_name, buf);
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

gpx_t* gpx_parser(const char* gpx_file_data, uint32_t (*add_waypoint_cb)(waypoint_t* wp))
{
    add_waypoint = add_waypoint_cb;
    gpx = RTOS_Malloc(sizeof(gpx_t));
    /* Output token table */
    sxmltok_t tokens[128];

    /* Parser object stores all data required for SXML to be reentrant */
    sxml_t parser;
    sxml_init(&parser);
    size_t data_len = strlen(gpx_file_data);

    for (;;) {
        sxmlerr_t err = sxml_parse(&parser, gpx_file_data, data_len, tokens, COUNT(tokens));
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
            process_tokens(gpx_file_data, tokens, &parser);

            /* Parser can now safely reuse all of the token table */
            parser.ntokens = 0;
            break;
        }

        case SXML_ERROR_BUFFERDRY: {
            /* Need to processs existing tokens before buffer is overwritten with new data */
            process_tokens(gpx_file_data, tokens, &parser);

            parser.ntokens = 0;

            ESP_LOGE(__func__, "bufferdry but why?");

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
            ESP_LOGI("xml_error", "%.30s [%d]", gpx_file_data + parser.bufferpos, parser.bufferpos);
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
    gpx->waypoints_num = waypoint_num;
    gpx->waypoints = first_wp;
    return gpx;
}