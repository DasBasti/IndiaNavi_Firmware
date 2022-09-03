#include <unity.h>
#include <stdlib.h>
#include "display.h"

#define DISPLAY_WIDTH 20
#define DISPLAY_HEIGHT 20

#include <helper.h>
#define printfb (printf_fb(dsp->fb, DISPLAY_HEIGHT,DISPLAY_WIDTH))

#include <fonts/font8x8.h>
#include <fonts/font8x16.h>
font_t f8x8, f8x16;
display_t *dsp;

error_code_t write_pixel(struct display *dsp, uint16_t x, uint16_t y,
                         uint8_t color)
{
    dsp->fb[((y * DISPLAY_WIDTH) + x)] = color;
    return PM_OK;
}

void setUp()
{
    dsp = display_init(DISPLAY_WIDTH, DISPLAY_HEIGHT, 8, DISPLAY_ROTATE_0);
    dsp->fb_size = DISPLAY_HEIGHT * DISPLAY_WIDTH;
    dsp->fb = malloc(dsp->fb_size);
    dsp->write_pixel = write_pixel;
    font_load_from_array(&f8x8, font8x8, font8x8_name);
    font_load_from_array(&f8x16, font8x16, font8x16_name);
}

void tearDown()
{
    free(dsp);
}

void test_display_init()
{
    TEST_ASSERT_NOT_NULL_MESSAGE(dsp, "display is 0");
    TEST_ASSERT_NOT_NULL_MESSAGE(dsp->fb, "famebuffer is 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, dsp->fb[0], "framebuffer");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(DISPLAY_HEIGHT, dsp->size.height, "height");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(DISPLAY_WIDTH, dsp->size.width, "width");
}

void test_display_fill()
{
    TEST_ASSERT_TRUE_MESSAGE(PM_OK == display_fill(dsp, WHITE), "did fill");
    TEST_ASSERT_EACH_EQUAL_UINT8_MESSAGE(WHITE, dsp->fb, dsp->fb_size, "pixels are white");
}

void test_display_draw_out_of_bound()
{
    TEST_ASSERT_TRUE_MESSAGE(PM_OK == display_pixel_draw(dsp, DISPLAY_HEIGHT-1,DISPLAY_WIDTH-1, WHITE), "pixel draw in bounds");
    TEST_ASSERT_TRUE_MESSAGE(OUT_OF_BOUNDS == display_pixel_draw(dsp, DISPLAY_HEIGHT,DISPLAY_WIDTH-1, WHITE), "pixel draw out of bounds x");
    TEST_ASSERT_TRUE_MESSAGE(OUT_OF_BOUNDS == display_pixel_draw(dsp, DISPLAY_HEIGHT-1,DISPLAY_WIDTH, WHITE), "pixel draw out of bounds y");
}

void test_display_draw_pixel() {
    TEST_ASSERT_TRUE_MESSAGE(PM_OK == display_pixel_draw(dsp, 0,0,WHITE), "draw pixel 0,0 white");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(WHITE, dsp->fb[0], "0 Pixel is WHITE");
    dsp->write_pixel = 0;
    TEST_ASSERT_TRUE_MESSAGE(PM_FAIL == display_pixel_draw(dsp, 0,0,WHITE), "draw pixel 0,0 white");
}

void test_display_draw_colors() {
    TEST_ASSERT_TRUE_MESSAGE(PM_OK == display_pixel_draw(dsp, 0,0,WHITE), "draw pixel 0,0 white");
    TEST_ASSERT_TRUE_MESSAGE(PM_OK == display_pixel_draw(dsp, 1,0,BLACK), "draw pixel 0,0 white");
    TEST_ASSERT_TRUE_MESSAGE(PM_OK == display_pixel_draw(dsp, 1,0,TRANSPARENT), "draw pixel 0,0 white");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(WHITE, dsp->fb[0], "0 Pixel is WHITE");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(BLACK, dsp->fb[1], "1 Pixel is BLACK");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(BLACK, dsp->fb[1], "1 Pixel is still BLACK");
}

void test_display_rect_draw(){
    uint8_t picture[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    };
    TEST_ASSERT_TRUE(PM_OK == display_fill(dsp, 0));
    TEST_ASSERT_TRUE(PM_OK == display_rect_draw(dsp, 1, 1, 4, 4, 1));
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(picture, dsp->fb, dsp->fb_size, "square not as expected");
}

void test_display_line_draw(){
    uint8_t picture[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,1,1,0,0,0,1,0,0,1,1,1,0,
        0,0,0,0,0,0,0,1,0,0,1,1,1,1,1,1,0,0,0,0,
        0,0,0,0,1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,0,
        1,1,1,1,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    };
    TEST_ASSERT_TRUE(PM_OK == display_fill(dsp, 0));
    TEST_ASSERT_TRUE(PM_OK == display_line_draw(dsp, 1, 1, 5, 3, 1));
    TEST_ASSERT_TRUE(PM_OK == display_line_draw(dsp, 3, 5, 1, 1, 1));
    TEST_ASSERT_TRUE(PM_OK == display_line_draw(dsp, 10, 6, 15, 13, 1));
    TEST_ASSERT_TRUE(PM_OK == display_line_draw(dsp, 2, 16, 16, 3, 1));
    TEST_ASSERT_TRUE(PM_OK == display_line_draw(dsp, 3, 16, 2, 16, 1));
    TEST_ASSERT_TRUE(PM_OK == display_line_draw(dsp, 0, 13, 18, 10, 1));
    TEST_ASSERT_TRUE(OUT_OF_BOUNDS == display_line_draw(dsp, 16, 16, DISPLAY_HEIGHT+1, DISPLAY_WIDTH+1, 1));
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(picture, dsp->fb, dsp->fb_size, "line not as expected");
}

void test_display_circle_draw(){
    uint8_t picture[] = {
        0,0,0,0,0,1,0,0,0,0,0,0,1,1,1,1,1,1,1,0,
        0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,
        0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,1,1,0,0,0,1,0,0,0,1,1,0,0,0,0,
        1,1,1,1,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,
        0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,
        0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,
        0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,
        0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,
        0,0,0,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,1,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   
    };
    TEST_ASSERT_TRUE(PM_OK == display_fill(dsp, 0));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw(dsp, 1, 1, 5, 1));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw(dsp, 10, 10, 8, 1));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw(dsp, 15, 5, 6, 1));
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(picture, dsp->fb, dsp->fb_size, "circle not as expected");
}

void test_display_circle_draw_segment(){
    uint8_t picture[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
        0,0,0,1,0,0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,
        0,0,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,
        0,0,0,1,0,0,0,0,0,0,1,1,1,1,0,0,0,1,0,0,
        0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,  
    };
    TEST_ASSERT_TRUE(PM_OK == display_fill(dsp, 0));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw_segment(dsp, 10, 10, 10, 1, 0x01));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw_segment(dsp, 10, 10, 8, 1, 0x02));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw_segment(dsp, 10, 10, 10, 1, 0x04));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw_segment(dsp, 10, 10, 8, 1, 0x08));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw_segment(dsp, 10, 10, 10, 1, 0x10));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw_segment(dsp, 10, 10, 8, 1, 0x20));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw_segment(dsp, 10, 10, 10, 1, 0x40));
    TEST_ASSERT_TRUE(PM_OK == display_circle_draw_segment(dsp, 10, 10, 8, 1, 0x80));
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(picture, dsp->fb, dsp->fb_size, "circle segments not as expected");
}

void test_display_rect_fill(){
    uint8_t picture[] = {
        0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,1,1,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,    
    };
    TEST_ASSERT_TRUE(PM_OK == display_fill(dsp, 0));
    TEST_ASSERT_TRUE(PM_OK == display_rect_fill(dsp, 1, 1, 5, 5, 1));
    TEST_ASSERT_TRUE(PM_OK == display_rect_fill(dsp, 7, 12, 8, 8, 1));
    TEST_ASSERT_TRUE(PM_OK == display_rect_fill(dsp, 18, 8, 5, 5, 1));
    TEST_ASSERT_TRUE(PM_OK == display_rect_fill(dsp, 10, 0, 5, 5, 1));
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(picture, dsp->fb, dsp->fb_size, "filled rect not as expected");
}

void test_display_circle_fill(){
    uint8_t picture[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
        0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
        0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
        0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
        0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    };
    TEST_ASSERT_TRUE(PM_OK == display_fill(dsp, 0));
    TEST_ASSERT_TRUE(PM_OK == display_circle_fill(dsp, 10, 10, 8, 1));
    TEST_IGNORE_MESSAGE("this fails because the current circle code creates holes!");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(picture, dsp->fb, dsp->fb_size, "filled rect not as expected");
}

void test_display_text_draw() {
    uint8_t picture[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,1,1,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,
        0,0,1,1,1,1,0,0,0,0,1,1,0,0,1,1,0,0,0,0,
        0,1,1,0,0,1,1,0,0,0,1,1,0,0,1,1,0,0,0,0,
        0,1,1,0,0,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,1,0,0,0,1,1,0,0,1,1,0,0,0,0,
        0,1,1,0,0,1,1,0,0,0,1,1,0,0,1,1,0,0,0,0,
        0,1,1,0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,1,1,1,1,0,0,1,1,1,1,1,0,0,0,0,0,0,
        0,0,1,1,0,0,1,1,0,0,1,1,0,1,1,0,0,0,0,0,
        0,1,1,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,
        0,1,1,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,
        0,1,1,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,
        0,0,1,1,0,0,1,1,0,0,1,1,0,1,1,0,0,0,0,0,
        0,0,0,1,1,1,1,0,0,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    };
    TEST_ASSERT_TRUE(PM_OK == display_fill(dsp, 0));
    TEST_ASSERT_TRUE(PM_OK == display_text_draw(dsp, &f8x8, 1, 1, "AB", 1));
    TEST_ASSERT_TRUE(PM_OK == display_text_draw(dsp, &f8x8, 1, 10, "CD", 1));
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(picture, dsp->fb, dsp->fb_size, "font not as expected");
}

void test_fonts() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(32, font_text_pixel_width(&f8x8, "Test"),"Fontsize calculation faild f8x8 width");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(32, font_text_pixel_width(&f8x16, "Test"),"Fontsize calculation faild f8x16 width");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, font_text_pixel_height(&f8x8, "Test"),"Fontsize calculation faild f8x8 height");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(16, font_text_pixel_height(&f8x16, "Test"),"Fontsize calculation faild f8x16 height");
}

void RUN_TESTS_PM_DISPLAY()
{
    RUN_TEST(test_display_init);
    RUN_TEST(test_display_draw_pixel);
    RUN_TEST(test_display_draw_out_of_bound);
    RUN_TEST(test_display_fill);
    RUN_TEST(test_display_draw_colors);
    RUN_TEST(test_display_rect_draw);
    RUN_TEST(test_display_line_draw);
    RUN_TEST(test_display_circle_draw);
    RUN_TEST(test_display_circle_draw_segment);
    RUN_TEST(test_display_rect_fill);
    RUN_TEST(test_display_circle_fill);
    RUN_TEST(test_display_text_draw);
    RUN_TEST(test_fonts);
}
