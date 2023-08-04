/*
 * Parser for PMTK responses
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pmtk_parser.h"
#include <string.h>

typedef struct {
    uint32_t packet_type;
    esp_err_t (*parse_packet_type)(esp_gps_t* esp_gps);
} message_parser_t;

uint32_t messageNumber;
char* pmtkSystemMessages[] = {
    "Unknown",
    "Startup",
    "Notification for host aiding EPO",
    "Notification for the transisiton to normal mode done successfully",
};

esp_err_t parse_packet_type_353(esp_gps_t* esp_gps)
{
    switch (esp_gps->item_num) {
    case 3: /* Enabled GPS */
        if (esp_gps->item_str[0] == '1')
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: GPS enabled");
        else
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: GPS disabled");
        break;
    case 4: /* Enabled GLONASS */
        if (esp_gps->item_str[0] == '1')
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: GLONASS enabled");
        else
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: GLONASS disabled");

        break;
    case 5: /* Enabled GALILEO */
        if (esp_gps->item_str[0] == '1')
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: GALILEO enabled");
        else
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: GALILEO disabled");

        break;
    case 6: /* Enabled GALILEO_FULL*/
        if (esp_gps->item_str[0] == '1')
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: GALILEO_FULL enabled");
        else
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: GALILEO_FULL disabled");

        break;
    case 7: /* Enables BEIDOU */
        if (esp_gps->item_str[0] == '1')
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: BEIDOU enabled");
        else
            ESP_LOGI(__func__, "PMTK_API_SET_GNSS_SEARCH_MODE: BEIDOU disabled");
    }
    return ESP_OK;
}

esp_err_t parse_packet_type_161(esp_gps_t* esp_gps)
{
    ESP_LOGI(__func__, "PMTK_CMD_STANDBY_MODE: %s", esp_gps->item_str);
    return ESP_OK;
}

static message_parser_t message_parser[] = {
    { .packet_type = 161,
        .parse_packet_type = parse_packet_type_161 },
    { .packet_type = 353,
        .parse_packet_type = parse_packet_type_353 },
};

esp_err_t pmtk_detect(esp_gps_t* esp_gps)
{
    if (strstr(esp_gps->item_str, "PMTK")) {
        messageNumber = atoi(esp_gps->item_str + 5);
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t pmtk_parse(esp_gps_t* esp_gps)
{
    /* Process PMTK statement
   $PMTK001,353,3,1,1,1,0,0,15*00
   1: Message
       353: PMTK_API_SET_GNSS_SEARCH_MODE
   2: Flag
       0 - Invalide packet type
       1 - Unsupported packet type
       2 - Valid packet, action failed
       3 - Valid packet, action succeeded
   3 ..: Packet specific

   */
    static int message_id;
    switch (messageNumber) {
    case 1:
        switch (esp_gps->item_num) {
        case 1: /* Process message */
            message_id = atoi(esp_gps->item_str);
            for (int i = 0; i < sizeof(message_parser); i++)
                if (message_parser[i].packet_type == message_id) {
                    if (message_parser[i].parse_packet_type)
                        return ESP_OK;
                }
            return ESP_ERR_NOT_SUPPORTED;
            break;
        case 2: /* Process flag */
            int flag = atoi(esp_gps->item_str);
            switch (flag) {
            case 0:
                return ESP_ERR_INVALID_ARG;
            case 1:
                return ESP_ERR_NOT_SUPPORTED;
            case 2:
                return ESP_FAIL;
            case 3:
                return ESP_OK;
            }
            break;
        default:
            if (message_id)
                return message_parser[message_id].parse_packet_type(esp_gps);
            break;
        }
        break;
    case 10:
        /* PMTK_SYS_MSG */
        int systemMessage = atoi(esp_gps->item_str);
        ESP_LOGI(__func__, "PMTK_SYS_MSG: %s", pmtkSystemMessages[systemMessage]);
        break;
    case 11:
        /* PMTK_TXT_MSG */
        ESP_LOGI(__func__, "PMTK_TXT_MSG: %s", esp_gps->item_str);
        messageNumber = 0;
        break;
    }
    return ESP_OK;
}