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

#include <icons_16.h>
#include <qrcodegen.h>

#define GPS_VIEW_STRLEN 5 // number of satellites
#define CLOCK_STRLEN 6    // "hh:mm"

#ifndef INITIAL_APP_MODE
#    define INITIAL_APP_MODE APP_MODE_TURN_OFF
#endif

const uint16_t margin_top = 5;
const uint16_t margin_bottom = 5;
const uint16_t margin_vertical = 10;
const uint16_t margin_left = 5;
const uint16_t margin_right = 5;
const uint16_t margin_horizontal = 10;

static const char* TAG = "GUI";

static display_t* eink;
static regulator_t* eink_reg; // supply of the display, switching it resets the controller

extern void vTaskGetRunTimeStats(char* pcWriteBuffer);

render_t* render_pipeline[RL_MAX]; // maximum number of rendered items
render_t* render_last[RL_MAX];     // pointer to end of render pipeline
static volatile uint8_t render_needed = 0;

app_mode_t _app_mode = INITIAL_APP_MODE;
static volatile app_mode_t current_screen = APP_MODE_NONE; // screen that is shown
font_t f8x8, f8x16;
label_t* clock_label;
battery_indicator_t* battery_indicator;
label_t* north_indicator_label;
label_t* wifi_indicator_label;
label_t* gps_indicator_label;
label_t* sd_indicator_label;

error_code_t (*_post_render_hook)(size_t arg);
size_t _post_rener_hook_arg;

void (*free_screen_func)(void);

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
    rd->next = NULL;

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
    render_pipeline[layer] = NULL;
    render_last[layer] = NULL;
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
    if (!img)
        return NULL;

    label_t* il = label_create(text, font,
        img->box.left + img->box.width + margin_left, top, 0, 0);
    if (!il) {
        RTOS_Free(img);
        return NULL;
    }
    il->child = img;
    label_shrink_to_text(il);
    il->alignVertical = MIDDLE;
    return il;
}

/**
 * Callbacks from renderer for clock label
 */
error_code_t updateTimeText(const display_t* dsp, void* comp)
{
    struct timeval tv;
    struct tm timeinfo;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &timeinfo);
    xSemaphoreTake(print_semaphore, portMAX_DELAY);
    snprintf(clock_label->text, CLOCK_STRLEN, "%02d:%02d", timeinfo.tm_hour,
        timeinfo.tm_min);
    xSemaphoreGive(print_semaphore);
    return PM_OK;
}

static int sprint_battery_percent(char* buffer, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    save_vsnprintf(buffer, BATTERY_CHARGE_STRBUF, format, args);
    va_end(args);
    return 0;
}

static label_t* top_bar;

/*
 * Show the access point icon while phones can join the WiFi of the device
 */
static error_code_t wifi_ap_icon_onBeforeRender(const display_t* dsp, void* image)
{
    ((image_t*)image)->data = wifi_ap_running() ? WIFI_AP : NULL;
    return PM_OK;
}

/*
 * Add the icon label and its image to the render pipeline
 */
static void add_icon_with_text_to_pipeline(label_t* il)
{
    if (!il)
        return;
    add_to_render_pipeline(label_render, il, RL_GUI_ELEMENTS);
    // render image after Label is rendered
    add_to_render_pipeline(image_render, il->child, RL_GUI_ELEMENTS);
}

/*
 * Create the top bar components once. The components are shared with other
 * tasks (battery, SD, GPS status) and therefore never freed.
 */
static error_code_t create_top_bar_components(const display_t* dsp)
{
    if (top_bar)
        return PM_OK;

    label_t* sb = label_create("", &f8x8, 0, 0, dsp->size.width,
        ICON_SIZE + margin_vertical);
    if (!sb)
        return PM_FAIL;

    sb->borderColor = BLACK;
    sb->borderWidth = 1;
    sb->borderLines = ALL_SOLID;
    sb->alignHorizontal = CENTER;
    sb->alignVertical = MIDDLE;
    sb->backgroundColor = WHITE;

    battery_indicator_t* bat = create_battery_indicator(sb->box.left + margin_left, margin_top, current_battery_level, is_charging, &f8x8, batlevels, batlevel_images, batlevel_num);
    if (!bat) {
        RTOS_Free(sb);
        return PM_FAIL;
    }
    bat->save_printf = sprint_battery_percent;
    save_sprintf(bat->label_text, "...%%");
    label_shrink_to_text(&bat->label);

    north_indicator_label = create_icon_with_text(dsp, norden,
        bat->label.box.left + bat->label.box.width + margin_horizontal,
        margin_top, "", &f8x8);

    if (north_indicator_label) {
        wifi_indicator_label = create_icon_with_text(dsp, WIFI_AP,
            north_indicator_label->box.left + north_indicator_label->box.width + margin_horizontal,
            margin_top, "", &f8x8);
        if (wifi_indicator_label)
            ((image_t*)wifi_indicator_label->child)->onBeforeRender = wifi_ap_icon_onBeforeRender;
    }

    char* GPSView = RTOS_Malloc(GPS_VIEW_STRLEN);
    gps_indicator_label = create_icon_with_text(dsp, noGPS,
        dsp->size.width - ICON_SIZE - (2 * margin_right) - 16, margin_top, GPSView, &f8x8);

    if (gps_indicator_label)
        sd_indicator_label = create_icon_with_text(dsp, noSD,
            gps_indicator_label->box.left - 2 * ICON_SIZE - margin_right, margin_top, "",
            &f8x8);

#ifdef CLOCK
    /* global clock label. */
    char* time = RTOS_Malloc(CLOCK_STRLEN);
    if (time) {
        clock_label = label_create(time, &f8x8, sb->box.left, sb->box.top,
            sb->box.width, sb->box.height);
        if (clock_label) {
            clock_label->alignVertical = MIDDLE;
            clock_label->alignHorizontal = CENTER;
            clock_label->onBeforeRender = updateTimeText;
        } else {
            RTOS_Free(time);
        }
    }
#endif
    battery_indicator = bat;
    top_bar = sb;
    return PM_OK;
}

static void create_top_bar(const display_t* dsp)
{
    if (create_top_bar_components(dsp) != PM_OK) {
        ESP_LOGE(TAG, "Can not create top bar");
        return;
    }

    add_to_render_pipeline(label_render, top_bar, RL_GUI_BACKGROUND);
    add_to_render_pipeline(label_render, &battery_indicator->label, RL_GUI_ELEMENTS);
    // render image after Label is rendered
    add_to_render_pipeline(image_render, &battery_indicator->image, RL_GUI_ELEMENTS);
    add_icon_with_text_to_pipeline(north_indicator_label);
    add_icon_with_text_to_pipeline(wifi_indicator_label);
    add_icon_with_text_to_pipeline(gps_indicator_label);
    add_icon_with_text_to_pipeline(sd_indicator_label);
#ifdef CLOCK
    if (clock_label)
        add_to_render_pipeline(label_render, clock_label, RL_GUI_ELEMENTS);
#endif
}

#define PROGRESS_BOX_TOP (ICON_SIZE + margin_vertical + 10) // below the top bar
#define PROGRESS_BOX_HEIGHT 44
#define PROGRESS_TEXT_LEN 48

/* result of the transfer drawn in the refresh that is committed next */
static upload_state_t upload_result_drawn = UPLOAD_IDLE;

/*
 * The progress box is shown while files come in. The result ("complete",
 * "stopped") only in one refresh.
 */
static bool upload_progress_visible(const upload_progress_t* p)
{
    if (p->files_total == 0)
        return false;
    if (p->state == UPLOAD_ACTIVE)
        return true;
    return (p->state == UPLOAD_DONE || p->state == UPLOAD_ABORTED) && !p->result_shown;
}

/**
 * Draw upload progress on top of every screen while the app transfers files
 */
static void render_upload_progress(const display_t* dsp)
{
    upload_progress_t p = upload_get_progress();
    upload_result_drawn = UPLOAD_IDLE;
    if (!upload_progress_visible(&p))
        return;
    if (p.state != UPLOAD_ACTIVE)
        upload_result_drawn = p.state;

    char text[PROGRESS_TEXT_LEN];
    color_t bar_color = GREEN;
    switch (p.state) {
    case UPLOAD_ACTIVE:
        snprintf(text, sizeof(text), "Upload %lu / %lu files", (unsigned long)p.files_done, (unsigned long)p.files_total);
        break;
    case UPLOAD_DONE:
        snprintf(text, sizeof(text), "Upload complete: %lu files", (unsigned long)p.files_total);
        break;
    default:
        snprintf(text, sizeof(text), "Upload stopped: %lu / %lu files", (unsigned long)p.files_done, (unsigned long)p.files_total);
        bar_color = RED;
        break;
    }

    int16_t left = margin_left;
    int16_t width = dsp->size.width - margin_horizontal;
    int16_t top = PROGRESS_BOX_TOP;
    display_rect_fill(dsp, left, top, width, PROGRESS_BOX_HEIGHT, WHITE);
    display_rect_draw(dsp, left, top, width, PROGRESS_BOX_HEIGHT, BLACK);

    int16_t text_width = font_text_pixel_width(&f8x16, text);
    display_text_draw(dsp, &f8x16, left + (width - text_width) / 2, top + 4, text, BLACK);

    // progress bar below the text
    int16_t bar_left = left + margin_left;
    int16_t bar_top = top + 26;
    int16_t bar_width = width - margin_horizontal;
    int16_t bar_height = 12;
    uint32_t done = p.files_done > p.files_total ? p.files_total : p.files_done;
    display_rect_draw(dsp, bar_left, bar_top, bar_width, bar_height, BLACK);
    display_rect_fill(dsp, bar_left + 1, bar_top + 1,
        (uint32_t)(bar_width - 2) * done / p.files_total, bar_height - 2, bar_color);
}

#define WIFI_BOX_WIDTH 160
#define WIFI_BOX_BOTTOM_GAP 57 // keep height graph and copyright of the map visible
#define WIFI_QR_MODULE_SIZE 2  // pixel per QR module
#define WIFI_QR_QUIET_ZONE 8   // white space around the QR code for scanners
#define WIFI_TEXT_LEN 32

/**
 * Small QR code with the access point credentials while nobody is connected.
 *
 * The charging screen shows its own larger QR code.
 */
static void render_wifi_qr(const display_t* dsp)
{
    if (!wifi_ap_running() || wifi_ap_station_count() || current_screen == APP_MODE_TURN_OFF)
        return;
    upload_progress_t p = upload_get_progress();
    if (upload_progress_visible(&p))
        return;
    const uint8_t* qr = wifi_ap_qrcode();
    if (!qr)
        return;

    int16_t modules = qrcodegen_getSize(qr);
    int16_t qr_size = modules * WIFI_QR_MODULE_SIZE;
    int16_t line_height = f8x8.height + 2;
    int16_t box_height = 3 + 2 * line_height + WIFI_QR_QUIET_ZONE + qr_size + WIFI_QR_QUIET_ZONE;
    int16_t left = dsp->size.width - WIFI_BOX_WIDTH - margin_right;
    int16_t top = dsp->size.height - WIFI_BOX_BOTTOM_GAP - box_height;

    display_rect_fill(dsp, left, top, WIFI_BOX_WIDTH, box_height, WHITE);
    display_rect_draw(dsp, left, top, WIFI_BOX_WIDTH, box_height, BLACK);

    char line[WIFI_TEXT_LEN];
    snprintf(line, sizeof(line), "WiFi %s", wifi_ap_ssid());
    display_text_draw(dsp, &f8x8, left + 4, top + 3, line, BLACK);
    snprintf(line, sizeof(line), "PW   %s", wifi_ap_password());
    display_text_draw(dsp, &f8x8, left + 4, top + 3 + line_height, line, BLACK);

    int16_t qr_left = left + (WIFI_BOX_WIDTH - qr_size) / 2;
    int16_t qr_top = top + 3 + 2 * line_height + WIFI_QR_QUIET_ZONE;
    for (int16_t y = 0; y < modules; y++)
        for (int16_t x = 0; x < modules; x++)
            if (qrcodegen_getModule(qr, x, y))
                display_rect_fill(dsp, qr_left + x * WIFI_QR_MODULE_SIZE, qr_top + y * WIFI_QR_MODULE_SIZE,
                    WIFI_QR_MODULE_SIZE, WIFI_QR_MODULE_SIZE, BLACK);
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
    render_upload_progress(eink);
    render_wifi_qr(eink);

    uint64_t end = esp_timer_get_time();

    ESP_LOGI(TAG, "render time %lu ms", (uint32_t)(end - start) / 1000);

    return PM_OK;
}

/**
 * Set App mode
 */
error_code_t gui_set_app_mode(app_mode_t mode)
{
    // If we do not change mode we do not need to rerender
    if (mode == _app_mode)
        return NOT_NEEDED;
    _app_mode = mode;
    render_needed = 1;
    return PM_OK;
}

/**
 * Run after rendering and displaying is finished
 */
void run_post_render_hook(void)
{
    if (_post_render_hook)
        if (_post_render_hook(_post_rener_hook_arg) == PM_OK)
            _post_render_hook = 0;
}

/**
 * Set post rendering hook
 */
void set_post_rendering_hook(error_code_t (*cb)(size_t arg), size_t arg)
{
    _post_rener_hook_arg = arg;
    _post_render_hook = cb;
}

static error_code_t start_screen_transition_hook(size_t arg)
{
    (void)arg;
    gui_set_app_mode(APP_START_SCREEN_TRANSITION);
    return PM_OK;
}

static error_code_t deep_sleep_post_render_hook(size_t arg)
{
    (void)arg;
    return enter_deep_sleep_if_not_charging();
}

/**
 * Free the current screen and remove all components from the render pipelines
 */
void free_screen(void)
{
    void (*func)(void) = free_screen_func;
    // clear first, a screen must never be freed twice
    free_screen_func = NULL;
    if (func)
        func();
    free_all_render_pipelines();
}
/**
 * Set screen free function
 */
void set_screen_free_function(void (*free_screen_cb)(void))
{
    free_screen_func = free_screen_cb;
}

/**
 * Display App
 */
void app_screen(const display_t* dsp)
{
    switch (_app_mode) {
    case APP_START_SCREEN:
        free_screen();
        current_screen = APP_START_SCREEN;
        picture_screen_create(dsp);
        set_post_rendering_hook(start_screen_transition_hook, 0);
        break;
    case APP_TEST_SCREEN:
        free_screen();
        current_screen = APP_TEST_SCREEN;
        create_top_bar(dsp);
        test_screen_create(dsp);
        gui_set_app_mode(APP_MODE_RUNNING);
        break;
    case APP_START_SCREEN_TRANSITION:
        if (!gps_is_position_known())
            break;
        /* free start screen and fall throught to map screen generation*/
        free_screen();
        gui_set_app_mode(APP_MODE_GPS_CREATE);
        __attribute__((fallthrough));
    case APP_MODE_GPS_CREATE:
        free_screen();
        current_screen = APP_MODE_GPS_CREATE;
        create_top_bar(dsp);
        map_screen_create(dsp);
        gui_set_app_mode(APP_MODE_RUNNING);
        break;
    case APP_MODE_TURN_OFF:
        free_screen();
        current_screen = APP_MODE_TURN_OFF;
        off_screen_create(dsp);
        set_post_rendering_hook(deep_sleep_post_render_hook, 0);
        gps_enter_standby();
        ESP_LOGI(TAG, "Starting Power Down Mode");
        gui_set_app_mode(APP_MODE_RUNNING);
        __attribute__((fallthrough));
    case APP_MODE_RUNNING:
    default:
        break;
    }
}

/**
 * Load track and map again if the map screen is shown.
 *
 * On other screens nothing is needed, the map screen loads the track when it is created.
 */
void gui_reload_track(void)
{
    if (current_screen == APP_MODE_GPS_CREATE)
        gui_set_app_mode(APP_MODE_GPS_CREATE);
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

/**
 * A controller that did not get ready ignores commands and shifts the next
 * image. Switch its supply off and on and send the register setup again.
 */
static void recover_display_if_needed(void)
{
    if (!ACEP_5IN65_NeedsRecovery() || !eink_reg)
        return;

    ESP_LOGW(TAG, "E-Ink display did not respond, reset it");
    eink_reg->disable(eink_reg);
    vTaskDelay(pdMS_TO_TICKS(3000));
    eink_reg->enable(eink_reg);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (ACEP_5IN65_Recover() == PM_OK) {
        ESP_LOGI(TAG, "E-Ink display reset done");
        trigger_rendering(); // show the image that was lost
    } else {
        ESP_LOGE(TAG, "E-Ink display reset failed, retry after the next refresh");
    }
}

/* battery level in percent needed to start the display without a charger */
#define DISPLAY_MIN_BATTERY_LEVEL 65

void StartGuiTask(void const* argument)
{
    ESP_LOGI(TAG, "init");
    ESP_LOGI(TAG, "fonts loading");
    font_load_from_array(&f8x8, font8x8, font8x8_name);
    font_load_from_array(&f8x16, font8x16, font8x16_name);

    // gpio_t *eeprom = gpio_create(OUTPUT, 0, EINK_EEPROM_nEN);
    // eeprom->onValue = GPIO_RESET;
    // gpio_write(eeprom, GPIO_SET);

    // an e-ink refresh on a low battery can cause a brown-out. With a charger
    // connected there is enough power, so the charge screen is shown.
    for (uint32_t i = 0; current_battery_level < DISPLAY_MIN_BATTERY_LEVEL && !is_charging; i++) {
        if (i % 30 == 0)
            ESP_LOGE(TAG, "wait for battery charge or charger. Current value: %ld%%", current_battery_level);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "init Display regualtor");
    gpio_t* reg_gpio = gpio_create(OUTPUT, 0, EINK_VCC_nEN);
    reg_gpio->onValue = GPIO_RESET;
    regulator_t* reg = regulator_gpio_create(reg_gpio);
    eink_reg = reg;

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
                // the transfer result was on the display, do not draw it again.
                // A refresh that timed out did not show it, it is drawn again after the reset.
                if (upload_result_drawn != UPLOAD_IDLE && !ACEP_5IN65_NeedsRecovery()) {
                    upload_progress_result_shown(upload_result_drawn);
                    upload_result_drawn = UPLOAD_IDLE;
                }
                recover_display_if_needed();
                // vTaskPrioritySet(NULL, 5);
                ESP_LOGI(TAG, "Refresh finished.");
                run_post_render_hook();
                xSemaphoreGive(gui_semaphore);
            } else {
                ESP_LOGI(TAG, "Render Mutex locked.");
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
