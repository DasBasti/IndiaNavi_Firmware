/*
 * GPX parser
 *
 * Copyright (c) 2022, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gpx.h"

#include "sxml.h"

#if defined(TESTING) || defined(LINUX)
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
static uint32_t waypoint_num = 0; // number of waypoints added
static waypoint_t* wp = NULL;
waypoint_t* first_wp = NULL;
uint32_t (*add_waypoint)(waypoint_t* wp);

gpx_t* gpx;


void process_tokens(const char* buffer, sxmltok_t* tokens, sxml_t* parser)
{
    char buf[255];
    for (uint32_t i = 0; i < parser->ntokens; i++) {
        // tokens can be longer than buf (comments, descriptions), truncate them
        size_t len = tokens[i].endpos - tokens[i].startpos;
        if (len > sizeof(buf) - 1)
            len = sizeof(buf) - 1;
        memcpy(buf, buffer + tokens[i].startpos, len);
        buf[len] = 0;
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
                if (wp) // unfinished waypoint, reuse it
                    memset(wp, 0, sizeof(waypoint_t));
                else
                    wp = RTOS_Malloc_Large(sizeof(waypoint_t)); // thousands of points, keep them out of internal RAM
                if (wp && first_wp == 0)
                    first_wp = wp;
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
                if (wp) {
                    add_waypoint(wp);
                    waypoint_num++;
                    wp = NULL; // owned by the waypoint list now
                }
            } else if (state == ELE && strcmp("ele", buf) == 0)
                state = TRKPT;
            break;
        case SXML_CHARACTER:
            if (state == TRK_NAME) {
                RTOS_Free(gpx->track_name);
                gpx->track_name = RTOS_Malloc(sizeof(char) * (strlen(buf) + 1));
                if (gpx->track_name)
                    strcpy(gpx->track_name, buf);
                ESP_LOGI("xml_data", "name: %s", buf);
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
            ESP_LOGI("xml_instruction", "%s", buf);
            break;
        case SXML_COMMENT:
            ESP_LOGI("xml_comment", "%s", buf);
            break;
        case SXML_DOCTYPE:
            ESP_LOGI("xml_doctype", "%s", buf);
            break;
        default: /* LCOV_EXCL_START */
            assert("case unhandled" && 0);
            break;/* LCOV_EXCL_STOP */
        }
#if !defined(TESTING) && !defined(LINUX)
        vPortYield();
#endif
    }
}

gpx_t* gpx_parser(const char* gpx_file_data, uint32_t (*add_waypoint_cb)(waypoint_t* wp))
{
    add_waypoint = add_waypoint_cb;
    // reset parser state from previous runs
    waypoint_num = 0;
    first_wp = NULL;
    wp = NULL;
    state = SKIP;
    cdata = NONE;
    gpx = RTOS_Malloc(sizeof(gpx_t));
    if (!gpx || !gpx_file_data)
        return gpx;
    /* Output token table */
    sxmltok_t tokens[128];

    /* Parser object stores all data required for SXML to be reentrant */
    sxml_t parser;
    sxml_init(&parser);
    size_t data_len = strlen(gpx_file_data);

    size_t parser_running=1;
    while (parser_running) {
        sxmlerr_t err = sxml_parse(&parser, gpx_file_data, data_len, tokens, COUNT(tokens));
        if (parser.ntokens)
            process_tokens(gpx_file_data, tokens, &parser);

        if (err == SXML_SUCCESS)
            break;

        switch (err) {
        case SXML_ERROR_TOKENSFULL: 
            /*
             Need to give parser more space for tokens to continue parsing.
             We choose here to reuse the existing token table once tokens have been processed.
            */
            parser.ntokens = 0;
            break;

        case SXML_ERROR_BUFFERDRY:
            /*
             The whole file is in the buffer, so no more data will follow and
             the file is incomplete. Stop here, restarting at the beginning of
             the buffer would parse the same data again and never end.
            */
            ESP_LOGI("xml_error", "incomplete file at %u of %u", (unsigned)parser.bufferpos, (unsigned)data_len);
            parser_running = 0;
            break;

        case SXML_ERROR_XMLINVALID: 
            /*
             An error occoured and we break parsing.
            */
            ESP_LOGI("xml_error", "%.30s [%d]", gpx_file_data + parser.bufferpos, parser.bufferpos);
            parser_running = 0;
            break;

        default: /* LCOV_EXCL_START */
            assert(0);
            break; /* LCOV_EXCL_STOP */
        }
#if !defined(TESTING) && !defined(LINUX)
        vPortYield();
#endif
    }
    
    // a waypoint that was started but never finished is not in the list
    if (wp) {
        if (wp == first_wp)
            first_wp = NULL;
        RTOS_Free(wp);
        wp = NULL;
    }

    gpx->waypoints_num = waypoint_num;
    gpx->waypoints = first_wp;
    return gpx;
}
/**
 * Free gpx data. Waypoints are owned by the waypoint list and freed there.
 */
void gpx_free(gpx_t* gpx_data)
{
    if (!gpx_data)
        return;
    RTOS_Free(gpx_data->track_name);
    RTOS_Free(gpx_data);
}
