#include <unity.h>
#include <Platinenmacher.h>

display_t *eink;

void setUp(){
    eink = ACEP_5IN65_Init(DISPLAY_ROTATE_90);
}

void tearDown(){

}

void test_display_init() {
    TEST_ASSERT_NOT_NULL(eink);
}

void test_display_clear_and_commit_fb() {
    TEST_ASSERT_NOT_NULL(eink);    
   	TEST_ASSERT_TRUE_MESSAGE(PM_OK == display_fill(eink, WHITE), "did fill white");
    TEST_ASSERT_TRUE_MESSAGE(PM_OK == display_commit_fb(eink), "did refresh");
}

void RUN_TESTS_DISPLAY() {
    //RUN_TEST(test_display_init);
    RUN_TEST(test_display_clear_and_commit_fb);
}