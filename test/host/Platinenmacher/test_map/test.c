#include <unity.h>

#include "../mock/mock_display.h"
#include "../mock/mock_renderhooks.h"

#include "gui/map.h"

map_t* map;

void setUp()
{
    map = map_create(0, 0, 2, 3, 256, 0);
}

void test_create_map()
{
    TEST_ASSERT_EQUAL_UINT8(2, map->width);
    TEST_ASSERT_EQUAL_UINT8(3, map->height);
    TEST_ASSERT_EQUAL_UINT16(512, map->box.width);
    TEST_ASSERT_EQUAL_UINT16(768, map->box.height);
    TEST_ASSERT_NOT_NULL(map->tiles);
    map_t* empty_map = map_create(0, 0, 0, 0, 256, 0);
    TEST_ASSERT_NULL(empty_map);
}

void test_create_map_at_negative_position()
{
    map_t* _map = map_create(-10, -10, 2, 2, 256, 0);
    TEST_ASSERT_EQUAL_INT16(-10, _map->box.top);
    TEST_ASSERT_EQUAL_INT16(-10, _map->box.left);
    TEST_ASSERT_EQUAL_INT16(512, _map->box.width);
    TEST_ASSERT_EQUAL_INT16(512, _map->box.height);
    TEST_ASSERT_EQUAL_INT16(-10, _map->tiles[0]->image->box.left);
    TEST_ASSERT_EQUAL_INT16(-10, _map->tiles[1]->image->box.left);
    TEST_ASSERT_EQUAL_INT16(246, _map->tiles[2]->image->box.left);
    TEST_ASSERT_EQUAL_INT16(246, _map->tiles[3]->image->box.left);
    TEST_ASSERT_EQUAL_INT16(-10, _map->tiles[0]->image->box.top);
    TEST_ASSERT_EQUAL_INT16(246, _map->tiles[1]->image->box.top);
    TEST_ASSERT_EQUAL_INT16(-10, _map->tiles[2]->image->box.top);
    TEST_ASSERT_EQUAL_INT16(246, _map->tiles[3]->image->box.top);
}

void test_zoom_level()
{
    TEST_ASSERT_EQUAL(PM_OK, map_update_zoom_level(map, 13));
    TEST_ASSERT_EQUAL(13, map_get_zoom_level(map));
}

void test_map_get_tile()
{
    TEST_ASSERT_NOT_NULL_MESSAGE(map_get_tile(map, 0, 0), "origin tile is NULL");
    TEST_ASSERT_NULL_MESSAGE(map_get_tile(map, 100, 100), "tile oob is not NULL");
}

void test_position_update()
{
    uint8_t zoom = 13;
    map_position_t pos = { .longitude = 8.68575379, .latitude = 49.7258546 };
    TEST_ASSERT_EQUAL(PM_OK, map_update_zoom_level(map, zoom));
    TEST_ASSERT_EQUAL(PM_OK, map_update_position(map, &pos));
    for (int i = 0; i < map->tile_count; i++) {
        TEST_ASSERT_NOT_NULL(map->tiles[i]);
        TEST_ASSERT_NOT_EQUAL_UINT8(0, map->tiles[i]->x);
    }
    zoom = 16;
    pos.longitude = 8.68585379;
    TEST_ASSERT_EQUAL_MESSAGE(PM_OK, map_update_zoom_level(map, zoom), "update zoomlevel to 16");
    TEST_ASSERT_EQUAL_MESSAGE(PM_OK, map_update_position(map, &pos), "update_position to second point");
    for (int i = 0; i < map->tile_count; i++) {
        TEST_ASSERT_NOT_NULL(map->tiles[i]);
        TEST_ASSERT_NOT_EQUAL_UINT8(0, map->tiles[i]->x);
    }
}

void test_map_render_callbacks()
{
    TEST_ASSERT_NOT_NULL(map);
    display_t* dsp = display_init(DISPLAY_WIDTH, DISPLAY_HEIGHT, 8, DISPLAY_ROTATE_0);
    dsp->fb_size = DISPLAY_HEIGHT * DISPLAY_WIDTH;
    dsp->fb = malloc(dsp->fb_size);
    dsp->write_pixel = write_pixel;
    dsp->decompress = decompress;

    onBeforeRender_cnt = 0;
    onAfterRender_cnt = 0;
    map_tile_attach_onAfterRender_callback(map, onAfterRenderCounter);
    map_render(dsp, map);
    TEST_ASSERT_EQUAL_INT(map->tile_count, onAfterRender_cnt);
    map_tile_attach_onBeforeRender_callback(map, onBeforeRenderCounter);
    map_render(dsp, map);
    TEST_ASSERT_EQUAL_INT(map->tile_count, onBeforeRender_cnt);
}

void run_on_waypoint(waypoint_t* wp)
{
    wp->active = 1;
}

void test_waypoints()
{
    waypoint_t* wp = RTOS_Malloc(sizeof(waypoint_t));
    wp->lat = 49.5;
    wp->lon = 8.0;
    wp->ele = 123.45;
    wp->tile_x = 0;
    wp->tile_y = 0;
    wp->active = 0;
    map_position_t pos = {
        .latitude = 49.5,
        .longitude = 8.00
    };
    map->tile_zoom = 16;
    map_update_position(map, &pos);
    TEST_ASSERT_EQUAL(PM_OK, map_calculate_waypoint(map, wp));
    TEST_ASSERT_EQUAL_UINT16(34224, wp->tile_x);
    TEST_ASSERT_EQUAL_UINT16(22367, wp->tile_y);
    TEST_ASSERT_EQUAL_UINT16(347, wp->pos_x);
    TEST_ASSERT_EQUAL_UINT16(17, wp->pos_y);
    TEST_ASSERT_EQUAL_UINT(1, wp->active);

    map_set_first_waypoint(wp);
    map_add_waypoint(wp);
    TEST_ASSERT_EQUAL_UINT(0, wp->num);
    waypoint_t* new_wp = RTOS_Malloc(sizeof(waypoint_t));
    map_add_waypoint(new_wp);
    TEST_ASSERT_EQUAL_UINT(1, new_wp->num);
    TEST_ASSERT_EQUAL(new_wp, wp->next);

    TEST_ASSERT_EQUAL_UINT(0, new_wp->active);

    map_run_on_waypoints(run_on_waypoint);

    TEST_ASSERT_EQUAL_UINT(1, wp->active);
    TEST_ASSERT_EQUAL_UINT(1, new_wp->active);

    map_update_waypoint_path(map);

    // since map posiiton is on wp we expect it to be active.
    TEST_ASSERT_EQUAL_UINT(1, wp->active);
    TEST_ASSERT_EQUAL_UINT(0, new_wp->active);

    map_free_waypoints();
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_map);
    RUN_TEST(test_create_map_at_negative_position);
    RUN_TEST(test_zoom_level);
    RUN_TEST(test_map_get_tile);
    RUN_TEST(test_position_update);
    RUN_TEST(test_map_render_callbacks);
    RUN_TEST(test_waypoints);
    UNITY_END();
}