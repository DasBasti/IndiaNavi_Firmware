#include <unity.h>

#include "../mock/mock_display.h"
#include "../mock/mock_renderhooks.h"

#include "gui/map.h"

map_t* map;

void setUp()
{
    map = map_create(0, 0, 2, 3, 256);
}

void test_create_map()
{
    TEST_ASSERT_EQUAL_UINT8(2, map->width);
    TEST_ASSERT_EQUAL_UINT8(3, map->height);
    TEST_ASSERT_EQUAL_UINT16(512, map->box.width);
    TEST_ASSERT_EQUAL_UINT16(768, map->box.height);
    TEST_ASSERT_NOT_NULL(map->tiles);
    map_t* empty_map = map_create(0, 0, 0, 0, 256);
    TEST_ASSERT_NULL(empty_map);
}

void test_create_map_at_negative_position()
{
    map_t* _map = map_create(-10, -10, 2, 2, 256);
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
    TEST_ASSERT_EQUAL(PM_OK, map_update_position(map, pos));
    for (int i = 0; i < map->tile_count; i++) {
        TEST_ASSERT_NOT_NULL(map->tiles[i]);
        TEST_ASSERT_NOT_EQUAL_UINT8(0, map->tiles[i]->x);
    }
    zoom = 16;
    pos.longitude = 8.68585379;
    TEST_ASSERT_EQUAL_MESSAGE(PM_OK, map_update_zoom_level(map, zoom), "update zoomlevel to 16");
    TEST_ASSERT_EQUAL_MESSAGE(PM_OK, map_update_position(map, pos), "update_position to second point");
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

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_map);
    RUN_TEST(test_create_map_at_negative_position);
    RUN_TEST(test_zoom_level);
    RUN_TEST(test_map_get_tile);
    RUN_TEST(test_position_update);
    RUN_TEST(test_map_render_callbacks);
    UNITY_END();
}