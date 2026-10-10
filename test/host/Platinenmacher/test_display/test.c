#include <unity.h>
#include "display.h"
#include "gui/label.h"
#include "gui/image.h"
#include "gui/graph.h"
#include <string.h>

#define DISPLAY_WIDTH 20
#define DISPLAY_HEIGHT 20

#define printfb (printf_fb(dsp->fb, DISPLAY_HEIGHT,DISPLAY_WIDTH))

#include <fonts/font8x8.h>
#include <fonts/font8x16.h>
font_t f8x8, f8x16;
uint8_t image_data[] = {0,0,1,1};
display_t *dsp;

error_code_t write_pixel(const struct display *dsp, int16_t x, int16_t y,
                         uint8_t color)
{
    dsp->fb[((y * DISPLAY_WIDTH) + x)] = color;
    return PM_OK;
}

uint8_t decompress(rect_t *size, int16_t x, int16_t y, const uint8_t *data)
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

static uint8_t fill_color;
static error_code_t fill(const display_t *dsp, uint8_t color)
{
    fill_color = color;
    return PM_OK;
}

void test_display_fill_uses_driver_fill()
{
    dsp->fill = fill;
    fill_color = 0xff;
    dsp->fb[0] = 0;
    TEST_ASSERT_EQUAL(PM_OK, display_fill(dsp, WHITE));
    TEST_ASSERT_EQUAL_UINT8(WHITE, fill_color);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, dsp->fb[0], "pixels are not drawn one by one");
}

void test_display_draw_image_is_clipped()
{
    uint8_t image[16];
    memset(image, WHITE, sizeof(image));
    memset(dsp->fb, 0, dsp->fb_size);
    // only the bottom right quarter is on the display
    TEST_ASSERT_EQUAL(OUT_OF_BOUNDS, display_draw_image(dsp, image, -2, -2, 4, 4));
    TEST_ASSERT_EQUAL_UINT8(WHITE, dsp->fb[0]);
    TEST_ASSERT_EQUAL_UINT8(WHITE, dsp->fb[DISPLAY_WIDTH + 1]);
    TEST_ASSERT_EQUAL_UINT8(0, dsp->fb[2]);
    TEST_ASSERT_EQUAL_UINT8(0, dsp->fb[2 * DISPLAY_WIDTH]);
    // the top left quarter at the bottom right corner
    TEST_ASSERT_EQUAL(PM_OK, display_draw_image(dsp, image, DISPLAY_WIDTH - 2, DISPLAY_HEIGHT - 2, 4, 4));
    TEST_ASSERT_EQUAL_UINT8(WHITE, dsp->fb[dsp->fb_size - 1]);
    TEST_ASSERT_EQUAL_UINT8(0, dsp->fb[dsp->fb_size - 3]);
}

static rect_t drawn_image, drawn_visible;
static error_code_t draw_image(const display_t *dsp, const uint8_t *data, const rect_t *image, const rect_t *visible)
{
    drawn_image = *image;
    drawn_visible = *visible;
    return PM_OK;
}

void test_display_draw_image_uses_driver_with_visible_part()
{
    uint8_t image[16] = { 0 };
    dsp->draw_image = draw_image;
    memset(dsp->fb, 0, dsp->fb_size);
    display_draw_image(dsp, image, -1, DISPLAY_HEIGHT - 3, 4, 4);
    TEST_ASSERT_EQUAL_INT16(-1, drawn_image.left);
    TEST_ASSERT_EQUAL_INT16(DISPLAY_HEIGHT - 3, drawn_image.top);
    TEST_ASSERT_EQUAL_UINT16(4, drawn_image.width);
    TEST_ASSERT_EQUAL_INT16(1, drawn_visible.left);
    TEST_ASSERT_EQUAL_INT16(0, drawn_visible.top);
    TEST_ASSERT_EQUAL_UINT16(3, drawn_visible.width);
    TEST_ASSERT_EQUAL_UINT16(3, drawn_visible.height);

    // completely outside, nothing to draw
    drawn_visible.width = 0;
    display_draw_image(dsp, image, DISPLAY_WIDTH, 0, 4, 4);
    TEST_ASSERT_EQUAL_UINT16(0, drawn_visible.width);
}

static rect_t filled_rect;
static int fill_rect_calls;
static error_code_t fill_rect(const display_t *dsp, const rect_t *rect, uint8_t color)
{
    filled_rect = *rect;
    fill_rect_calls++;
    return PM_OK;
}

void test_display_rect_fill_uses_driver_with_part_on_display()
{
    dsp->fill_rect = fill_rect;
    fill_rect_calls = 0;
    display_rect_fill(dsp, -3, 15, 10, 10, WHITE);
    TEST_ASSERT_EQUAL_INT(1, fill_rect_calls);
    TEST_ASSERT_EQUAL_INT16(0, filled_rect.left);
    TEST_ASSERT_EQUAL_INT16(15, filled_rect.top);
    TEST_ASSERT_EQUAL_UINT16(7, filled_rect.width);
    TEST_ASSERT_EQUAL_UINT16(5, filled_rect.height);

    // outside of the display or transparent, nothing to fill
    display_rect_fill(dsp, DISPLAY_WIDTH, 0, 4, 4, WHITE);
    display_rect_fill(dsp, 0, 0, 4, 4, TRANSPARENT);
    TEST_ASSERT_EQUAL_INT(1, fill_rect_calls);
}

/* the graph drawn into a canvas of the size of the display */
static display_t *render_graph(graph_t *graph)
{
    display_t *canvas = display_canvas_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    TEST_ASSERT_NOT_NULL(canvas);
    display_fill(canvas, WHITE);
    graph_renderer(canvas, graph);
    return canvas;
}

void test_graph_with_static_data_looks_the_same()
{
    graph_point_t data[8], other[8];
    for (int i = 0; i < 8; i++) {
        data[i].value = i * 3;
        data[i].color = BLACK;
        other[i].value = 21 - i * 3;
        other[i].color = BLACK;
    }
    graph_t *graph = graph_create(1, 2, 18, 16, data, 8, &f8x8);
    graph_set_range(graph, 0, 21);
    graph->background_color = TRANSPARENT; // the canvas has to keep what is below
    graph->current_position = 3;
    graph->current_position_color = BLACK;

    display_t *drawn = render_graph(graph);
    graph->static_data = true;
    display_t *cached = render_graph(graph);
    TEST_ASSERT_NOT_NULL(graph->cache);
    display_t *copied = render_graph(graph); // from the cache
    TEST_ASSERT_EQUAL_UINT8_ARRAY(drawn->fb, cached->fb, drawn->fb_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(drawn->fb, copied->fb, drawn->fb_size);

    // new data is drawn again
    graph_update_data(graph, other, 8);
    TEST_ASSERT_NULL(graph->cache);
    display_t *updated = render_graph(graph);
    graph->static_data = false;
    display_t *updated_drawn = render_graph(graph);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(updated_drawn->fb, updated->fb, drawn->fb_size);
    TEST_ASSERT_FALSE(memcmp(drawn->fb, updated->fb, drawn->fb_size) == 0);

    display_canvas_free(drawn);
    display_canvas_free(cached);
    display_canvas_free(copied);
    display_canvas_free(updated);
    display_canvas_free(updated_drawn);
    graph_free(graph);
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
        0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
    };
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

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_display_init);
    RUN_TEST(test_display_draw_pixel);
    RUN_TEST(test_display_draw_out_of_bound);
    RUN_TEST(test_display_fill);
    RUN_TEST(test_display_fill_uses_driver_fill);
    RUN_TEST(test_display_draw_image_is_clipped);
    RUN_TEST(test_graph_with_static_data_looks_the_same);
    RUN_TEST(test_display_rect_fill_uses_driver_with_part_on_display);
    RUN_TEST(test_display_draw_image_uses_driver_with_visible_part);
    RUN_TEST(test_display_draw_colors);
    RUN_TEST(test_display_rect_draw);
    RUN_TEST(test_display_line_draw);
    RUN_TEST(test_display_circle_draw);
    RUN_TEST(test_display_circle_draw_segment);
    RUN_TEST(test_display_rect_fill);
    RUN_TEST(test_display_circle_fill);
    RUN_TEST(test_display_text_draw);
    
    UNITY_END();
}
