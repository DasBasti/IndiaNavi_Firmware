/*
 * Off screen component for GUI
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui.h"
#include "tasks.h"

#include <icons_32.h>

#include <esp_mac.h>
#include <esp_random.h>
#include <qrcodegen.h>

#define RANDOM_IMG_NUM 14

static const display_t* dsp;

// created components
static char* infoText;
static label_t* infoBox;
static image_t* wifi_indicator_image;
static label_t* push_button;
static label_t* qr_label;

static uint8_t *qrcode;
static uint8_t *tempBuffer;
static uint8_t* splash_image_data = NULL;
static image_t* splash;

#define SPLASH_WIDTH 448
#define SPLASH_HEIGHT 600
#define SPLASH_DATA_SIZE (SPLASH_WIDTH * SPLASH_HEIGHT / 2)
static char *url;
#define URL_LENGTH 61

/* QR code with the access point credentials, shown while charging */
#define QR_MODULE_SIZE 3 // pixel per QR module
#define QR_TOP 495
#define WIFI_QR_BOX_WIDTH 160
#define WIFI_QR_TEXT_LINES 2
#define WIFI_QR_TEXT_LEN 64

typedef struct {
    uint8_t* code;
    int16_t left;
    int16_t top;
    bool (*visible)(void);
} qr_view_t;

static qr_view_t url_qr;
static qr_view_t wifi_qr;
static label_t* wifi_qr_label;
static char* wifi_qr_text;

void off_screen_free()
{
    free_all_render_pipelines();

    RTOS_Free(infoText);
    infoText = NULL;
    RTOS_Free(infoBox);
    infoBox = NULL;
    RTOS_Free(push_button);
    push_button = NULL;
    RTOS_Free(qr_label);
    qr_label = NULL;
    RTOS_Free(splash);
    splash = NULL;
    RTOS_Free(splash_image_data);
    splash_image_data = NULL;
    RTOS_Free(wifi_indicator_image);
    wifi_indicator_image = NULL;
    RTOS_Free(tempBuffer);
    tempBuffer = NULL;
    RTOS_Free(qrcode);
    qrcode = NULL;
    RTOS_Free(url);
    url = NULL;
    wifi_qr.code = NULL; // owned by wifi.c
    RTOS_Free(wifi_qr_label);
    wifi_qr_label = NULL;
    RTOS_Free(wifi_qr_text);
    wifi_qr_text = NULL;
}

static char* messages[] = { "Device is sleeping push button to start   ", "Device is charging push button to start   " };

error_code_t render_arrow(const display_t* dsp, void*)
{
    int16_t startx, starty;
    startx = dsp->size.width - 16;
    starty = 4;

    // down arrow
    display_line_draw(dsp, startx, starty, startx + 6, starty + 4, BLACK);
    display_line_draw(dsp, startx + 6, starty + 4, startx + 12, starty + 0, BLACK);

    // button Box
    display_rect_draw(dsp, startx + 2, starty + 8, 12, 18, BLACK);
    display_rect_fill(dsp, startx + 5, starty + 12, 6, 10, RED);
    return PM_OK;
}

// Draws the given QR Code at its position
static error_code_t render_qr(const display_t* dsp, void* comp)
{
    qr_view_t* view = (qr_view_t*)comp;
    if (!view->code || (view->visible && !view->visible()))
        return NOT_NEEDED;
    int size = qrcodegen_getSize(view->code);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            display_rect_fill(dsp, (QR_MODULE_SIZE * x) + view->left, (QR_MODULE_SIZE * y) + view->top,
                QR_MODULE_SIZE, QR_MODULE_SIZE, (qrcodegen_getModule(view->code, x, y) ? BLACK : WHITE));
        }
    }
    return PM_OK;
}

// The access point only runs in charge mode
static bool wifi_qr_visible(void)
{
    return wifi_ap_running();
}

static error_code_t wifi_qr_label_onBeforeRender(const display_t* dsp, void* label)
{
    return wifi_qr_visible() ? PM_OK : ABORT;
}

/*
 * QR code of the access point, phones join the network after scanning it.
 */
static void create_wifi_qr(const display_t* dsp)
{
    wifi_qr.code = (uint8_t*)wifi_ap_qrcode();
    if (!wifi_qr.code)
        return;

    int16_t qr_size = qrcodegen_getSize(wifi_qr.code) * QR_MODULE_SIZE;
    // leave white space between text and QR code for scanners
    int16_t text_height = WIFI_QR_TEXT_LINES * (f8x8.height + 2) + 8;
    int16_t box_left = dsp->size.width - WIFI_QR_BOX_WIDTH - 2;
    wifi_qr.left = box_left + (WIFI_QR_BOX_WIDTH - qr_size) / 2;
    wifi_qr.top = QR_TOP;
    wifi_qr.visible = wifi_qr_visible;

    // SSID and password as text for typing them in by hand
    wifi_qr_text = RTOS_Malloc(WIFI_QR_TEXT_LEN);
    if (wifi_qr_text) {
        snprintf(wifi_qr_text, WIFI_QR_TEXT_LEN, "WiFi %s\nPW   %s", wifi_ap_ssid(), wifi_ap_password());
        wifi_qr_label = label_create(wifi_qr_text, &f8x8, box_left, QR_TOP - text_height,
            WIFI_QR_BOX_WIDTH, text_height + qr_size + 3);
    }
    if (wifi_qr_label) {
        wifi_qr_label->alignVertical = TOP;
        wifi_qr_label->alignHorizontal = LEFT;
        wifi_qr_label->backgroundColor = WHITE;
        wifi_qr_label->onBeforeRender = wifi_qr_label_onBeforeRender;
        add_to_render_pipeline(label_render, wifi_qr_label, RL_GUI_ELEMENTS);
    }
    add_to_render_pipeline(render_qr, &wifi_qr, RL_GUI_ELEMENTS);
}

// Update string on display
error_code_t push_button_label_onBeforeRender(const display_t* dsp, void* label)
{
    label_t* l = (label_t*)label;
    l->text = messages[is_charging ? 1 : 0];
    return PM_OK;
}

// Update wifi icon on display
error_code_t wifi_indicator_image_onBeforeRender(const display_t* dsp, void* image)
{
    image_t* i = (image_t*)image;

    i->data = wifi_indicator_image_data;
    if (!is_charging)
        i->data = NULL;
    return PM_OK;
}

void turn_to_on()
{
    gui_set_app_mode(APP_MODE_GPS_CREATE);
    wifi_request_stop();
    trigger_rendering();
}

void off_screen_create(const display_t* display)
{
    FIL t_img = {0};
    UINT br = 0;
    FILINFO t_img_nfo;
    FRESULT res = FR_NOT_READY;
    char fn[20];
    snprintf(fn, sizeof(fn), "//art%u.raw", (uint8_t)(esp_random() % RANDOM_IMG_NUM) + 1);

    dsp = display;
    size_t infoText_len = dsp->size.width / f8x8.width;
    infoText = RTOS_Malloc(infoText_len);
    if (infoText)
        save_snprintf(infoText, infoText_len, "%s", GIT_HASH);

    /* Create splash screen image component from splash.raw on SD card*/
    waitForSDInit();
    if (sd_semaphore && xSemaphoreTake(sd_semaphore, SD_MUTEX_TIMEOUT)) {
        // Check file info
        res = f_stat((const TCHAR*)fn, &t_img_nfo);
        ESP_LOGI(__func__, "Load image %s is: %d", fn, res);
        // the renderer reads a full screen image, smaller files would be read out of bounds
        if (FR_OK == res && t_img_nfo.fsize >= SPLASH_DATA_SIZE) {
            splash_image_data = RTOS_Malloc(SPLASH_DATA_SIZE);
            ESP_LOGI(__func__, "Load image to: %p", splash_image_data);
            if (splash_image_data) {
                res = f_open(&t_img, (const TCHAR*)fn, FA_READ);
                if (FR_OK == res) {
                    res = f_read(&t_img, splash_image_data, SPLASH_DATA_SIZE, &br);
                    f_close(&t_img);
                }
                if (FR_OK != res || br != SPLASH_DATA_SIZE) {
                    ESP_LOGI(__func__, "Error from SD card: %d", res);
                    RTOS_Free(splash_image_data);
                    splash_image_data = NULL;
                }
            }
        }
        xSemaphoreGive(sd_semaphore);
    }

    splash = image_create(splash_image_data, 0, 0, SPLASH_WIDTH, SPLASH_HEIGHT);
    if (splash)
        add_to_render_pipeline(image_render, splash, RL_MAP);

    infoBox = label_create(infoText, &f8x8, 0, dsp->size.height - 13,
        dsp->size.width, 13);
    infoBox->borderWidth = 1;
    infoBox->borderLines = ALL_SOLID;
    infoBox->alignVertical = MIDDLE;
    infoBox->backgroundColor = WHITE;

    add_to_render_pipeline(label_render, infoBox, RL_GUI_ELEMENTS);

    push_button = label_create(NULL, &f8x16, 0, 0, dsp->size.width, 32);
    push_button->onBeforeRender = push_button_label_onBeforeRender;
    push_button->alignHorizontal = RIGHT;
    push_button->alignVertical = BOTTOM;
    push_button->backgroundColor = WHITE;

    add_to_render_pipeline(label_render, push_button, RL_GUI_ELEMENTS);

    add_to_render_pipeline(render_arrow, NULL, RL_GUI_ELEMENTS);

    uint8_t derived_mac_addr[6] = { 0 };
    ESP_ERROR_CHECK(esp_read_mac(derived_mac_addr, ESP_MAC_WIFI_STA));
    url = RTOS_Malloc(URL_LENGTH);
    if (url)
        snprintf(url, URL_LENGTH, "https://platinenmacher.tech/navi/?device=%x%x%x%x%x%x",
            derived_mac_addr[0], derived_mac_addr[1], derived_mac_addr[2],
            derived_mac_addr[3], derived_mac_addr[4], derived_mac_addr[5]);
    tempBuffer = RTOS_Malloc(qrcodegen_BUFFER_LEN_MAX);
    qrcode = RTOS_Malloc(qrcodegen_BUFFER_LEN_MAX);
    if (!url || !tempBuffer || !qrcode
        || !qrcodegen_encodeText(url, tempBuffer, qrcode, qrcodegen_Ecc_LOW,
            qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
        ESP_LOGE(__func__, "QR code not created");
        RTOS_Free(qrcode);
        qrcode = NULL;
    }

    if (qrcode) {
        url_qr.code = qrcode;
        url_qr.left = 5;
        url_qr.top = QR_TOP;
        url_qr.visible = NULL;
        qr_label = label_create("Scan me", &f8x8, 2, QR_TOP - 13,
            qrcodegen_getSize(qrcode) * 3 + 6, 13 + qrcodegen_getSize(qrcode) * 3 + 3);
        qr_label->alignVertical = TOP;
        qr_label->alignHorizontal = CENTER;
        qr_label->backgroundColor = WHITE;
        add_to_render_pipeline(label_render, qr_label, RL_GUI_ELEMENTS);
        add_to_render_pipeline(render_qr, &url_qr, RL_GUI_ELEMENTS);
    }

    create_wifi_qr(dsp);

    wifi_indicator_image = image_create(WIFI_0, 3, 0, 32, 32);
    wifi_indicator_image->onBeforeRender = wifi_indicator_image_onBeforeRender;
    add_to_render_pipeline(image_render, wifi_indicator_image, RL_GUI_ELEMENTS);

    set_screen_free_function(off_screen_free);
    set_short_press_event(turn_to_on);
}