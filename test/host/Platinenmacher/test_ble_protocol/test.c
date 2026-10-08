#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "ble_protocol.h"

static void put_le(uint8_t* out, uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; i++)
        out[i] = (value >> (8 * i)) & 0xff;
}

void test_uuid_bytes_are_in_reverse_order()
{
    const uint8_t uuid[16] = BLEP_UUID128(BLEP_UUID_OTA_DATA);
    // 494e4449-000a-4e41-5649-000000000000
    TEST_ASSERT_EQUAL_UINT8(0x49, uuid[15]);
    TEST_ASSERT_EQUAL_UINT8(0x4e, uuid[14]);
    TEST_ASSERT_EQUAL_UINT8(0x0a, uuid[10]);
    TEST_ASSERT_EQUAL_UINT8(0x00, uuid[0]);
}

void test_info_has_version_flags_battery_and_firmware()
{
    uint8_t out[32];
    size_t len = blep_info_encode(out, sizeof(out), BLEP_INFO_FLAG_OTA | BLEP_INFO_FLAG_CHARGING, 150, "abc1234");
    TEST_ASSERT_EQUAL(11, len);
    TEST_ASSERT_EQUAL_UINT8(BLEP_API_VERSION, out[0]);
    TEST_ASSERT_EQUAL_UINT8(3, out[1]);
    TEST_ASSERT_EQUAL_UINT8(100, out[2]); // limited
    TEST_ASSERT_EQUAL_UINT8(0, out[3]);
    TEST_ASSERT_EQUAL_MEMORY("abc1234", out + 4, 7);
    TEST_ASSERT_EQUAL(0, blep_info_encode(out, 8, 0, 0, "abc1234")); // too small
}

void test_time_round_trip()
{
    uint8_t raw[BLEP_TIME_SIZE];
    int64_t epoch = 0;
    blep_time_encode(raw, 1790000000LL);
    TEST_ASSERT_EQUAL(BLEP_OK, blep_time_decode(raw, sizeof(raw), &epoch));
    TEST_ASSERT_TRUE(epoch == 1790000000LL);
}

void test_time_is_little_endian()
{
    uint8_t raw[BLEP_TIME_SIZE];
    blep_time_encode(raw, 0x0102030405LL);
    TEST_ASSERT_EQUAL_UINT8(0x05, raw[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, raw[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00, raw[7]);
}

void test_time_rejects_wrong_length_and_range()
{
    uint8_t raw[BLEP_TIME_SIZE];
    int64_t epoch = 5;
    blep_time_encode(raw, 1790000000LL);
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_time_decode(raw, 7, &epoch));
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_time_decode(NULL, 8, &epoch));
    blep_time_encode(raw, 0);
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_time_decode(raw, 8, &epoch));
    blep_time_encode(raw, BLEP_TIME_MIN - 1);
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_time_decode(raw, 8, &epoch));
    blep_time_encode(raw, BLEP_TIME_MIN);
    TEST_ASSERT_EQUAL(BLEP_OK, blep_time_decode(raw, 8, &epoch));
    blep_time_encode(raw, BLEP_TIME_MAX);
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_time_decode(raw, 8, &epoch));
    TEST_ASSERT_TRUE(epoch == BLEP_TIME_MIN); // not touched by the failures
}

void test_utc_and_epoch_match_known_dates()
{
    blep_utc_t utc = { 2026, 1, 1, 0, 0, 0 };
    TEST_ASSERT_TRUE(blep_utc_to_epoch(&utc) == 1767225600LL);
    blep_utc_t summer = { 2026, 7, 15, 13, 45, 59 };
    TEST_ASSERT_TRUE(blep_utc_to_epoch(&summer) == 1784123159LL);
    blep_utc_t leap = { 2028, 2, 29, 23, 59, 59 };
    TEST_ASSERT_TRUE(blep_utc_to_epoch(&leap) == 1835481599LL);
    blep_utc_t unix_start = { 1970, 1, 1, 0, 0, 0 };
    TEST_ASSERT_TRUE(blep_utc_to_epoch(&unix_start) == 0);
}

void test_epoch_to_utc_inverts_utc_to_epoch()
{
    int64_t samples[] = { 0, 951782400LL, 1767225600LL, 1784123159LL, 1835481599LL, 4102444799LL };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        blep_utc_t utc;
        blep_epoch_to_utc(samples[i], &utc);
        TEST_ASSERT_TRUE(blep_utc_to_epoch(&utc) == samples[i]);
    }
    blep_utc_t utc;
    blep_epoch_to_utc(1835481599LL, &utc);
    TEST_ASSERT_EQUAL(2028, utc.year);
    TEST_ASSERT_EQUAL(2, utc.month);
    TEST_ASSERT_EQUAL(29, utc.day);
    TEST_ASSERT_EQUAL(23, utc.hour);
    TEST_ASSERT_EQUAL(59, utc.minute);
    TEST_ASSERT_EQUAL(59, utc.second);
}

void test_position_in_decodes_all_fields()
{
    uint8_t raw[BLEP_POSITION_IN_SIZE];
    put_le(raw, (uint32_t)496268460, 4);
    put_le(raw + 4, (uint32_t)85818750, 4);
    put_le(raw + 8, (uint16_t)(int16_t)-12, 2);
    put_le(raw + 10, 25, 2);
    put_le(raw + 12, 1790000000u, 4);
    blep_position_in_t p;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_position_in_decode(raw, sizeof(raw), &p));
    TEST_ASSERT_EQUAL_INT32(496268460, p.latitude_e7);
    TEST_ASSERT_EQUAL_INT32(85818750, p.longitude_e7);
    TEST_ASSERT_EQUAL_INT16(-12, p.altitude_m);
    TEST_ASSERT_EQUAL_UINT16(25, p.accuracy_m);
    TEST_ASSERT_EQUAL_UINT32(1790000000u, p.timestamp);
}

void test_position_in_rejects_bad_values()
{
    uint8_t raw[BLEP_POSITION_IN_SIZE] = { 0 };
    blep_position_in_t p;
    put_le(raw + 10, 10, 2);
    TEST_ASSERT_EQUAL(BLEP_OK, blep_position_in_decode(raw, sizeof(raw), &p));
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_position_in_decode(raw, 15, &p));
    put_le(raw, (uint32_t)900000001, 4); // latitude over 90 degrees
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_position_in_decode(raw, sizeof(raw), &p));
    put_le(raw, 0, 4);
    put_le(raw + 4, (uint32_t)(int32_t)-1800000001, 4); // longitude under -180 degrees
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_position_in_decode(raw, sizeof(raw), &p));
    put_le(raw + 4, 0, 4);
    put_le(raw + 10, 0, 2); // no accuracy
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_position_in_decode(raw, sizeof(raw), &p));
}

void test_position_out_layout()
{
    blep_position_out_t p = {
        .latitude_e7 = -496268460,
        .longitude_e7 = 85818750,
        .altitude_m = 312,
        .hdop_x10 = 12,
        .fix = BLEP_FIX_PHONE,
        .satellites_in_use = 7,
        .satellites_in_view = 11,
    };
    uint8_t raw[BLEP_POSITION_OUT_SIZE];
    blep_position_out_encode(raw, &p);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)-496268460, (uint32_t)(raw[0] | raw[1] << 8 | raw[2] << 16 | (uint32_t)raw[3] << 24));
    TEST_ASSERT_EQUAL_UINT8(312 & 0xff, raw[8]);
    TEST_ASSERT_EQUAL_UINT8(312 >> 8, raw[9]);
    TEST_ASSERT_EQUAL_UINT8(12, raw[10]);
    TEST_ASSERT_EQUAL_UINT8(7, raw[12]);
    TEST_ASSERT_EQUAL_UINT8(7, raw[13]);
    TEST_ASSERT_EQUAL_UINT8(11, raw[14]);
    TEST_ASSERT_EQUAL_UINT8(0, raw[15]);
}

void test_degree_conversion()
{
    TEST_ASSERT_EQUAL_INT32(496268460, blep_degrees_to_e7(49.626846));
    TEST_ASSERT_EQUAL_INT32(-85818750, blep_degrees_to_e7(-8.581875));
    TEST_ASSERT_TRUE(blep_e7_to_degrees(496268460) > 49.6268459 && blep_e7_to_degrees(496268460) < 49.6268461);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, blep_meters_to_i16(1e9));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, blep_meters_to_i16(-1e9));
    TEST_ASSERT_EQUAL_INT16(0, blep_meters_to_i16(0.0 / 0.0));
    TEST_ASSERT_EQUAL_INT16(312, blep_meters_to_i16(311.6));
}

void test_wifi_control_accepts_only_zero_and_one()
{
    uint8_t on = 9;
    uint8_t v = 1;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_wifi_control_decode(&v, 1, &on));
    TEST_ASSERT_EQUAL_UINT8(1, on);
    v = 0;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_wifi_control_decode(&v, 1, &on));
    TEST_ASSERT_EQUAL_UINT8(0, on);
    v = 2;
    TEST_ASSERT_EQUAL(BLEP_ERR_FORMAT, blep_wifi_control_decode(&v, 1, &on));
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_wifi_control_decode(&v, 0, &on));
    uint8_t two[2] = { 1, 1 };
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_wifi_control_decode(two, 2, &on));
}

void test_wifi_status_has_ssid_but_no_password()
{
    uint8_t out[40];
    size_t len = blep_wifi_status_encode(out, sizeof(out), 1, 2, "IndiaNavi-A1B2");
    TEST_ASSERT_EQUAL(2 + 14, len);
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);
    TEST_ASSERT_EQUAL_UINT8(2, out[1]);
    TEST_ASSERT_EQUAL_MEMORY("IndiaNavi-A1B2", out + 2, 14);
    TEST_ASSERT_EQUAL(0, blep_wifi_status_encode(out, 10, 1, 2, "IndiaNavi-A1B2"));
}

void test_settings_round_trip()
{
    blep_settings_t in = { .flags = BLEP_SETTING_SHOW_TRACK | BLEP_SETTING_SHOW_HEIGHT_GRAPH, .track_color = 4, .update_interval_s = 600 };
    uint8_t raw[BLEP_SETTINGS_SIZE];
    blep_settings_encode(raw, &in);
    TEST_ASSERT_EQUAL_UINT8(3, raw[0]);
    TEST_ASSERT_EQUAL_UINT8(5, raw[1]);
    TEST_ASSERT_EQUAL_UINT8(600 & 0xff, raw[2]);
    TEST_ASSERT_EQUAL_UINT8(600 >> 8, raw[3]);
    blep_settings_t out;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_settings_decode(raw, sizeof(raw), &out));
    TEST_ASSERT_EQUAL_UINT8(3, out.flags);
    TEST_ASSERT_EQUAL_UINT8(4, out.track_color);
    TEST_ASSERT_EQUAL_UINT16(600, out.update_interval_s);
}

void test_settings_track_color()
{
    blep_settings_t s;
    uint8_t raw[BLEP_SETTINGS_SIZE] = { 1, 0, 60, 0 };
    // 0 is the default, as written by an app that does not know the color
    TEST_ASSERT_EQUAL(BLEP_OK, blep_settings_decode(raw, sizeof(raw), &s));
    TEST_ASSERT_EQUAL_UINT8(BLEP_TRACK_COLOR_DEFAULT, s.track_color);
    raw[1] = 1;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_settings_decode(raw, sizeof(raw), &s));
    TEST_ASSERT_EQUAL_UINT8(0, s.track_color);
    raw[1] = BLEP_TRACK_COLOR_MAX + 1;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_settings_decode(raw, sizeof(raw), &s));
    TEST_ASSERT_EQUAL_UINT8(BLEP_TRACK_COLOR_MAX, s.track_color);
    raw[1] = BLEP_TRACK_COLOR_MAX + 2;
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_settings_decode(raw, sizeof(raw), &s));

    blep_settings_t invalid = { .flags = 1, .track_color = BLEP_TRACK_COLOR_MAX + 1, .update_interval_s = 60 };
    blep_settings_encode(raw, &invalid);
    TEST_ASSERT_EQUAL_UINT8(0, raw[1]);
}

void test_settings_interval_is_limited_to_30_to_600_seconds()
{
    blep_settings_t s;
    uint8_t raw[BLEP_SETTINGS_SIZE] = { 1, 0, 0, 0 };
    put_le(raw + 2, 29, 2);
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_settings_decode(raw, sizeof(raw), &s));
    put_le(raw + 2, 30, 2);
    TEST_ASSERT_EQUAL(BLEP_OK, blep_settings_decode(raw, sizeof(raw), &s));
    put_le(raw + 2, 600, 2);
    TEST_ASSERT_EQUAL(BLEP_OK, blep_settings_decode(raw, sizeof(raw), &s));
    put_le(raw + 2, 601, 2);
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_settings_decode(raw, sizeof(raw), &s));
}

void test_settings_rejects_reserved_bits_and_length()
{
    blep_settings_t s;
    uint8_t raw[BLEP_SETTINGS_SIZE] = { 4, 0, 60, 0 };
    TEST_ASSERT_EQUAL(BLEP_ERR_FORMAT, blep_settings_decode(raw, sizeof(raw), &s));
    raw[0] = 1;
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_settings_decode(raw, 3, &s));
}

void test_clamp_update_interval()
{
    TEST_ASSERT_EQUAL_UINT16(30, blep_clamp_update_interval(0));
    TEST_ASSERT_EQUAL_UINT16(30, blep_clamp_update_interval(29));
    TEST_ASSERT_EQUAL_UINT16(45, blep_clamp_update_interval(45));
    TEST_ASSERT_EQUAL_UINT16(600, blep_clamp_update_interval(600));
    TEST_ASSERT_EQUAL_UINT16(600, blep_clamp_update_interval(100000));
}

void test_device_control_only_knows_forget()
{
    uint8_t cmd = 0;
    uint8_t v = BLEP_DEVICE_FORGET_PHONE;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_device_control_decode(&v, 1, &cmd));
    TEST_ASSERT_EQUAL_UINT8(BLEP_DEVICE_FORGET_PHONE, cmd);
    v = 2;
    TEST_ASSERT_EQUAL(BLEP_ERR_FORMAT, blep_device_control_decode(&v, 1, &cmd));
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_device_control_decode(&v, 0, &cmd));
}

void test_recording_commands()
{
    uint8_t cmd = 0;
    uint32_t id = 99;
    uint8_t start = BLEP_RECORD_CMD_START;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_recording_command_decode(&start, 1, &cmd, &id));
    TEST_ASSERT_EQUAL_UINT8(BLEP_RECORD_CMD_START, cmd);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    uint8_t stop = BLEP_RECORD_CMD_STOP;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_recording_command_decode(&stop, 1, &cmd, &id));
    TEST_ASSERT_EQUAL_UINT8(BLEP_RECORD_CMD_STOP, cmd);

    uint8_t del[] = { BLEP_RECORD_CMD_DELETE, 0x78, 0x56, 0x34, 0x12 };
    TEST_ASSERT_EQUAL(BLEP_OK, blep_recording_command_decode(del, sizeof(del), &cmd, &id));
    TEST_ASSERT_EQUAL_UINT8(BLEP_RECORD_CMD_DELETE, cmd);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, id);

    uint8_t del_zero[] = { BLEP_RECORD_CMD_DELETE, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL(BLEP_ERR_RANGE, blep_recording_command_decode(del_zero, sizeof(del_zero), &cmd, &id));
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_recording_command_decode(del, 4, &cmd, &id));
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_recording_command_decode(del, 2, &cmd, &id));
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_recording_command_decode(&start, 0, &cmd, &id));
    uint8_t unknown = 4;
    TEST_ASSERT_EQUAL(BLEP_ERR_FORMAT, blep_recording_command_decode(&unknown, 1, &cmd, &id));
}

void test_recording_status_layout()
{
    blep_recording_status_t status = { .recording = 1, .id = 0x6704a1b0, .size = 0x00010203, .last_point = 0x6704a1ff };
    uint8_t raw[BLEP_RECORDING_STATUS_SIZE];
    memset(raw, 0xaa, sizeof(raw));
    blep_recording_status_encode(raw, &status);
    const uint8_t expected[] = { 1, 0, 0, 0, 0xb0, 0xa1, 0x04, 0x67, 0x03, 0x02, 0x01, 0x00, 0xff, 0xa1, 0x04, 0x67 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, raw, sizeof(expected));
}

void test_recordings_list_layout()
{
    uint16_t first = 0;
    uint8_t select[] = { 0x1c, 0x00 };
    TEST_ASSERT_EQUAL(BLEP_OK, blep_recordings_select_decode(select, sizeof(select), &first));
    TEST_ASSERT_EQUAL_UINT16(28, first);
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_recordings_select_decode(select, 1, &first));

    blep_recording_entry_t entries[] = { { 0x11223344, 1000 }, { 0x55667788, 70000 } };
    uint8_t out[BLEP_RECORDINGS_HEADER_SIZE + BLEP_RECORDINGS_MAX_ENTRIES * BLEP_RECORDINGS_ENTRY_SIZE];
    size_t len = blep_recordings_encode(out, sizeof(out), 30, 28, entries, 2);
    TEST_ASSERT_EQUAL(4 + 2 * 8, len);
    const uint8_t expected[] = { 30, 0, 28, 0, 0x44, 0x33, 0x22, 0x11, 0xe8, 0x03, 0, 0, 0x88, 0x77, 0x66, 0x55, 0x70, 0x11, 0x01, 0 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, len);

    TEST_ASSERT_EQUAL(4, blep_recordings_encode(out, sizeof(out), 0, 0, NULL, 0));
    TEST_ASSERT_EQUAL(0, blep_recordings_encode(out, 10, 2, 0, entries, 2));
    TEST_ASSERT_TRUE(sizeof(out) <= 244); // fits into one read with an MTU of 247
}

void test_recording_file_names()
{
    char name[BLEP_RECORDING_NAME_LEN + 1];
    blep_recording_file_name(name, 0x6704a1b0);
    TEST_ASSERT_EQUAL_STRING("6704A1B0.GPX", name);

    uint32_t id = 0;
    TEST_ASSERT_TRUE(blep_recording_id_from_name("6704A1B0.GPX", &id));
    TEST_ASSERT_EQUAL_HEX32(0x6704a1b0, id);
    TEST_ASSERT_TRUE(blep_recording_id_from_name("6704a1b0.gpx", &id));
    TEST_ASSERT_EQUAL_HEX32(0x6704a1b0, id);

    TEST_ASSERT_FALSE(blep_recording_id_from_name("LOG.GPX", &id));
    TEST_ASSERT_FALSE(blep_recording_id_from_name("6704A1B0.TMP", &id));
    TEST_ASSERT_FALSE(blep_recording_id_from_name("6704A1BG.GPX", &id));
    TEST_ASSERT_FALSE(blep_recording_id_from_name("00000000.GPX", &id));
    TEST_ASSERT_FALSE(blep_recording_id_from_name("6704A1B0X.GPX", &id));
    TEST_ASSERT_FALSE(blep_recording_id_from_name(NULL, &id));
}

void test_ota_start_carries_the_size()
{
    uint8_t raw[BLEP_OTA_START_SIZE] = { BLEP_OTA_CMD_START };
    put_le(raw + 1, 1500000, 4);
    uint8_t cmd = 0;
    uint32_t size = 0;
    TEST_ASSERT_EQUAL(BLEP_OK, blep_ota_command_decode(raw, sizeof(raw), &cmd, &size));
    TEST_ASSERT_EQUAL_UINT8(BLEP_OTA_CMD_START, cmd);
    TEST_ASSERT_EQUAL_UINT32(1500000, size);
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_ota_command_decode(raw, 4, &cmd, &size));
}

void test_ota_simple_commands()
{
    uint8_t cmd = 0;
    uint32_t size = 77;
    uint8_t ids[] = { BLEP_OTA_CMD_ABORT, BLEP_OTA_CMD_FINISH, BLEP_OTA_CMD_RESTART };
    for (size_t i = 0; i < sizeof(ids); i++) {
        TEST_ASSERT_EQUAL(BLEP_OK, blep_ota_command_decode(&ids[i], 1, &cmd, &size));
        TEST_ASSERT_EQUAL_UINT8(ids[i], cmd);
        TEST_ASSERT_EQUAL_UINT32(0, size);
    }
    uint8_t two[2] = { BLEP_OTA_CMD_FINISH, 0 };
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_ota_command_decode(two, 2, &cmd, &size));
    uint8_t unknown = 0x7f;
    TEST_ASSERT_EQUAL(BLEP_ERR_FORMAT, blep_ota_command_decode(&unknown, 1, &cmd, &size));
    TEST_ASSERT_EQUAL(BLEP_ERR_LENGTH, blep_ota_command_decode(&unknown, 0, &cmd, &size));
}

void test_ota_status_layout()
{
    uint8_t raw[BLEP_OTA_STATUS_SIZE];
    blep_ota_status_encode(raw, BLEP_OTA_ERROR, BLEP_OTA_ERR_MTU, 0x01020304);
    TEST_ASSERT_EQUAL_UINT8(BLEP_OTA_ERROR, raw[0]);
    TEST_ASSERT_EQUAL_UINT8(BLEP_OTA_ERR_MTU, raw[1]);
    TEST_ASSERT_EQUAL_UINT8(0x04, raw[2]);
    TEST_ASSERT_EQUAL_UINT8(0x01, raw[5]);
}

void test_ota_estimate_and_progress_screen_threshold()
{
    TEST_ASSERT_EQUAL_UINT32(0, blep_ota_estimate_seconds(0));
    TEST_ASSERT_EQUAL_UINT32(1, blep_ota_estimate_seconds(1));
    TEST_ASSERT_EQUAL_UINT32(35, blep_ota_estimate_seconds(1400000));
    // 15 s at 40 kB/s are 600000 bytes
    TEST_ASSERT_FALSE(blep_ota_needs_progress_screen(600000));
    TEST_ASSERT_TRUE(blep_ota_needs_progress_screen(600001));
    TEST_ASSERT_TRUE(blep_ota_needs_progress_screen(6553600)); // the whole partition
}

void test_pmtk_sentences_have_a_valid_checksum()
{
    blep_utc_t utc = { 2026, 7, 15, 13, 45, 59 };
    char out[BLEP_PMTK_MAX_LEN];
    size_t len = blep_pmtk_time(out, sizeof(out), &utc);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING_LEN("$PMTK740,2026,07,15,13,45,59*", out, 29);
    TEST_ASSERT_EQUAL_CHAR('\r', out[len - 2]);
    TEST_ASSERT_EQUAL_CHAR('\n', out[len - 1]);

    uint8_t checksum = 0;
    for (size_t i = 1; out[i] != '*'; i++)
        checksum ^= (uint8_t)out[i];
    char expected[3];
    snprintf(expected, sizeof(expected), "%02X", checksum);
    TEST_ASSERT_EQUAL_STRING_LEN(expected, out + 29, 2);
}

void test_pmtk_position_sentence()
{
    blep_utc_t utc = { 2026, 1, 2, 3, 4, 5 };
    char out[BLEP_PMTK_MAX_LEN];
    size_t len = blep_pmtk_position(out, sizeof(out), 49.626846, -8.581875, 312, &utc);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING_LEN("$PMTK741,49.626846,-8.581875,312,2026,01,02,03,04,05*", out, 53);
    TEST_ASSERT_EQUAL(len, strlen(out));
}

void test_pmtk_buffer_too_small()
{
    blep_utc_t utc = { 2026, 1, 2, 3, 4, 5 };
    char out[20];
    TEST_ASSERT_EQUAL(0, blep_pmtk_time(out, sizeof(out), &utc));
    TEST_ASSERT_EQUAL(0, blep_pmtk_position(out, sizeof(out), 1.0, 2.0, 3, &utc));
    TEST_ASSERT_EQUAL(0, blep_pmtk_time(NULL, 0, &utc));
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_uuid_bytes_are_in_reverse_order);
    RUN_TEST(test_info_has_version_flags_battery_and_firmware);
    RUN_TEST(test_time_round_trip);
    RUN_TEST(test_time_is_little_endian);
    RUN_TEST(test_time_rejects_wrong_length_and_range);
    RUN_TEST(test_utc_and_epoch_match_known_dates);
    RUN_TEST(test_epoch_to_utc_inverts_utc_to_epoch);
    RUN_TEST(test_position_in_decodes_all_fields);
    RUN_TEST(test_position_in_rejects_bad_values);
    RUN_TEST(test_position_out_layout);
    RUN_TEST(test_degree_conversion);
    RUN_TEST(test_wifi_control_accepts_only_zero_and_one);
    RUN_TEST(test_wifi_status_has_ssid_but_no_password);
    RUN_TEST(test_settings_round_trip);
    RUN_TEST(test_settings_interval_is_limited_to_30_to_600_seconds);
    RUN_TEST(test_settings_track_color);
    RUN_TEST(test_settings_rejects_reserved_bits_and_length);
    RUN_TEST(test_clamp_update_interval);
    RUN_TEST(test_device_control_only_knows_forget);
    RUN_TEST(test_recording_commands);
    RUN_TEST(test_recording_status_layout);
    RUN_TEST(test_recordings_list_layout);
    RUN_TEST(test_recording_file_names);
    RUN_TEST(test_ota_start_carries_the_size);
    RUN_TEST(test_ota_simple_commands);
    RUN_TEST(test_ota_status_layout);
    RUN_TEST(test_ota_estimate_and_progress_screen_threshold);
    RUN_TEST(test_pmtk_sentences_have_a_valid_checksum);
    RUN_TEST(test_pmtk_position_sentence);
    RUN_TEST(test_pmtk_buffer_too_small);
    UNITY_END();
}
