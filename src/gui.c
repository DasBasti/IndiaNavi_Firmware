/*
 * GUI for the IndiaNavi Applcation
 *
 * Handles GuiTask and DisplayManager
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#include "gui.h"
#include "pins.h"
#include "tasks.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <hw/regulator_gpio.h>

#include "time.h"
#include <sys/time.h>

#include <driver/gpio.h>

#include "icons_32/icons_32.h"

const uint16_t margin_top = 5;
const uint16_t margin_bottom = 5;
const uint16_t margin_vertical = 10;
const uint16_t margin_left = 5;
const uint16_t margin_right = 5;
const uint16_t margin_horizontal = 10;

static const char* TAG = "GUI";

static display_t* eink;

extern void vTaskGetRunTimeStats(char* pcWriteBuffer);

render_t* render_pipeline[RL_MAX]; // maximum number of rendered items
render_t* render_last[RL_MAX];     // pointer to end of render pipeline
static uint8_t render_needed = 0;

app_mode_t _app_mode = APP_MODE_GPS_CREATE;
font_t f8x8, f8x16;
label_t* clock_label;
label_t* battery_label;
label_t* north_indicator_label;
label_t* wifi_indicator_label;
label_t* gps_indicator_label;
label_t* sd_indicator_label;

map_position_t* map_position;

acep_5in65_dev_t eink_dev = {
    .clk = EINK_SPI_CLK,
    .mosi = EINK_SPI_MOSI,
    .select = EINK_SPI_nCS,
    .dc = EINK_DC,
    .busy = EINK_BUSY,
    .host = SPI3_HOST,
};

/**
 * Add render function to pipeline
 *
 * @return render slot
 */
render_t* add_to_render_pipeline(error_code_t (*render)(const display_t* dsp, void* component),
    void* comp,
    enum RenderLayer layer)
{
    // increase render slot before adding this one. Slot 0 is overflow!
    render_t* rd = RTOS_Malloc(sizeof(render_t));
    if (!rd) {
        ESP_LOGE(TAG, "render pipeline full!");
        return 0;
    }
    rd->render = render;
    rd->comp = comp;

    if (render_pipeline[layer] == NULL) {
        render_pipeline[layer] = rd;
    } else {
        render_last[layer]->next = rd;
    }
    render_last[layer] = rd;

    return rd;
}

void free_render_pipeline(enum RenderLayer layer)
{
    render_t* r = render_pipeline[layer];
    while (r) {
        render_t* rn;
        rn = r;
        r = r->next;
        RTOS_Free(rn);
    }
}

void free_all_render_pipelines()
{
    for (uint8_t i = 0; i < RL_MAX; i++)
        free_render_pipeline(i);
}

/**
 * Add a prerender callback to pipeline
 *
 * This is called before all other renderers are called.
 *
 * @return render slot
 */
render_t* add_pre_render_callback(error_code_t (*cb)(const display_t* dsp, void* component))
{
    return add_to_render_pipeline(cb, NULL, RL_PRE_RENDER);
}

static label_t* create_icon_with_text(const display_t* dsp, uint8_t* icon_data,
    uint16_t left, uint16_t top, char* text, font_t* font)
{

    image_t* img = image_create(icon_data, left, top, ICON_SIZE,
        ICON_SIZE);

    label_t* il = label_create(text, font,
        img->box.left + img->box.width + margin_left, top, 0, 0);
    il->child = img;
    label_shrink_to_text(il);
    il->alignVertical = MIDDLE;
    add_to_render_pipeline(label_render, il, RL_GUI_ELEMENTS);
    // render image after Label is rendered
    add_to_render_pipeline(image_render, img, RL_GUI_ELEMENTS);
    return il;
}

/**
 * Callbacks from renderer for clock label
 */
error_code_t updateTimeText(const display_t* dsp, void* comp)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* timeinfo = localtime(&tv.tv_sec);
    xSemaphoreTake(print_semaphore, portMAX_DELAY);
    sprintf(clock_label->text, "%02d:%02d", timeinfo->tm_hour,
        timeinfo->tm_min);
    xSemaphoreGive(print_semaphore);
    return PM_OK;
}

static void create_top_bar(const display_t* dsp)
{
    label_t* sb = label_create("", &f8x8, 0, 0, dsp->size.width,
        ICON_SIZE + margin_vertical);

    sb->borderColor = BLACK;
    sb->borderWidth = 1;
    sb->borderLines = ALL_SOLID;
    sb->alignHorizontal = CENTER;
    sb->alignVertical = MIDDLE;
    sb->backgroundColor = WHITE;
    add_to_render_pipeline(label_render, sb, RL_GUI_BACKGROUND);

    battery_label = create_icon_with_text(dsp, bat_100,
        sb->box.left + margin_left, margin_top, RTOS_Malloc(4), &f8x8);
    save_sprintf(battery_label->text, "...%%");
    label_shrink_to_text(battery_label);

    north_indicator_label = create_icon_with_text(dsp, norden,
        battery_label->box.left + battery_label->box.width + margin_horizontal,
        margin_top, "", &f8x8);

    wifi_indicator_label = create_icon_with_text(dsp, WIFI_0,
        north_indicator_label->box.left + north_indicator_label->box.width + margin_horizontal,
        margin_top, "", &f8x8);

    char* GPSView = RTOS_Malloc(sizeof(char) * 5);
    gps_indicator_label = create_icon_with_text(dsp, noGPS,
        dsp->size.width - ICON_SIZE - (2 * margin_right) - 16, margin_top, GPSView, &f8x8);

    sd_indicator_label = create_icon_with_text(dsp, noSD,
        gps_indicator_label->box.left - 2 * ICON_SIZE - margin_right, margin_top, "",
        &f8x8);

#ifdef CLOCK
    /* global clock label. */
    char* time = RTOS_Malloc(6);
    clock_label = label_create(time, &f8x8, sb->box.left, sb->box.top,
        sb->box.width, sb->box.height);
    clock_label->alignVertical = MIDDLE;
    clock_label->alignHorizontal = CENTER;
    clock_label->onBeforeRender = updateTimeText;
    add_to_render_pipeline(label_render, clock_label, RL_GUI_ELEMENTS);
#endif
}

/**
 * Render all App components.
 */
static error_code_t app_render()
{
    render_t* rd;
    uint64_t start = esp_timer_get_time();
    display_fill(eink, WHITE);
    for (uint8_t layer = 0; layer < RL_MAX; layer++) {
        rd = render_pipeline[layer];
        while (rd) {
            if (render_needed)
                return DEFERRED;
            if (rd->render)
                rd->render(eink, rd->comp);
            rd = rd->next;
        }
        vTaskDelay(0);
    }

    uint64_t end = esp_timer_get_time();

    ESP_LOGI(TAG, "render time %lu ms", (uint32_t)(end - start) / 1000);

    return PM_OK;
}

/**
 * Set App mode
 */
void gui_set_app_mode(app_mode_t mode)
{
    // If we do not change mode we do not need to rerender
    if (mode == _app_mode)
        return;
    _app_mode = mode;
    render_needed = 1;
}

/**
 * Display App
 */
void app_screen(const display_t* dsp)
{
    switch (_app_mode) {
    case APP_START_SCREEN:
        start_screen_create(dsp);
        gui_set_app_mode(APP_START_SCREEN_TRANSITION);
        break;
    case APP_TEST_SCREEN:
        test_screen_create(dsp);
        gui_set_app_mode(APP_MODE_RUNNING);
        break;
    case APP_START_SCREEN_TRANSITION:
        /* free start screen and fall throught to map screen generation*/
        start_screen_free();
        gui_set_app_mode(APP_MODE_GPS_CREATE);
        __attribute__((fallthrough));
    case APP_MODE_GPS_CREATE:
        map_screen_create(dsp);
        gui_set_app_mode(APP_MODE_RUNNING);
        break;
    case APP_MODE_TURN_OFF:
        off_screen_create(dsp);
        gui_set_app_mode(APP_MODE_OFF);
        break;
    case APP_MODE_OFF:
        enter_deep_sleep();
    case APP_MODE_RUNNING:
    default:
        break;
    }
}

void trigger_rendering()
{
    render_needed = 1;
}

void render_cmd_cb(const command_t* cmd)
{
    ESP_LOGI(TAG, "manual redraw");
    trigger_rendering();
}

void StartGuiTask(void const* argument)
{
    ESP_LOGI(TAG, "init");
    ESP_LOGI(TAG, "fonts loading");
    font_load_from_array(&f8x8, font8x8, font8x8_name);
    font_load_from_array(&f8x16, font8x16, font8x16_name);

    // gpio_t *eeprom = gpio_create(OUTPUT, 0, EINK_EEPROM_nEN);
    // eeprom->onValue = GPIO_RESET;
    // gpio_write(eeprom, GPIO_SET);

    while (current_battery_level < 65) {
        ESP_LOGE(TAG, "wait for battery charge. Current value: %ld%%", current_battery_level);
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "init Display regualtor");
    gpio_t* reg_gpio = gpio_create(OUTPUT, 0, EINK_VCC_nEN);
    reg_gpio->onValue = GPIO_RESET;
    regulator_t* reg = regulator_gpio_create(reg_gpio);

    ESP_LOGI(TAG, "reset E-Ink Display");
    reg->disable(reg);
    vTaskDelay(300);
    reg->enable(reg);
    vTaskDelay(10);
    ESP_LOGI(TAG, "init E-Ink Display");
    do {
        eink = ACEP_5IN65_Init(&eink_dev, DISPLAY_ROTATE_90);
        if (!eink) {
            ESP_LOGE(TAG, "E-Ink Display not initialized! retry...");
            reg->disable(reg);
            vTaskDelay(300);
            reg->enable(reg);
            vTaskDelay(10);
        }
    } while (!eink);

    ESP_LOGI(TAG, "App screen init");

#ifndef DEBUG
    ESP_LOGI(TAG, "Screen clear");
    display_fill(eink, WHITE);
    display_commit_fb(eink);
    ESP_LOGI(TAG, "Screen clear done");
#endif

    // TODO: check if we want that here
    create_top_bar(eink);
    ESP_LOGI(TAG, "App screen init done");

    vTaskDelay(100 / portTICK_PERIOD_MS); // ???
    ESP_LOGI(TAG, "Loop ready.");
    trigger_rendering();

    for (;;) {
        if (render_needed) {
            if (xSemaphoreTake(gui_semaphore, 0) == pdTRUE) {
                while (render_needed) {
                    // reset render count. if a renderer triggers a rerender we will directly rerender
                    render_needed = 0;
                    app_screen(eink);
                    if (DEFERRED == app_render())
                        ESP_LOGI(TAG, "rendering got restarted");
                }
                ESP_LOGI(TAG, "Refresh.");
                // vTaskPrioritySet(NULL, 1);
                display_commit_fb(eink);
                // vTaskPrioritySet(NULL, 5);
                ESP_LOGI(TAG, "Refresh finished.");
                xSemaphoreGive(gui_semaphore);
            } else {
                ESP_LOGI(TAG, "Render Mutex locked.");
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
