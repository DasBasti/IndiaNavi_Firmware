/*
 * Bluetooth LE protocol of the IndiaNavi, see ble_protocol.h
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void put_u16(uint8_t* out, uint16_t value)
{
    out[0] = value & 0xff;
    out[1] = value >> 8;
}

static void put_u32(uint8_t* out, uint32_t value)
{
    for (int i = 0; i < 4; i++)
        out[i] = (value >> (8 * i)) & 0xff;
}

static uint16_t get_u16(const uint8_t* in)
{
    return (uint16_t)(in[0] | (in[1] << 8));
}

static uint32_t get_u32(const uint8_t* in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

/* ---- Info ---- */

size_t blep_info_encode(uint8_t* out, size_t size, uint8_t flags, uint8_t battery_percent, const char* firmware)
{
    size_t text_len = firmware ? strlen(firmware) : 0;
    if (!out || size < BLEP_INFO_HEADER_SIZE + text_len)
        return 0;
    out[0] = BLEP_API_VERSION;
    out[1] = flags;
    out[2] = battery_percent > 100 ? 100 : battery_percent;
    out[3] = 0;
    if (text_len)
        memcpy(out + BLEP_INFO_HEADER_SIZE, firmware, text_len);
    return BLEP_INFO_HEADER_SIZE + text_len;
}

/* ---- Time ---- */

blep_err_t blep_time_decode(const uint8_t* in, size_t len, int64_t* epoch)
{
    if (!in || !epoch || len != BLEP_TIME_SIZE)
        return BLEP_ERR_LENGTH;
    uint64_t value = (uint64_t)get_u32(in) | ((uint64_t)get_u32(in + 4) << 32);
    if (value < (uint64_t)BLEP_TIME_MIN || value >= (uint64_t)BLEP_TIME_MAX)
        return BLEP_ERR_RANGE;
    *epoch = (int64_t)value;
    return BLEP_OK;
}

void blep_time_encode(uint8_t* out, int64_t epoch)
{
    uint64_t value = epoch < 0 ? 0 : (uint64_t)epoch;
    put_u32(out, (uint32_t)(value & 0xffffffffu));
    put_u32(out + 4, (uint32_t)(value >> 32));
}

/*
 * Days between 1970-01-01 and a date of the proleptic Gregorian calendar
 * (algorithm of Howard Hinnant, "days_from_civil")
 */
static int64_t days_from_civil(int year, int month, int day)
{
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t year_of_era = year - era * 400;
    int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

int64_t blep_utc_to_epoch(const blep_utc_t* utc)
{
    return days_from_civil(utc->year, utc->month, utc->day) * 86400LL
        + utc->hour * 3600LL + utc->minute * 60LL + utc->second;
}

void blep_epoch_to_utc(int64_t epoch, blep_utc_t* utc)
{
    int64_t days = epoch / 86400;
    int64_t seconds = epoch % 86400;
    if (seconds < 0) {
        seconds += 86400;
        days--;
    }
    // "civil_from_days"
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t day_of_era = days - era * 146097;
    int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int64_t mp = (5 * day_of_year + 2) / 153;
    int64_t day = day_of_year - (153 * mp + 2) / 5 + 1;
    int64_t month = mp < 10 ? mp + 3 : mp - 9;
    int64_t year = year_of_era + era * 400 + (month <= 2);

    utc->year = (int)year;
    utc->month = (int)month;
    utc->day = (int)day;
    utc->hour = (int)(seconds / 3600);
    utc->minute = (int)(seconds % 3600 / 60);
    utc->second = (int)(seconds % 60);
}

/* ---- Position ---- */

int32_t blep_degrees_to_e7(double degrees)
{
    return (int32_t)lround(degrees * 1e7);
}

double blep_e7_to_degrees(int32_t e7)
{
    return e7 / 1e7;
}

int16_t blep_meters_to_i16(double meters)
{
    if (meters != meters) // NaN
        return 0;
    if (meters > INT16_MAX)
        return INT16_MAX;
    if (meters < INT16_MIN)
        return INT16_MIN;
    return (int16_t)lround(meters);
}

blep_err_t blep_position_in_decode(const uint8_t* in, size_t len, blep_position_in_t* position)
{
    if (!in || !position || len != BLEP_POSITION_IN_SIZE)
        return BLEP_ERR_LENGTH;
    blep_position_in_t p;
    p.latitude_e7 = (int32_t)get_u32(in);
    p.longitude_e7 = (int32_t)get_u32(in + 4);
    p.altitude_m = (int16_t)get_u16(in + 8);
    p.accuracy_m = get_u16(in + 10);
    p.timestamp = get_u32(in + 12);
    if (p.latitude_e7 < -900000000 || p.latitude_e7 > 900000000
        || p.longitude_e7 < -1800000000 || p.longitude_e7 > 1800000000
        || p.accuracy_m == 0)
        return BLEP_ERR_RANGE;
    *position = p;
    return BLEP_OK;
}

void blep_position_out_encode(uint8_t* out, const blep_position_out_t* position)
{
    put_u32(out, (uint32_t)position->latitude_e7);
    put_u32(out + 4, (uint32_t)position->longitude_e7);
    put_u16(out + 8, (uint16_t)position->altitude_m);
    put_u16(out + 10, position->hdop_x10);
    out[12] = position->fix;
    out[13] = position->satellites_in_use;
    out[14] = position->satellites_in_view;
    out[15] = 0;
}

/* ---- WiFi access point ---- */

blep_err_t blep_wifi_control_decode(const uint8_t* in, size_t len, uint8_t* on)
{
    if (!in || !on || len != 1)
        return BLEP_ERR_LENGTH;
    if (in[0] != BLEP_WIFI_OFF && in[0] != BLEP_WIFI_ON)
        return BLEP_ERR_FORMAT;
    *on = in[0];
    return BLEP_OK;
}

size_t blep_wifi_status_encode(uint8_t* out, size_t size, uint8_t running, uint8_t stations, const char* ssid)
{
    size_t text_len = ssid ? strlen(ssid) : 0;
    if (!out || size < BLEP_WIFI_STATUS_HEADER_SIZE + text_len)
        return 0;
    out[0] = running ? 1 : 0;
    out[1] = stations;
    if (text_len)
        memcpy(out + BLEP_WIFI_STATUS_HEADER_SIZE, ssid, text_len);
    return BLEP_WIFI_STATUS_HEADER_SIZE + text_len;
}

/* ---- Display settings ---- */

uint16_t blep_clamp_update_interval(uint32_t seconds)
{
    if (seconds < BLEP_UPDATE_INTERVAL_MIN_S)
        return BLEP_UPDATE_INTERVAL_MIN_S;
    if (seconds > BLEP_UPDATE_INTERVAL_MAX_S)
        return BLEP_UPDATE_INTERVAL_MAX_S;
    return (uint16_t)seconds;
}

blep_err_t blep_settings_decode(const uint8_t* in, size_t len, blep_settings_t* settings)
{
    if (!in || !settings || len != BLEP_SETTINGS_SIZE)
        return BLEP_ERR_LENGTH;
    if (in[0] & ~BLEP_SETTING_FLAGS_MASK)
        return BLEP_ERR_FORMAT;
    uint16_t interval = get_u16(in + 2);
    if (interval < BLEP_UPDATE_INTERVAL_MIN_S || interval > BLEP_UPDATE_INTERVAL_MAX_S)
        return BLEP_ERR_RANGE;
    // 0 is the default, so an app that does not know the color keeps it blue
    if (in[1] > BLEP_TRACK_COLOR_MAX + 1)
        return BLEP_ERR_RANGE;
    settings->flags = in[0];
    settings->track_color = in[1] ? in[1] - 1 : BLEP_TRACK_COLOR_DEFAULT;
    settings->update_interval_s = interval;
    return BLEP_OK;
}

void blep_settings_encode(uint8_t* out, const blep_settings_t* settings)
{
    out[0] = settings->flags & BLEP_SETTING_FLAGS_MASK;
    out[1] = settings->track_color <= BLEP_TRACK_COLOR_MAX ? settings->track_color + 1 : 0;
    put_u16(out + 2, settings->update_interval_s);
}

/* ---- Device control ---- */

blep_err_t blep_device_control_decode(const uint8_t* in, size_t len, uint8_t* command)
{
    if (!in || !command || len != 1)
        return BLEP_ERR_LENGTH;
    if (in[0] != BLEP_DEVICE_FORGET_PHONE)
        return BLEP_ERR_FORMAT;
    *command = in[0];
    return BLEP_OK;
}

/* ---- Firmware update ---- */

blep_err_t blep_ota_command_decode(const uint8_t* in, size_t len, uint8_t* command, uint32_t* size)
{
    if (!in || !command || !size || len < 1)
        return BLEP_ERR_LENGTH;
    switch (in[0]) {
    case BLEP_OTA_CMD_START:
        if (len != BLEP_OTA_START_SIZE)
            return BLEP_ERR_LENGTH;
        *size = get_u32(in + 1);
        break;
    case BLEP_OTA_CMD_ABORT:
    case BLEP_OTA_CMD_FINISH:
    case BLEP_OTA_CMD_RESTART:
        if (len != 1)
            return BLEP_ERR_LENGTH;
        *size = 0;
        break;
    default:
        return BLEP_ERR_FORMAT;
    }
    *command = in[0];
    return BLEP_OK;
}

void blep_ota_status_encode(uint8_t* out, blep_ota_state_t state, blep_ota_error_t error, uint32_t offset)
{
    out[0] = (uint8_t)state;
    out[1] = (uint8_t)error;
    put_u32(out + 2, offset);
}

uint32_t blep_ota_estimate_seconds(uint32_t size)
{
    return (size + BLEP_OTA_ASSUMED_RATE - 1) / BLEP_OTA_ASSUMED_RATE;
}

int blep_ota_needs_progress_screen(uint32_t size)
{
    return blep_ota_estimate_seconds(size) > BLEP_OTA_PROGRESS_SCREEN_S;
}

/* ---- GPS module ---- */

/* The checksum is the XOR of all characters between '$' and '*' */
static size_t pmtk_finish(char* out, size_t size, size_t body_len)
{
    uint8_t checksum = 0;
    for (size_t i = 1; i < body_len; i++)
        checksum ^= (uint8_t)out[i];
    int tail = snprintf(out + body_len, size - body_len, "*%02X\r\n", checksum);
    if (tail < 0 || (size_t)tail >= size - body_len)
        return 0;
    return body_len + (size_t)tail;
}

size_t blep_pmtk_time(char* out, size_t size, const blep_utc_t* utc)
{
    if (!out || size == 0)
        return 0;
    int body = snprintf(out, size, "$PMTK740,%04d,%02d,%02d,%02d,%02d,%02d",
        utc->year, utc->month, utc->day, utc->hour, utc->minute, utc->second);
    if (body < 0 || (size_t)body >= size)
        return 0;
    return pmtk_finish(out, size, (size_t)body);
}

size_t blep_pmtk_position(char* out, size_t size, double latitude, double longitude, int altitude_m, const blep_utc_t* utc)
{
    if (!out || size == 0)
        return 0;
    int body = snprintf(out, size, "$PMTK741,%.6f,%.6f,%d,%04d,%02d,%02d,%02d,%02d,%02d",
        latitude, longitude, altitude_m, utc->year, utc->month, utc->day, utc->hour, utc->minute, utc->second);
    if (body < 0 || (size_t)body >= size)
        return 0;
    return pmtk_finish(out, size, (size_t)body);
}
