#include <unity.h>
#include "../mock/mock_display.h"
#include "../mock/mock_renderhooks.h"

#include "display.h"
#include "gui/label.h"
#include "gui/image.h"

#define printfb (printf_fb(dsp->fb, DISPLAY_HEIGHT,DISPLAY_WIDTH))

#include <fonts/font8x8.h>
#include <fonts/font8x16.h>
font_t f8x8, f8x16;
uint8_t image_data[] = {0,0,1,1};
display_t *dsp;

void setUp()
{
    dsp = display_init(DISPLAY_WIDTH, DISPLAY_HEIGHT, 8, DISPLAY_ROTATE_0);
    dsp->fb_size = DISPLAY_HEIGHT * DISPLAY_WIDTH;
    dsp->fb = malloc(dsp->fb_size);
    dsp->write_pixel = write_pixel;
    dsp->decompress = decompress;
    font_load_from_array(&f8x8, font8x8, font8x8_name);
    font_load_from_array(&f8x16, font8x16, font8x16_name);
}

void tearDown()
{
    free(dsp);
}

void test_fonts() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(32, font_text_pixel_width(&f8x8, "Test"),"Fontsize calculation faild f8x8 width");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(32, font_text_pixel_width(&f8x16, "Test"),"Fontsize calculation faild f8x16 width");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, font_text_pixel_height(&f8x8, "Test"),"Fontsize calculation faild f8x8 height");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(16, font_text_pixel_height(&f8x16, "Test"),"Fontsize calculation faild f8x16 height");
}

void test_label_create()
{
    label_t *label = label_create("test", &f8x8, 0, 1, 2, 3);
    TEST_ASSERT_NOT_NULL(label);
    TEST_ASSERT_EQUAL_STRING("test", label->text);
    TEST_ASSERT_EQUAL(&f8x8, label->font);
    TEST_ASSERT_EQUAL_INT16(0, label->box.left);
    TEST_ASSERT_EQUAL_INT16(1, label->box.top);
    TEST_ASSERT_EQUAL_UINT16(2, label->box.width);
    TEST_ASSERT_EQUAL_UINT16(3, label->box.height);
}

void test_label_create_at_negative_position()
{
    label_t *label = label_create("test", &f8x8, -10, -10, 2, 3);
    TEST_ASSERT_NOT_NULL(label);
    TEST_ASSERT_EQUAL_STRING("test", label->text);
    TEST_ASSERT_EQUAL(&f8x8, label->font);
    TEST_ASSERT_EQUAL_INT16(-10, label->box.left);
    TEST_ASSERT_EQUAL_INT16(-10, label->box.top);
    TEST_ASSERT_EQUAL_UINT16(2, label->box.width);
    TEST_ASSERT_EQUAL_UINT16(3, label->box.height);
}

void test_label_render()
{
    label_t *label = label_create("test", &f8x8, 0, 1, 2, 3);
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    TEST_ASSERT_EQUAL_UINT32(0, onAfterRender_cnt);
    TEST_ASSERT_EQUAL_UINT32(0, onAfterRender_cnt);
    // append on_render callbacks
    onBeforeRender_cnt = 0;
    onAfterRender_cnt = 0;
    label->onBeforeRender = onBeforeRenderCounter;
    label->onAfterRender = onAfterRenderCounter;
    TEST_ASSERT_TRUE(PM_OK == label_render(dsp, label));
    TEST_ASSERT_EQUAL_UINT32(1, onAfterRender_cnt);
    TEST_ASSERT_EQUAL_UINT32(1, onAfterRender_cnt);
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

void test_image_render()
{
    image_t *img = image_create(image_data, 0, 0, 2, 2);
   
    TEST_ASSERT_TRUE(PM_OK == image_render(dsp, img));
    onBeforeRender_cnt = 0;
    onAfterRender_cnt = 0;
    img->onAfterRender = onAfterRenderCounter;
    img->onBeforeRender = onBeforeRenderCounter;
    TEST_ASSERT_TRUE(PM_OK == image_render(dsp, img));
    TEST_ASSERT_EQUAL(ABORT, image_render(dsp, img));
    img->onBeforeRender = 0;
    TEST_ASSERT_EQUAL(ABORT, image_render(dsp, img));
}

void test_image_render_at_negative_position()
{
    image_t *img = image_create(image_data, -10, -10, 2, 2);
   
    TEST_ASSERT_TRUE(PM_OK == image_render(dsp, img));
    onBeforeRender_cnt = 0;
    onAfterRender_cnt = 0;
    img->onAfterRender = onAfterRenderCounter;
    img->onBeforeRender = onBeforeRenderCounter;
    TEST_ASSERT_TRUE(PM_OK == image_render(dsp, img));
    TEST_ASSERT_EQUAL(ABORT, image_render(dsp, img));
    img->onBeforeRender = 0;
    TEST_ASSERT_EQUAL(ABORT, image_render(dsp, img));
    TEST_ASSERT_EQUAL_INT16(-10, img->box.top);
    TEST_ASSERT_EQUAL_INT16(-10, img->box.left);
}


int main(int argc, char **argv)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_fonts);
    RUN_TEST(test_label_create);
    RUN_TEST(test_label_create_at_negative_position);
    RUN_TEST(test_label_shrink_to_text);
    RUN_TEST(test_label_render);
    RUN_TEST(test_label_borders);
    RUN_TEST(test_label_background);
    RUN_TEST(test_label_textalign);
    RUN_TEST(test_image_render);
    RUN_TEST(test_image_render_at_negative_position);

    UNITY_END();
}
