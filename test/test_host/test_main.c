#include <unity.h>

extern void RUN_TESTS_READLINE();
extern void RUN_TESTS_PM_GUI();

int main(int argc, char** argv){
    UNITY_BEGIN();
    RUN_TESTS_READLINE();
    RUN_TESTS_PM_GUI();
    UNITY_END();
}