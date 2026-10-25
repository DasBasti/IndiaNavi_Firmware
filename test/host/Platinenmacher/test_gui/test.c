#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "../mock/mock_display.h"
#include "../mock/mock_renderhooks.h"

#include "display.h"
#include "gui/label.h"
#include "gui/image.h"
#include "gui/ui.h"

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

/* the host tests also run on a black and white display */
#ifdef EINK_7COLOR
#define TEST_ARROW_COLOR RED
#define TEST_FILL_COLOR GREEN
#else
#define TEST_ARROW_COLOR BLACK
#define TEST_FILL_COLOR BLACK
#endif

void test_ui_segments_filled()
{
    TEST_ASSERT_EQUAL_UINT8(0, ui_segments_filled(0));
    TEST_ASSERT_EQUAL_UINT8(0, ui_segments_filled(9));
    TEST_ASSERT_EQUAL_UINT8(1, ui_segments_filled(10));
    TEST_ASSERT_EQUAL_UINT8(5, ui_segments_filled(59));
    TEST_ASSERT_EQUAL_UINT8(10, ui_segments_filled(100));
    TEST_ASSERT_EQUAL_UINT8(10, ui_segments_filled(200));
}

void test_ui_button_hint_width_and_height()
{
    // border, padding, text, padding, arrow, padding
    TEST_ASSERT_EQUAL_INT16(UI_BORDER + UI_PAD + 32 + UI_PAD + UI_ARROW_SIZE + UI_PAD,
        ui_button_hint_width(&f8x16, "Test", NULL, NULL));
    // the wider of text and sub text decides
    TEST_ASSERT_EQUAL_INT16(UI_BORDER + UI_PAD + 64 + UI_PAD + UI_ARROW_SIZE + UI_PAD,
        ui_button_hint_width(&f8x16, "Test", &f8x8, "12345678"));
    TEST_ASSERT_EQUAL_INT16(UI_BORDER + UI_PAD + 16 + UI_PAD + UI_BORDER,
        ui_button_hint_height(&f8x16, NULL, NULL));
    TEST_ASSERT_EQUAL_INT16(UI_BORDER + UI_PAD + 16 + UI_PAD + UI_BORDER + 8 + 2,
        ui_button_hint_height(&f8x16, &f8x8, "x"));
}

void test_ui_button_hint_touches_the_right_edge()
{
    display_t* big = display_init(120, 60, 8, DISPLAY_ROTATE_0);
    big->fb_size = 120 * 60;
    big->fb = calloc(big->fb_size, 1);
    big->write_pixel = write_pixel;
    memset(big->fb, WHITE, big->fb_size);

    TEST_ASSERT_EQUAL(PM_OK, ui_button_hint(big, 2, &f8x16, "HOLD", NULL, NULL));
    int16_t width = ui_button_hint_width(&f8x16, "HOLD", NULL, NULL);
    // the outline is black, at the right edge and at the left edge of the box
    TEST_ASSERT_EQUAL_UINT8(BLACK, big->fb[(2 + 8) * 120 + 119]);
    TEST_ASSERT_EQUAL_UINT8(BLACK, big->fb[(2 + 8) * 120 + (120 - width)]);
    // left of the box nothing is drawn
    TEST_ASSERT_EQUAL_UINT8(WHITE, big->fb[(2 + 8) * 120 + (120 - width - 1)]);
    // the arrow is red
    int reds = 0;
    for (int i = 0; i < 120 * 60; i++)
        if (big->fb[i] == TEST_ARROW_COLOR)
            reds++;
    TEST_ASSERT_GREATER_THAN(10, reds);
    free(big->fb);
    free(big);
}

void test_ui_segment_bar_fills_segments()
{
    display_t* big = display_init(120, 20, 8, DISPLAY_ROTATE_0);
    big->fb_size = 120 * 20;
    big->fb = calloc(big->fb_size, 1);
    big->write_pixel = write_pixel;
    memset(big->fb, WHITE, big->fb_size);

    ui_segment_bar(big, 0, 0, 119, 10, 30, TEST_FILL_COLOR);
    int16_t segment_width = (119 - (UI_SEGMENTS - 1) * UI_SEGMENT_GAP) / UI_SEGMENTS;
    int16_t middle = 5;
    // inside the third segment green, inside the fourth white
    TEST_ASSERT_EQUAL_UINT8(TEST_FILL_COLOR, big->fb[middle * 120 + 2 * (segment_width + UI_SEGMENT_GAP) + segment_width / 2]);
    TEST_ASSERT_EQUAL_UINT8(WHITE, big->fb[middle * 120 + 3 * (segment_width + UI_SEGMENT_GAP) + segment_width / 2]);
    free(big->fb);
    free(big);
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
    RUN_TEST(test_ui_segments_filled);
    RUN_TEST(test_ui_button_hint_width_and_height);
    RUN_TEST(test_ui_button_hint_touches_the_right_edge);
    RUN_TEST(test_ui_segment_bar_fills_segments);

    UNITY_END();
}
