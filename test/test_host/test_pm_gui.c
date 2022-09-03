#include <unity.h>
#include "display.h"
#include "gui/label.h"
#include "gui/image.h"

#include <fonts/font8x8.h>
#include <fonts/font8x16.h>
font_t f8x8, f8x16;
extern display_t *dsp;

extern error_code_t write_pixel(struct display *dsp, uint16_t x, uint16_t y,
                                uint8_t color);

void test_label_create()
{
    label_t *label = label_create("test", &f8x8, 0, 1, 2, 3);
    TEST_ASSERT_NOT_NULL(label);
    TEST_ASSERT_EQUAL_STRING("test", label->text);
    TEST_ASSERT_EQUAL(&f8x8, label->font);
    TEST_ASSERT_EQUAL_UINT16(0, label->box.left);
    TEST_ASSERT_EQUAL_UINT16(1, label->box.top);
    TEST_ASSERT_EQUAL_UINT16(2, label->box.width);
    TEST_ASSERT_EQUAL_UINT16(3, label->box.height);
}

/*
 * for the lable we render once and the second render will fail
 */
volatile uint32_t label_onBeforeRender_cnt = 0;
volatile uint32_t label_onAfterRender_cnt = 0;
error_code_t label_onBeforeRender(display_t *dsp, void *label)
{
    if (label_onBeforeRender_cnt == 0)
    {
        label_onBeforeRender_cnt += 1;
        return PM_OK;
    }
    PM_FAIL;
}
error_code_t label_onAfterRender(display_t *dsp, void *label)
{
    if (label_onAfterRender_cnt == 0)
    {
        label_onAfterRender_cnt += 1;
        return PM_OK;
    }
    PM_FAIL;
}

void test_label_render()
{
    label_t *label = label_create("test", &f8x8, 0, 1, 2, 3);
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    TEST_ASSERT_EQUAL_UINT32(0, label_onAfterRender_cnt);
    TEST_ASSERT_EQUAL_UINT32(0, label_onAfterRender_cnt);
    // append on_render callbacks
    label->onBeforeRender = label_onBeforeRender;
    label->onAfterRender = label_onAfterRender;
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    TEST_ASSERT_EQUAL_UINT32(1, label_onAfterRender_cnt);
    TEST_ASSERT_EQUAL_UINT32(1, label_onAfterRender_cnt);
    // test on_render callbacks failing
    TEST_ASSERT_TRUE(ABORT == label_render(dsp, label));
    label->onBeforeRender = 0;
    TEST_ASSERT_TRUE(ABORT == label_render(dsp, label));
    label->onAfterRender = 0;
}

void test_label_borders()
{
    label_t *label = label_create("test", &f8x8, 0, 1, 2, 3);
    // test rendering borders
    label->borderColor = BLACK;
    label->borderWidth = 1;
    label->borderLines = (TOP_SOLID | BOTTOM_SOLID | LEFT_SOLID | RIGHT_SOLID);
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    label->roundedCorners = (TOP_LEFT | TOP_RIGHT | BOTTOM_LEFT | BOTTOM_RIGHT);
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
}

void test_label_background()
{
    label_t *label = label_create("test", &f8x8, 0, 1, 50, 50);
    // test rendering borders
    label->backgroundColor = BLACK;
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
}

void test_label_textalign()
{
    label_t *label = label_create("test", &f8x8, 0, 1, 50, 50);
    label->alignHorizontal = CENTER;
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    label->alignHorizontal = RIGHT;
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    label->alignHorizontal = LEFT;
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    label->alignVertical = BOTTOM;
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    label->alignVertical = MIDDLE;
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    label->alignVertical = TOP;
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
}

void test_label_shrink_to_text()
{
    label_t *label = label_create("test", &f8x8, 0, 0, 0, 0);
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    TEST_ASSERT_EQUAL(0, label->box.width);
    TEST_ASSERT_TRUE(PM_OK == label_shrink_to_text(label));
    TEST_ASSERT_EQUAL(32, label->box.width);
    label->text = "newtest";
    TEST_ASSERT_EQUAL(32, label->box.width);
    TEST_ASSERT_TRUE(PM_OK == label_shrink_to_text(label));
    TEST_ASSERT_EQUAL(56, label->box.width);
    image_t *img = image_create(NULL, 0, 0, 10, 10);
    label->child = img;
    TEST_ASSERT_TRUE(PM_OK == label_shrink_to_text(label));
    TEST_ASSERT_EQUAL(56, label->box.width);
    TEST_ASSERT_EQUAL(10, label->box.height);
}

void RUN_TESTS_PM_GUI()
{
    RUN_TEST(test_label_create);
    RUN_TEST(test_label_shrink_to_text);
    RUN_TEST(test_label_render);
    RUN_TEST(test_label_borders);
    RUN_TEST(test_label_background);
    RUN_TEST(test_label_textalign);
}