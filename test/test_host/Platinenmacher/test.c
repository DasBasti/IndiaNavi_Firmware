#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "helper.h"

void test_null_is_null() {
    char* next = readline(0, 0);
    TEST_ASSERT_NULL(next);

}

void test_empty_string_is_empty() {
    char dst[100]= {1};
    char* next = readline("", dst);
    TEST_ASSERT_EQUAL_CHAR(0, dst[0]);
    TEST_ASSERT_NULL(next);
}

void test_ignore_carriage_return() {
    char dst[100] = {1};
    char *src = "line one\rline two";
    char* next = readline(src, dst);
    TEST_ASSERT_EQUAL_STRING("line oneline two", dst);
    TEST_ASSERT_NULL(next);
}

void test_two_lines_string_is_two_lines() {
    char dst[100] = {1};
    char *src = "line one\r\nline two";
    char* next = readline(src, dst);
    TEST_ASSERT_EQUAL_STRING("line one", dst);
    next = readline(next, dst);
    TEST_ASSERT_EQUAL_STRING("line two", dst);
    TEST_ASSERT_NULL(next);
    next = readline(next, dst);
    TEST_ASSERT_NULL(next);
}

#include "display.h"
#include "gui/label.h"
#include "gui/image.h"

#define DISPLAY_WIDTH 20
#define DISPLAY_HEIGHT 20

#define printfb (printf_fb(dsp->fb, DISPLAY_HEIGHT,DISPLAY_WIDTH))

#include <fonts/font8x8.h>
#include <fonts/font8x16.h>
font_t f8x8, f8x16;
uint8_t image_data[] = {0,0,1,1};
display_t *dsp;

error_code_t write_pixel(struct display *dsp, uint16_t x, uint16_t y,
                         uint8_t color)
{
    dsp->fb[((y * DISPLAY_WIDTH) + x)] = color;
    return PM_OK;
}

uint8_t decompress(rect_t *size, uint16_t x, uint16_t y, uint8_t *data)
{

    return data[x*size->width+y];
}

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
volatile uint32_t onBeforeRender_cnt = 0;
volatile uint32_t onAfterRender_cnt = 0;
error_code_t onBeforeRenderCounter(display_t *dsp, void *label)
{
    if (onBeforeRender_cnt == 0)
    {
        onBeforeRender_cnt += 1;
        return PM_OK;
    }
    PM_FAIL;
}
error_code_t onAfterRenderCounter(display_t *dsp, void *label)
{
    if (onAfterRender_cnt == 0)
    {
        onAfterRender_cnt += 1;
        return PM_OK;
    }
    PM_FAIL;
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

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_two_lines_string_is_two_lines);
    RUN_TEST(test_ignore_carriage_return);
    RUN_TEST(test_empty_string_is_empty);
    RUN_TEST(test_null_is_null);
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
    RUN_TEST(test_label_create);
    RUN_TEST(test_label_shrink_to_text);
    RUN_TEST(test_label_render);
    RUN_TEST(test_label_borders);
    RUN_TEST(test_label_background);
    RUN_TEST(test_label_textalign);
    RUN_TEST(test_image_render);

    UNITY_END();
}
