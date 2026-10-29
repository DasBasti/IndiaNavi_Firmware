#include <math.h>
#include <unity.h>

#include "map_catalog.h"

static map_catalog_t catalog;

/* the Unity of the native tests is built without double support (UNITY_INCLUDE_DOUBLE) */
#define ASSERT_NEAR(delta, expected, actual) TEST_ASSERT_TRUE(fabs((double)(expected) - (double)(actual)) <= (delta))

/* regions of about the size of the official maps of German states */
static const map_bounds_t RHEINLAND_PFALZ = { 48.96, 6.11, 50.95, 8.51 };
static const map_bounds_t SAARLAND = { 49.11, 6.35, 49.64, 7.41 };
static const map_bounds_t BADEN_WUERTTEMBERG = { 47.53, 7.51, 49.80, 10.50 };
static const map_bounds_t GERMANY = { 47.27, 5.86, 55.06, 15.04 };

void setUp(void)
{
    map_catalog_clear(&catalog);
}

void tearDown(void)
{
}

void test_names_of_maps(void)
{
    TEST_ASSERT_TRUE(map_catalog_is_map_name("MAP.MAP"));
    TEST_ASSERT_TRUE(map_catalog_is_map_name("rheinl01.map"));
    TEST_ASSERT_FALSE(map_catalog_is_map_name("MAP.TMP"));
    TEST_ASSERT_FALSE(map_catalog_is_map_name(".MAP"));
    TEST_ASSERT_FALSE(map_catalog_is_map_name("14"));
    TEST_ASSERT_FALSE(map_catalog_is_map_name("TOOLONGNAME.MAP"));
}

void test_names_maps_can_be_uploaded_with(void)
{
    TEST_ASSERT_TRUE(map_catalog_is_upload_name("rheinl01.map"));
    TEST_ASSERT_TRUE(map_catalog_is_upload_name("DE_BW-2.MAP"));
    TEST_ASSERT_FALSE(map_catalog_is_upload_name("toolongna.map"));
    TEST_ASSERT_FALSE(map_catalog_is_upload_name("a.b.map"));
    TEST_ASSERT_FALSE(map_catalog_is_upload_name("../x.map"));
    TEST_ASSERT_FALSE(map_catalog_is_upload_name("x y.map"));
    TEST_ASSERT_FALSE(map_catalog_is_upload_name("x.raw"));
}

void test_size_and_date_from_the_start_of_a_map(void)
{
    uint8_t start[MAP_CATALOG_HEADER_START] = "mapsforge binary OSM";
    // file size 301234567 at byte 28, creation date 1759550000000 at byte 36, big endian
    const uint64_t size = 301234567, date = 1759550000000ULL;
    for (int i = 0; i < 8; i++) {
        start[28 + i] = size >> (56 - 8 * i);
        start[36 + i] = date >> (56 - 8 * i);
    }
    uint64_t file_size, created;
    TEST_ASSERT_TRUE(map_catalog_parse_header_start(start, sizeof(start), &file_size, &created));
    TEST_ASSERT_TRUE(file_size == size && created == date);
    TEST_ASSERT_FALSE(map_catalog_parse_header_start(start, 43, &file_size, &created));
    start[0] = 'M';
    TEST_ASSERT_FALSE(map_catalog_parse_header_start(start, sizeof(start), &file_size, &created));
}

void test_the_catalog_is_full_at_its_maximum(void)
{
    for (int i = 0; i < MAP_CATALOG_MAX; i++)
        TEST_ASSERT_TRUE(map_catalog_add(&catalog, "A.MAP", 1, i, SAARLAND));
    TEST_ASSERT_FALSE(map_catalog_add(&catalog, "B.MAP", 1, 0, SAARLAND));
    TEST_ASSERT_EQUAL_UINT8(MAP_CATALOG_MAX, catalog.count);
}

void test_a_newer_map_of_the_same_region_tops_the_old_one(void)
{
    map_catalog_add(&catalog, "OLD.MAP", 1, 1000, RHEINLAND_PFALZ);
    map_bounds_t slightly_different = RHEINLAND_PFALZ;
    slightly_different.max_lat -= 0.05;
    map_catalog_add(&catalog, "NEW.MAP", 1, 2000, slightly_different);
    TEST_ASSERT_EQUAL_UINT8(1, map_catalog_resolve(&catalog));
    TEST_ASSERT_TRUE(catalog.maps[0].superseded);
    TEST_ASSERT_FALSE(catalog.maps[1].superseded);
    TEST_ASSERT_EQUAL_UINT8(1, map_catalog_used(&catalog));
}

void test_a_newer_country_tops_an_older_region_but_not_the_other_way(void)
{
    map_catalog_add(&catalog, "SAAR.MAP", 1, 1000, SAARLAND);
    map_catalog_add(&catalog, "DE.MAP", 1, 2000, GERMANY);
    map_catalog_resolve(&catalog);
    TEST_ASSERT_TRUE(catalog.maps[0].superseded);

    map_catalog_clear(&catalog);
    map_catalog_add(&catalog, "SAAR.MAP", 1, 3000, SAARLAND);
    map_catalog_add(&catalog, "DE.MAP", 1, 2000, GERMANY);
    TEST_ASSERT_EQUAL_UINT8(2, map_catalog_resolve(&catalog));
}

void test_neighbours_and_maps_of_the_same_age_are_kept(void)
{
    map_catalog_add(&catalog, "RLP.MAP", 1, 1000, RHEINLAND_PFALZ);
    map_catalog_add(&catalog, "BW.MAP", 1, 2000, BADEN_WUERTTEMBERG);
    map_catalog_add(&catalog, "RLP2.MAP", 1, 1000, RHEINLAND_PFALZ);
    TEST_ASSERT_EQUAL_UINT8(3, map_catalog_resolve(&catalog));
}

void test_the_screen_in_degrees(void)
{
    // the position in the middle of a 448 x 600 screen at zoom 16
    map_bounds_t b = map_catalog_screen_bounds(49.143, 7.765, 16, 448, 600, 224, 300);
    ASSERT_NEAR(1e-4, 49.143, (b.min_lat + b.max_lat) / 2);
    ASSERT_NEAR(1e-6, 7.765, (b.min_lon + b.max_lon) / 2);
    // 448 pixels of 360 / 2^24 degrees
    ASSERT_NEAR(1e-6, 448 * 360.0 / 16777216.0, b.max_lon - b.min_lon);
    TEST_ASSERT_TRUE(b.max_lat > b.min_lat);

    // the anchor moves the screen: at the left edge the screen lies east of the position
    map_bounds_t left = map_catalog_screen_bounds(49.143, 7.765, 16, 448, 600, 0, 300);
    ASSERT_NEAR(1e-9, 7.765, left.min_lon);
}

void test_the_maps_of_a_screen_smallest_first(void)
{
    map_catalog_add(&catalog, "DE.MAP", 1, 1000, GERMANY);
    map_catalog_add(&catalog, "RLP.MAP", 1, 2000, RHEINLAND_PFALZ);
    map_catalog_add(&catalog, "BW.MAP", 1, 2000, BADEN_WUERTTEMBERG);
    map_catalog_add(&catalog, "SAAR.MAP", 1, 2000, SAARLAND);
    map_catalog_resolve(&catalog);
    uint8_t selected[MAP_CATALOG_DRAWN];

    // in the Palatinate near the border of Saarland, Baden-Wuerttemberg is too far away
    map_bounds_t screen = map_catalog_screen_bounds(49.20, 7.40, 14, 448, 600, 224, 300);
    TEST_ASSERT_EQUAL_UINT8(3, map_catalog_select(&catalog, screen, selected, MAP_CATALOG_DRAWN));
    TEST_ASSERT_EQUAL_STRING("SAAR.MAP", catalog.maps[selected[0]].name);
    TEST_ASSERT_EQUAL_STRING("RLP.MAP", catalog.maps[selected[1]].name);
    TEST_ASSERT_EQUAL_STRING("DE.MAP", catalog.maps[selected[2]].name);

    // with room for two the biggest one is left out
    TEST_ASSERT_EQUAL_UINT8(2, map_catalog_select(&catalog, screen, selected, 2));
    TEST_ASSERT_EQUAL_STRING("SAAR.MAP", catalog.maps[selected[0]].name);
    TEST_ASSERT_EQUAL_STRING("RLP.MAP", catalog.maps[selected[1]].name);

    // outside of all maps
    screen = map_catalog_screen_bounds(40.0, -3.7, 16, 448, 600, 224, 300);
    TEST_ASSERT_EQUAL_UINT8(0, map_catalog_select(&catalog, screen, selected, MAP_CATALOG_DRAWN));
}

void test_superseded_maps_are_not_drawn(void)
{
    map_catalog_add(&catalog, "OLD.MAP", 1, 1000, SAARLAND);
    map_catalog_add(&catalog, "NEW.MAP", 1, 2000, SAARLAND);
    map_catalog_resolve(&catalog);
    uint8_t selected[MAP_CATALOG_DRAWN];
    map_bounds_t screen = map_catalog_screen_bounds(49.3, 7.0, 16, 448, 600, 224, 300);
    TEST_ASSERT_EQUAL_UINT8(1, map_catalog_select(&catalog, screen, selected, MAP_CATALOG_DRAWN));
    TEST_ASSERT_EQUAL_STRING("NEW.MAP", catalog.maps[selected[0]].name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_names_of_maps);
    RUN_TEST(test_names_maps_can_be_uploaded_with);
    RUN_TEST(test_size_and_date_from_the_start_of_a_map);
    RUN_TEST(test_the_catalog_is_full_at_its_maximum);
    RUN_TEST(test_a_newer_map_of_the_same_region_tops_the_old_one);
    RUN_TEST(test_a_newer_country_tops_an_older_region_but_not_the_other_way);
    RUN_TEST(test_neighbours_and_maps_of_the_same_age_are_kept);
    RUN_TEST(test_the_screen_in_degrees);
    RUN_TEST(test_the_maps_of_a_screen_smallest_first);
    RUN_TEST(test_superseded_maps_are_not_drawn);
    return UNITY_END();
}
