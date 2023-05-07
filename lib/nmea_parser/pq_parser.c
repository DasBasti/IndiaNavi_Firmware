/*
 * Parser for Quectell GNSS SDK command responses
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pq_parser.h"
#include <string.h>

typedef enum {
    STATEMENT_PQBAUD = 1, /*!< Change NMEA port default baud rate */
    STATEMENT_PQEPE,      /*!< Enable/Disable PQEPE sentence output */
    STATEMENT_PQ1PPS,     /*!< Set the type and pulse width of 1PPS output */
    STATEMENT_PQFLP,      /*!< Set the module into FLP (Fitness Low Power) mode */
    STATEMENT_PQTXT,      /*!< Enable/Disable GPTXT sentence output */
    STATEMENT_PQECEF,     /*!< Enable/Disable ECEFPOSVEL sentence output */
    STATEMENT_PQODO,      /*!< Start/Stop odometer reading */
    STATEMENT_PQPZ90,     /*!< Enable/Disable switching from WGS84 to PZ-90.11 */
    STATEMENT_PQGLP,      /*!< Set the module into GLP (GNSS Low Power) mode */
    STATEMENT_PQVEL,      /*!< Enable/Disable 3 ways velocity sentence */
    STATEMENT_PQJAM,      /*!< Enable/Disable jamming detection function */
    STATEMENT_PQRLM,      /*!< Enable/Disable return link message output */
    STATEMENT_PQGEO,      /*!< Configure parameters of geo-fence */
} pq_statement_t;

static pq_statement_t statement = STATEMENT_UNKNOWN;

esp_err_t pq_parse_glp(esp_gps_t* esp_gps)
{
    static int write = 0;

    if (esp_gps->item_num == 1) {
        if (esp_gps->item_str[0] == 'W') {
            write = 1;
        }
    } else if (esp_gps->item_num == 2) {
        if (write)
            ESP_LOGI(__func__, "GLP set: %s", esp_gps->item_str);
        else
            ESP_LOGI(__func__, "GLP get: %s", esp_gps->item_str);
    }
    return ESP_OK;
}

esp_err_t pq_detect(esp_gps_t* esp_gps)
{
    if (strstr(esp_gps->item_str, "PQBAUD")) {
        statement = STATEMENT_PQBAUD;
    } else if (strstr(esp_gps->item_str, "PQEPE")) {
        statement = STATEMENT_PQEPE;
    } else if (strstr(esp_gps->item_str, "PQ1PPS")) {
        statement = STATEMENT_PQ1PPS;
    } else if (strstr(esp_gps->item_str, "PQLFP")) {
        statement = STATEMENT_PQFLP;
    } else if (strstr(esp_gps->item_str, "PQTXT")) {
        statement = STATEMENT_PQTXT;
    } else if (strstr(esp_gps->item_str, "PQECEF")) {
        statement = STATEMENT_PQECEF;
    } else if (strstr(esp_gps->item_str, "PQODO")) {
        statement = STATEMENT_PQODO;
    } else if (strstr(esp_gps->item_str, "PQPZ90")) {
        statement = STATEMENT_PQPZ90;
    } else if (strstr(esp_gps->item_str, "PQGLP")) {
        statement = STATEMENT_PQGLP;
    } else if (strstr(esp_gps->item_str, "PQVEL")) {
        statement = STATEMENT_PQVEL;
    } else if (strstr(esp_gps->item_str, "PQJAM")) {
        statement = STATEMENT_PQJAM;
    } else if (strstr(esp_gps->item_str, "PQRLM")) {
        statement = STATEMENT_PQRLM;
    } else if (strstr(esp_gps->item_str, "PQGEO")) {
        statement = STATEMENT_PQGEO;
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

esp_err_t pq_parse(esp_gps_t* esp_gps)
{
    switch (statement) {
    case STATEMENT_PQBAUD:
        break;
    case STATEMENT_PQEPE:
        break;
    case STATEMENT_PQ1PPS:
        break;
    case STATEMENT_PQFLP:
        break;
    case STATEMENT_PQTXT:
        break;
    case STATEMENT_PQECEF:
        break;
    case STATEMENT_PQODO:
        break;
    case STATEMENT_PQPZ90:
        break;
    case STATEMENT_PQGLP:
        pq_parse_glp(esp_gps);
        break;
    case STATEMENT_PQVEL:
        break;
    case STATEMENT_PQJAM:
        break;
    case STATEMENT_PQRLM:
        break;
    case STATEMENT_PQGEO:
        break;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}