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

#define BAT 2000
#define CHARGER (BAT + CHARGER_MARGIN + 100)

void test_charger_connected_after_readings_in_a_row()
{
    charger_state_t s = { 0 };
    for (int i = 1; i < CHARGER_CONFIRM_READINGS; i++) {
        TEST_ASSERT_EQUAL(CHARGER_NO_CHANGE, charger_state_update(&s, CHARGER, BAT));
        TEST_ASSERT_TRUE(charger_state_is_pending(&s));
    }
    TEST_ASSERT_EQUAL(CHARGER_CONNECTED, charger_state_update(&s, CHARGER, BAT));
    TEST_ASSERT_FALSE(charger_state_is_pending(&s));
    TEST_ASSERT_TRUE(s.charging);
    // reported once
    TEST_ASSERT_EQUAL(CHARGER_NO_CHANGE, charger_state_update(&s, CHARGER, BAT));
}

void test_charger_removed_after_readings_in_a_row()
{
    charger_state_t s = { .charging = true };
    for (int i = 1; i < CHARGER_CONFIRM_READINGS; i++)
        TEST_ASSERT_EQUAL(CHARGER_NO_CHANGE, charger_state_update(&s, BAT, BAT));
    TEST_ASSERT_EQUAL(CHARGER_REMOVED, charger_state_update(&s, BAT, BAT));
    TEST_ASSERT_FALSE(s.charging);
}

void test_charger_noise_does_not_change_state()
{
    charger_state_t s = { 0 };
    // readings that jump around the margin
    for (int i = 0; i < 20; i++) {
        int charger = i % CHARGER_CONFIRM_READINGS == 0 ? BAT : CHARGER;
        TEST_ASSERT_EQUAL(CHARGER_NO_CHANGE, charger_state_update(&s, charger, BAT));
    }
    TEST_ASSERT_FALSE(s.charging);
}

void test_charger_at_margin_keeps_state()
{
    charger_state_t s = { .charging = true };
    for (int i = 0; i < 10; i++)
        TEST_ASSERT_EQUAL(CHARGER_NO_CHANGE, charger_state_update(&s, BAT + CHARGER_MARGIN, BAT));
    TEST_ASSERT_TRUE(s.charging);
    TEST_ASSERT_FALSE(charger_state_is_pending(&s));
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_battery_is_not_empty);
    RUN_TEST(test_empty_after_readings_in_a_row);
    RUN_TEST(test_a_good_reading_starts_again);
    RUN_TEST(test_never_empty_while_charging);
    RUN_TEST(test_charger_connected_after_readings_in_a_row);
    RUN_TEST(test_charger_removed_after_readings_in_a_row);
    RUN_TEST(test_charger_noise_does_not_change_state);
    RUN_TEST(test_charger_at_margin_keeps_state);
    UNITY_END();
}
