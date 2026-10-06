/*
 * Bluetooth LE protocol of the IndiaNavi, API version 1
 *
 * Encoding and decoding of the characteristic values and everything else
 * that has no hardware dependency, so it can be tested on the host.
 * The byte layouts are described in IndiaNavi_App/docs/ble_api.md.
 * All numbers are little endian.
 *
 * Copyright (c) 2026, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BLEP_API_VERSION 1

/*
 * 128 bit UUIDs 494e4449-00NN-4e41-5649-000000000000 ("INDI" "NAVI"),
 * as the 16 bytes NimBLE wants (least significant byte first).
 * BLEP_UUID128_BYTES is the plain list for BLE_UUID128_INIT().
 */
#define BLEP_UUID128_BYTES(n) 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x56, 0x41, 0x4e, (n), 0x00, 0x49, 0x44, 0x4e, 0x49
#define BLEP_UUID128(n) { BLEP_UUID128_BYTES(n) }

#define BLEP_UUID_SERVICE 0x01
#define BLEP_UUID_INFO 0x02
#define BLEP_UUID_TIME 0x03
#define BLEP_UUID_POSITION_IN 0x04
#define BLEP_UUID_POSITION_OUT 0x05
#define BLEP_UUID_WIFI_CONTROL 0x06
#define BLEP_UUID_WIFI_STATUS 0x07
#define BLEP_UUID_SETTINGS 0x08
#define BLEP_UUID_OTA_CONTROL 0x09
#define BLEP_UUID_OTA_DATA 0x0a
#define BLEP_UUID_DEVICE_CONTROL 0x0b

/* ATT error for a value the device does not accept (Value Not Allowed) */
#define BLEP_ATT_ERR_VALUE_NOT_ALLOWED 0x13

typedef enum {
    BLEP_OK = 0,
    BLEP_ERR_LENGTH, /// value has the wrong length
    BLEP_ERR_RANGE,  /// a number is outside of its range
    BLEP_ERR_FORMAT, /// unknown command or reserved bits set
} blep_err_t;

/* ---- Info ---- */

#define BLEP_INFO_FLAG_OTA 0x01      /// firmware can be updated over WiFi and BLE
#define BLEP_INFO_FLAG_CHARGING 0x02 /// a charger is connected
#define BLEP_INFO_HEADER_SIZE 4

/**
 * Info value: api, flags, battery in percent, 0, firmware version text (not terminated)
 *
 * @return number of bytes written, 0 if out is too small
 */
size_t blep_info_encode(uint8_t* out, size_t size, uint8_t flags, uint8_t battery_percent, const char* firmware);

/* ---- Time ---- */

#define BLEP_TIME_SIZE 8
#define BLEP_TIME_MIN 1767225600LL /// 2026-01-01, the firmware is newer than this
#define BLEP_TIME_MAX 4102444800LL /// 2100-01-01

/** Seconds since 1970-01-01 UTC as 64 bit number */
blep_err_t blep_time_decode(const uint8_t* in, size_t len, int64_t* epoch);
void blep_time_encode(uint8_t* out, int64_t epoch);

typedef struct {
    int year; /// 2026
    int month; /// 1..12
    int day; /// 1..31
    int hour;
    int minute;
    int second;
} blep_utc_t;

int64_t blep_utc_to_epoch(const blep_utc_t* utc);
void blep_epoch_to_utc(int64_t epoch, blep_utc_t* utc);

/* ---- Position ---- */

#define BLEP_POSITION_IN_SIZE 16
#define BLEP_POSITION_OUT_SIZE 16

/* fix values of the position the device reports. 0..6 are the NMEA values of the GPS module */
#define BLEP_FIX_INVALID 0
#define BLEP_FIX_GPS 1
#define BLEP_FIX_DGPS 2
#define BLEP_FIX_DR 6
#define BLEP_FIX_PHONE 7 /// position of the phone, no GPS fix yet

/* Position the phone sends to the device */
typedef struct {
    int32_t latitude_e7;  /// degrees * 1e7
    int32_t longitude_e7; /// degrees * 1e7
    int16_t altitude_m;
    uint16_t accuracy_m;  /// 1 sigma radius, 0 is not allowed
    uint32_t timestamp;   /// seconds since 1970-01-01 UTC when the phone measured it
} blep_position_in_t;

/* Position the device sends to the phone */
typedef struct {
    int32_t latitude_e7;
    int32_t longitude_e7;
    int16_t altitude_m;
    uint16_t hdop_x10; /// HDOP * 10
    uint8_t fix;       /// BLEP_FIX_*
    uint8_t satellites_in_use;
    uint8_t satellites_in_view;
} blep_position_out_t;

blep_err_t blep_position_in_decode(const uint8_t* in, size_t len, blep_position_in_t* position);
void blep_position_out_encode(uint8_t* out, const blep_position_out_t* position);

int32_t blep_degrees_to_e7(double degrees);
double blep_e7_to_degrees(int32_t e7);
int16_t blep_meters_to_i16(double meters);

/* ---- WiFi access point ---- */

#define BLEP_WIFI_OFF 0
#define BLEP_WIFI_ON 1
#define BLEP_WIFI_STATUS_HEADER_SIZE 2

/** One byte: BLEP_WIFI_OFF or BLEP_WIFI_ON */
blep_err_t blep_wifi_control_decode(const uint8_t* in, size_t len, uint8_t* on);

/**
 * Status value: running (0/1), number of phones joined the access point, SSID.
 * The password never leaves the device, the phone reads it from the QR code.
 *
 * @return number of bytes written, 0 if out is too small
 */
size_t blep_wifi_status_encode(uint8_t* out, size_t size, uint8_t running, uint8_t stations, const char* ssid);

/* ---- Display settings ---- */

#define BLEP_SETTINGS_SIZE 4
#define BLEP_SETTING_SHOW_TRACK 0x01
#define BLEP_SETTING_SHOW_HEIGHT_GRAPH 0x02
#define BLEP_SETTING_FLAGS_MASK 0x03

#define BLEP_UPDATE_INTERVAL_MIN_S 30
#define BLEP_UPDATE_INTERVAL_MAX_S 600
#define BLEP_UPDATE_INTERVAL_DEFAULT_S 60

typedef struct {
    uint8_t flags;
    uint16_t update_interval_s; /// time between two automatic screen updates
} blep_settings_t;

/** Flags, 0, interval in seconds (u16). An interval outside of 30..600 is rejected. */
blep_err_t blep_settings_decode(const uint8_t* in, size_t len, blep_settings_t* settings);
void blep_settings_encode(uint8_t* out, const blep_settings_t* settings);
uint16_t blep_clamp_update_interval(uint32_t seconds);

/* ---- Device control ---- */

#define BLEP_DEVICE_FORGET_PHONE 0x01 /// delete the bond and allow another phone to pair

blep_err_t blep_device_control_decode(const uint8_t* in, size_t len, uint8_t* command);

/* ---- Firmware update ---- */

#define BLEP_OTA_CMD_START 0x01   /// + u32 image size
#define BLEP_OTA_CMD_ABORT 0x02
#define BLEP_OTA_CMD_FINISH 0x03  /// all bytes sent, check the image and select it for the next boot
#define BLEP_OTA_CMD_RESTART 0x04 /// boot the new firmware
#define BLEP_OTA_START_SIZE 5

#define BLEP_OTA_MIN_MTU 185           /// a slower link is refused
#define BLEP_OTA_ACK_INTERVAL 4096     /// the device reports its position after this many bytes
#define BLEP_OTA_WINDOW 8192           /// the phone sends at most this much beyond the last report
#define BLEP_OTA_MIN_BATTERY 30        /// percent, if no charger is connected
#define BLEP_OTA_TIMEOUT_S 120         /// no data for this long aborts the update
#define BLEP_OTA_ASSUMED_RATE 40000    /// bytes per second the display estimate is based on
#define BLEP_OTA_PROGRESS_SCREEN_S 15  /// updates that take longer show a progress screen
#define BLEP_OTA_STATUS_SIZE 6

typedef enum {
    BLEP_OTA_IDLE = 0,
    BLEP_OTA_READY,     /// START accepted, send the image from offset 0
    BLEP_OTA_RECEIVING, /// periodic report of the number of bytes that are in flash
    BLEP_OTA_VERIFYING, /// FINISH is running
    BLEP_OTA_DONE,      /// image is valid and selected, RESTART boots it
    BLEP_OTA_ERROR,     /// see error code, the update is over
} blep_ota_state_t;

typedef enum {
    BLEP_OTA_NO_ERROR = 0,
    BLEP_OTA_ERR_BUSY,        /// another update or a transfer is running
    BLEP_OTA_ERR_SIZE,        /// image is empty or does not fit into the partition
    BLEP_OTA_ERR_BATTERY,     /// battery too low and no charger
    BLEP_OTA_ERR_MTU,         /// link too slow, see BLEP_OTA_MIN_MTU
    BLEP_OTA_ERR_FLASH,       /// can not write the flash
    BLEP_OTA_ERR_INVALID,     /// image rejected by the check
    BLEP_OTA_ERR_SEQUENCE,    /// command not allowed in this state or more data than announced
    BLEP_OTA_ERR_TIMEOUT,     /// no data for BLEP_OTA_TIMEOUT_S
    BLEP_OTA_ERR_OVERFLOW,    /// the phone sent more than BLEP_OTA_WINDOW bytes without waiting
    BLEP_OTA_ERR_ABORTED,     /// aborted by the phone, disconnect or power down
} blep_ota_error_t;

/** First byte is the command, START carries the size of the image */
blep_err_t blep_ota_command_decode(const uint8_t* in, size_t len, uint8_t* command, uint32_t* size);

/** state, error, u32 number of bytes that are in flash */
void blep_ota_status_encode(uint8_t* out, blep_ota_state_t state, blep_ota_error_t error, uint32_t offset);

/** Estimated duration of a transfer, based on BLEP_OTA_ASSUMED_RATE */
uint32_t blep_ota_estimate_seconds(uint32_t size);

/** An update that is estimated to take longer than BLEP_OTA_PROGRESS_SCREEN_S shows the progress on the display */
int blep_ota_needs_progress_screen(uint32_t size);

/* ---- GPS module ---- */

/*
 * PMTK740 (UTC time) and PMTK741 (position and time) tell the GPS module where
 * and when it is, so it does not have to search the whole sky.
 *
 * @return length of the sentence including "\r\n", 0 if the buffer is too small
 */
#define BLEP_PMTK_MAX_LEN 96
size_t blep_pmtk_time(char* out, size_t size, const blep_utc_t* utc);
size_t blep_pmtk_position(char* out, size_t size, double latitude, double longitude, int altitude_m, const blep_utc_t* utc);

#endif /* BLE_PROTOCOL_H */
