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

// Prints the given QR Code to the console.
static error_code_t render_qr(const display_t* dsp, void* comp)
{
    uint8_t* qrcode = (uint8_t*)comp;
    int size = qrcodegen_getSize(qrcode);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            display_rect_fill(dsp, (3 * x) + 5, (3 * y) + 495, 3, 3, (qrcodegen_getModule(qrcode, x, y) ? BLACK : WHITE));
        }
    }
    return PM_OK;
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
        qr_label = label_create("Scan me", &f8x8, 2, 495 - 13,
            qrcodegen_getSize(qrcode) * 3 + 6, 13 + qrcodegen_getSize(qrcode) * 3 + 3);
        qr_label->alignVertical = TOP;
        qr_label->alignHorizontal = CENTER;
        qr_label->backgroundColor = WHITE;
        add_to_render_pipeline(label_render, qr_label, RL_GUI_ELEMENTS);
        add_to_render_pipeline(render_qr, qrcode, RL_GUI_ELEMENTS);
    }

    wifi_indicator_image = image_create(WIFI_0, 3, 0, 32, 32);
    wifi_indicator_image->onBeforeRender = wifi_indicator_image_onBeforeRender;
    add_to_render_pipeline(image_render, wifi_indicator_image, RL_GUI_ELEMENTS);

    set_screen_free_function(off_screen_free);
    set_short_press_event(turn_to_on);
}