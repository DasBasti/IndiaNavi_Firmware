#include <unity.h>

extern void RUN_TESTS_READLINE();

void app_main(){
    UNITY_BEGIN();
    RUN_TESTS_READLINE();
    UNITY_END();
}