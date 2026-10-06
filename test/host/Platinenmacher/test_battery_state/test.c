#include <unity.h>

#include "battery_state.h"

void setUp() { }
void tearDown() { }

void test_full_battery_is_not_empty()
{
    battery_state_t s = { 0 };
    for (int i = 0; i < 10; i++)
        TEST_ASSERT_FALSE(battery_state_update(&s, 80, false));
    TEST_ASSERT_FALSE(battery_state_is_low(&s));
}

void test_empty_after_readings_in_a_row()
{
    battery_state_t s = { 0 };
    for (int i = 1; i < BATTERY_EMPTY_READINGS; i++) {
        TEST_ASSERT_FALSE(battery_state_update(&s, BATTERY_EMPTY_LEVEL, false));
        TEST_ASSERT_TRUE(battery_state_is_low(&s));
    }
    TEST_ASSERT_TRUE(battery_state_update(&s, 0, false));
    // and it stays empty
    TEST_ASSERT_TRUE(battery_state_update(&s, 0, false));
}

void test_a_good_reading_starts_again()
{
    battery_state_t s = { 0 };
    for (int i = 1; i < BATTERY_EMPTY_READINGS; i++)
        battery_state_update(&s, 0, false);
    // the voltage comes back after a refresh of the display
    TEST_ASSERT_FALSE(battery_state_update(&s, BATTERY_EMPTY_LEVEL + 1, false));
    TEST_ASSERT_FALSE(battery_state_is_low(&s));
    for (int i = 1; i < BATTERY_EMPTY_READINGS; i++)
        TEST_ASSERT_FALSE(battery_state_update(&s, 0, false));
    TEST_ASSERT_TRUE(battery_state_update(&s, 0, false));
}

void test_never_empty_while_charging()
{
    battery_state_t s = { 0 };
    for (int i = 1; i < BATTERY_EMPTY_READINGS; i++)
        battery_state_update(&s, 0, false);
    for (int i = 0; i < 10; i++)
        TEST_ASSERT_FALSE(battery_state_update(&s, 0, true));
    TEST_ASSERT_FALSE(battery_state_is_low(&s));
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_battery_is_not_empty);
    RUN_TEST(test_empty_after_readings_in_a_row);
    RUN_TEST(test_a_good_reading_starts_again);
    RUN_TEST(test_never_empty_while_charging);
    UNITY_END();
}
