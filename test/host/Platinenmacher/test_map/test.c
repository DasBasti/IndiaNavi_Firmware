#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "../mock/mock_display.h"
#include "../mock/mock_renderhooks.h"

#include "gui/map.h"

map_t* map;

void setUp()
{
    map = map_create(0, 0, 3, 3, 256, 0);
}

void test_create_map()
{
    TEST_ASSERT_EQUAL_UINT8(3, map->width);
    TEST_ASSERT_EQUAL_UINT8(3, map->height);
    TEST_ASSERT_EQUAL_UINT16(768, map->box.width);
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
    TEST_ASSERT_NOT_NULL_MESSAGE(map_get_tile(map, 2, 2), "last tile is NULL");
    TEST_ASSERT_NULL_MESSAGE(map_get_tile(map, 3, 0), "tile x == width is not NULL");
    TEST_ASSERT_NULL_MESSAGE(map_get_tile(map, 0, 3), "tile y == height is not NULL");
}

void test_map_free()
{
    map_t* _map = map_create(0, 0, 2, 2, 256, 0);
    _map->tiles[0]->image->data = RTOS_Malloc(16);
    map_free(_map);
    map_free(NULL);
}

void test_waypoints_restart_after_free()
{
    waypoint_t* a = RTOS_Malloc(sizeof(waypoint_t));
    waypoint_t* b = RTOS_Malloc(sizeof(waypoint_t));
    map_add_waypoint(a);
    map_add_waypoint(b);
    TEST_ASSERT_EQUAL_UINT(1, b->num);
    map_free_waypoints();

    // a new list has to start at 0 and must not touch freed waypoints
    waypoint_t* c = RTOS_Malloc(sizeof(waypoint_t));
    map_add_waypoint(c);
    TEST_ASSERT_EQUAL_UINT(0, c->num);
    TEST_ASSERT_NULL(c->next);
    map_free_waypoints();
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
    map_add_waypoint(wp);
    TEST_ASSERT_EQUAL(PM_OK, map_calculate_waypoint(map, wp));
    TEST_ASSERT_EQUAL_UINT16(34224, wp->tile_x);
    TEST_ASSERT_EQUAL_UINT16(22367, wp->tile_y);
    TEST_ASSERT_EQUAL_UINT16(347, wp->pos_x);
    TEST_ASSERT_EQUAL_UINT16(273, wp->pos_y);
    TEST_ASSERT_EQUAL_UINT(1, wp->active);

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

/* a straight track along x, one waypoint every step pixels */
#define LINE_POINTS 30
static waypoint_t line[LINE_POINTS];

static void make_line(int16_t step)
{
    memset(line, 0, sizeof(line));
    for (int i = 0; i < LINE_POINTS; i++) {
        line[i].pos_x = i * step;
        line[i].pos_y = 100;
        line[i].active = 1;
        line[i].next = i + 1 < LINE_POINTS ? &line[i + 1] : NULL;
    }
}

void test_arrows_are_placed_along_the_track()
{
    make_line(5);
    waypoint_place_arrows(line, 80, 12);
    for (int i = 0; i < LINE_POINTS; i++) {
        if (i == 7) {
            // half the spacing after the start, the direction is taken over 12 pixels (3 segments)
            TEST_ASSERT_EQUAL_PTR(&line[10], line[i].arrow_to);
        } else if (i == 23) {
            // then one every 80 pixels
            TEST_ASSERT_EQUAL_PTR(&line[26], line[i].arrow_to);
        } else {
            TEST_ASSERT_NULL_MESSAGE(line[i].arrow_to, "no arrow expected");
        }
    }
}

void test_arrows_only_on_the_screen()
{
    make_line(5);
    line[3].arrow_to = &line[4]; // from an earlier placement
    for (int i = 0; i < LINE_POINTS; i++)
        line[i].active = i >= 20; // the start of the track is off the screen
    waypoint_place_arrows(line, 80, 12);
    TEST_ASSERT_NULL(line[3].arrow_to);
    for (int i = 0; i < LINE_POINTS; i++)
        TEST_ASSERT_TRUE(line[i].arrow_to == NULL || i >= 20);
    // half the spacing after the first visible point, the look-ahead stops at the end of the track
    TEST_ASSERT_EQUAL_PTR(&line[29], line[27].arrow_to);
    waypoint_place_arrows(NULL, 80, 12); // an empty track
}

void test_arrow_is_drawn_in_the_direction_of_the_track()
{
    display_t* dsp = display_init(DISPLAY_WIDTH, DISPLAY_HEIGHT, 8, DISPLAY_ROTATE_0);
    dsp->fb_size = DISPLAY_HEIGHT * DISPLAY_WIDTH;
    dsp->fb = malloc(dsp->fb_size);
    dsp->write_pixel = write_pixel;
    dsp->decompress = decompress;
    memset(dsp->fb, 0xaa, dsp->fb_size);

    waypoint_t from = { .pos_x = 10, .pos_y = 10, .active = 1, .color = WHITE };
    waypoint_t to = { .pos_x = 30, .pos_y = 10, .active = 1 };
    from.arrow_to = &to;
    TEST_ASSERT_EQUAL(ABORT, waypoint_render_arrow(dsp, &from));

#define PIXEL(x, y) dsp->fb[(y) * DISPLAY_WIDTH + (x)]
    // tip ahead of the waypoint, the base 10 pixels behind with a border
    TEST_ASSERT_EQUAL_UINT8(BLACK, PIXEL(15, 10));
    TEST_ASSERT_EQUAL_UINT8(BLACK, PIXEL(5, 5));
    TEST_ASSERT_EQUAL_UINT8(BLACK, PIXEL(5, 15));
    // filled with the color of the track
    TEST_ASSERT_EQUAL_UINT8(WHITE, PIXEL(10, 10));
    TEST_ASSERT_EQUAL_UINT8(WHITE, PIXEL(8, 8));
    // nothing beyond the tip or behind the base
    TEST_ASSERT_EQUAL_UINT8(0xaa, PIXEL(17, 10));
    TEST_ASSERT_EQUAL_UINT8(0xaa, PIXEL(3, 10));
    // the sides are narrow at the tip
    TEST_ASSERT_EQUAL_UINT8(0xaa, PIXEL(14, 5));

    // a black track gets a white border
    memset(dsp->fb, 0xaa, dsp->fb_size);
    from.color = BLACK;
    to.pos_x = -10; // and the arrow points the other way
    waypoint_render_arrow(dsp, &from);
    TEST_ASSERT_EQUAL_UINT8(WHITE, PIXEL(5, 10));
    TEST_ASSERT_EQUAL_UINT8(WHITE, PIXEL(15, 5));
    TEST_ASSERT_EQUAL_UINT8(0xaa, PIXEL(17, 10));
#undef PIXEL

    // no arrow without a direction
    memset(dsp->fb, 0xaa, dsp->fb_size);
    from.arrow_to = NULL;
    waypoint_render_arrow(dsp, &from);
    for (uint32_t i = 0; i < dsp->fb_size; i++)
        TEST_ASSERT_EQUAL_UINT8(0xaa, dsp->fb[i]);
    free(dsp->fb);
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
    RUN_TEST(test_map_free);
    RUN_TEST(test_waypoints_restart_after_free);
    RUN_TEST(test_arrows_are_placed_along_the_track);
    RUN_TEST(test_arrows_only_on_the_screen);
    RUN_TEST(test_arrow_is_drawn_in_the_direction_of_the_track);
    UNITY_END();
}