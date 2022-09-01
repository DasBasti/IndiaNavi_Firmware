#include <unity.h>
extern void RUN_TESTS_DISPLAY();

void app_main() {
    UNITY_BEGIN();
    RUN_TESTS_DISPLAY();
    UNITY_END();
}