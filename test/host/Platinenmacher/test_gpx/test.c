#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "parser/gpx.h"

static uint16_t num_wp;
uint32_t modify_waypoint(waypoint_t* wp)
{
    num_wp++;
    TEST_ASSERT_NOT_NULL(wp);
    wp->active = 1;
    return num_wp;
}

void setUp()
{
    num_wp = 0;
}

void test_gpx_parsing()
{
    FILE* file;

    file = fopen("test/host/Platinenmacher/test_gpx/test.gpx", "rb");
    assert(file != NULL);

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET); /* same as rewind(f); */

    char* gpx_data = malloc(fsize + 1);
    fread(gpx_data, fsize, 1, file);
    fclose(file);
    gpx_data[fsize] = 0;

    gpx_t* gpx = gpx_parser(gpx_data, modify_waypoint);
    waypoint_t *start = gpx->waypoints;
    free(gpx_data);

    TEST_ASSERT_EQUAL_STRING_LEN("Teststrecke", gpx->track_name, 12);
    TEST_ASSERT_EQUAL_UINT16(17, num_wp);
    TEST_ASSERT_EQUAL_FLOAT(49.622274, start->lat);
    TEST_ASSERT_EQUAL_FLOAT(8.587822, start->lon);
    TEST_ASSERT_EQUAL_UINT(1, start->active);
}

void test_gpx_parsing_incomplete()
{
    FILE* file;

    file = fopen("test/host/Platinenmacher/test_gpx/test_incomplete.gpx", "rb");
    assert(file != NULL);

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET); /* same as rewind(f); */

    char* gpx_data = malloc(fsize + 1);
    fread(gpx_data, fsize, 1, file);
    fclose(file);
    gpx_data[fsize] = 0;

    gpx_t* gpx = gpx_parser(gpx_data, modify_waypoint);
    waypoint_t *start = gpx->waypoints;
    free(gpx_data);

    TEST_ASSERT_EQUAL_STRING_LEN("Teststrecke", gpx->track_name, 12);
    TEST_ASSERT_EQUAL(num_wp, gpx->waypoints_num);
    TEST_ASSERT_EQUAL_UINT16(2, num_wp);
    TEST_ASSERT_EQUAL_FLOAT(49.622274, start->lat);
    TEST_ASSERT_EQUAL_FLOAT(8.587822, start->lon);
    TEST_ASSERT_EQUAL_UINT(1, start->active);
}

void test_gpx_parsing_error()
{
    FILE* file;

    file = fopen("test/host/Platinenmacher/test_gpx/test_error.gpx", "rb");
    assert(file != NULL);

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET); /* same as rewind(f); */

    char* gpx_data = malloc(fsize + 1);
    fread(gpx_data, fsize, 1, file);
    fclose(file);
    gpx_data[fsize] = 0;

    gpx_t* gpx = gpx_parser(gpx_data, modify_waypoint);
    waypoint_t *start = gpx->waypoints;
    free(gpx_data);

    // the name is parsed before the error is found
    TEST_ASSERT_EQUAL_STRING("Teststrecke", gpx->track_name);
    TEST_ASSERT_EQUAL_UINT16(0, gpx->waypoints_num);
}

void test_gpx_long_tokens()
{
    // tokens longer than the internal buffer must be truncated, not overflow
    char name[1024];
    memset(name, 'x', sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    char* gpx_data = malloc(4096);
    snprintf(gpx_data, 4096,
        "<?xml version=\"1.0\"?>\n<gpx><!-- %s --><trk><name>%s</name><trkseg>"
        "<trkpt lat=\"49.1\" lon=\"8.1\"><ele>100</ele></trkpt>"
        "</trkseg></trk></gpx>",
        name, name);

    gpx_t* gpx = gpx_parser(gpx_data, modify_waypoint);
    free(gpx_data);

    TEST_ASSERT_NOT_NULL(gpx->track_name);
    TEST_ASSERT_EQUAL_UINT(254, strlen(gpx->track_name));
    TEST_ASSERT_EQUAL_UINT16(1, gpx->waypoints_num);
    gpx_free(gpx);
}

void test_gpx_parser_resets_state()
{
    char* data = "<gpx><trk><trkseg><trkpt lat=\"1\" lon=\"2\"></trkpt></trkseg></trk></gpx>";
    gpx_t* first = gpx_parser(data, modify_waypoint);
    gpx_t* second = gpx_parser(data, modify_waypoint);
    TEST_ASSERT_NOT_EQUAL(first->waypoints, second->waypoints);
    TEST_ASSERT_EQUAL_UINT16(1, second->waypoints_num);
    free(first->waypoints);
    free(second->waypoints);
    gpx_free(first);
    gpx_free(second);
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_gpx_long_tokens);
    RUN_TEST(test_gpx_parser_resets_state);
    RUN_TEST(test_gpx_parsing);
    RUN_TEST(test_gpx_parsing_incomplete);
    RUN_TEST(test_gpx_parsing_error);
    UNITY_END();
}