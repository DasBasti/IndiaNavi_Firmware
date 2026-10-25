/*
 * Off screen component for GUI
 *
 * A picture from the SD card, the hint for the button in the corner where the button is, and a
 * white sheet with the state of the device. The e-ink display keeps the image while the device sleeps.
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui.h"
#include "navi/button.h"
#include "navi/power.h"
#include "navi/safe_print.h"
#include "navi/sd.h"
#include "navi/wifi.h"

#include <esp_mac.h>
#include <esp_random.h>
#include <qrcodegen.h>

#define RANDOM_IMG_NUM 14

static const display_t* dsp;

// created components
static uint8_t* qrcode;
static uint8_t* tempBuffer;
static uint8_t* splash_image_data = NULL;
static image_t* splash;
static char* url;

#define SPLASH_WIDTH 448
#define SPLASH_HEIGHT 600
#define SPLASH_DATA_SIZE (SPLASH_WIDTH * SPLASH_HEIGHT / 2)
#define URL_LENGTH 61

/* the sheet at the bottom of the screen carries all the text, the picture never reaches it */
#define SHEET_HEIGHT 168
#define SHEET_EDGE 4
#define SHEET_MARGIN 16
#define QR_MODULE_SIZE 3 // pixel per QR module
#define QR_QUIET_ZONE 4  // white space around a QR code for scanners
#define LINE_GAP 6
#define LINE_TEXT_LEN 64

/* hint in the corner of the button, the hold time is the same for switching on and off */
#define HINT_TOP 8
static char hint_text[16];

static void off_screen_free(void)
{
    free_all_render_pipelines();

    RTOS_Free(splash);
    splash = NULL;
    RTOS_Free(splash_image_data);
    splash_image_data = NULL;
    RTOS_Free(tempBuffer);
    tempBuffer = NULL;
    RTOS_Free(qrcode);
    qrcode = NULL;
    RTOS_Free(url);
    url = NULL;
}

/* QR code with a white quiet zone around it, the size in pixel is returned */
static int16_t draw_qr(const uint8_t* code, int16_t left, int16_t top)
{
    int16_t modules = qrcodegen_getSize(code);
    int16_t size = modules * QR_MODULE_SIZE;
    display_rect_fill(dsp, left - QR_QUIET_ZONE, top - QR_QUIET_ZONE, size + 2 * QR_QUIET_ZONE,
        size + 2 * QR_QUIET_ZONE, WHITE);
    for (int16_t y = 0; y < modules; y++)
        for (int16_t x = 0; x < modules; x++)
            if (qrcodegen_getModule(code, x, y))
                display_rect_fill(dsp, left + x * QR_MODULE_SIZE, top + y * QR_MODULE_SIZE, QR_MODULE_SIZE,
                    QR_MODULE_SIZE, BLACK);
    return size;
}

/* text centered over a QR code, in the small font */
static void draw_qr_caption(const char* text, int16_t qr_left, int16_t qr_size, int16_t top)
{
    int16_t width = font_text_pixel_width(&f8x8, text);
    display_text_draw(dsp, &f8x8, qr_left + (qr_size - width) / 2, top, text, BLACK);
}

static error_code_t render_button_hint(const display_t* dsp, void* comp)
{
    // while charging the device does not go to sleep again, a short press starts it
    return ui_button_hint(dsp, HINT_TOP, &f8x16, power_is_charging() ? "PRESS" : hint_text, &f8x8, "to start");
}

/*
 * The sheet: QR code of the device page on the left, the state of the device in the middle and,
 * while the access point runs (charging), its QR code on the right.
 */
static error_code_t render_sheet(const display_t* dsp, void* comp)
{
    int16_t sheet_top = dsp->size.height - SHEET_HEIGHT;
    display_rect_fill(dsp, 0, sheet_top, dsp->size.width, SHEET_HEIGHT, WHITE);
    display_rect_fill(dsp, 0, sheet_top, dsp->size.width, SHEET_EDGE, BLACK);

    int16_t content_top = sheet_top + SHEET_EDGE + SHEET_MARGIN / 2;
    int16_t qr_top = content_top + f8x8.height + QR_QUIET_ZONE + 6;
    int16_t text_left = SHEET_MARGIN;
    int16_t text_right = dsp->size.width - SHEET_MARGIN;

    if (qrcode) {
        int16_t qr_left = SHEET_MARGIN + QR_QUIET_ZONE;
        int16_t qr_size = qrcodegen_getSize(qrcode) * QR_MODULE_SIZE;
        draw_qr_caption("Scan me", qr_left, qr_size, content_top);
        draw_qr(qrcode, qr_left, qr_top);
        text_left = qr_left + qr_size + QR_QUIET_ZONE + SHEET_MARGIN;
    }

    const uint8_t* wifi_qr = wifi_ap_running() ? wifi_ap_qrcode() : NULL;
    if (wifi_qr) {
        int16_t qr_size = qrcodegen_getSize(wifi_qr) * QR_MODULE_SIZE;
        int16_t qr_left = dsp->size.width - SHEET_MARGIN - QR_QUIET_ZONE - qr_size;
        draw_qr_caption("Join WiFi", qr_left, qr_size, content_top);
        draw_qr(wifi_qr, qr_left, qr_top);
        text_right = qr_left - QR_QUIET_ZONE - SHEET_MARGIN;
    }

    char line[LINE_TEXT_LEN];
    int16_t y = content_top;
    display_text_draw(dsp, &f8x16, text_left, y, "IndiaNavi", BLACK);
    y += f8x16.height + LINE_GAP;

    bool charging = power_is_charging();
    int32_t level = power_battery_level();
    if (!power_state_known())
        snprintf(line, sizeof(line), "%s", charging ? "Charging" : "Sleeping");
    else if (charging)
        snprintf(line, sizeof(line), "Charging %d%%", (int)level);
    else
        snprintf(line, sizeof(line), "Sleeping %d%%", (int)level);
    display_text_draw(dsp, &f8x16, text_left, y, line, BLACK);
    y += f8x16.height + LINE_GAP;

    // one segment is a tenth of the battery
    ui_segment_bar(dsp, text_left, y, text_right - text_left, 16, power_state_known() && level > 0 ? (uint8_t)(level > 100 ? 100 : level) : 0,
        GREEN);
    y += 16 + LINE_GAP + 2;

    if (wifi_qr) {
        snprintf(line, sizeof(line), "WiFi %s", wifi_ap_ssid());
        display_text_draw(dsp, &f8x8, text_left, y, line, BLACK);
        y += f8x8.height + 2;
        snprintf(line, sizeof(line), "PW   %s", wifi_ap_password());
        display_text_draw(dsp, &f8x8, text_left, y, line, BLACK);
    }

    // version at the bottom edge
    display_text_draw(dsp, &f8x8, SHEET_MARGIN, dsp->size.height - f8x8.height - 4, GIT_HASH, BLACK);
    return PM_OK;
}

void turn_to_on()
{
    // a press while the off screen is still drawn would switch on again before it was seen
    if (!gui_screen_displayed(APP_MODE_TURN_OFF)) {
        ESP_LOGI(__func__, "Off screen is not on the display yet, ignore the press");
        return;
    }
    gui_set_app_mode(APP_MODE_GPS_CREATE);
    // while charging WiFi keeps running, the map screen shows the AP QR code
    if (!power_is_charging())
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
    snprintf(hint_text, sizeof(hint_text), "HOLD %us", (unsigned)(BUTTON_HOLD_MS / 1000));

    /* Create splash screen image component from splash.raw on SD card*/
    if (sd_lock()) {
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
        sd_unlock();
    }

    splash = image_create(splash_image_data, 0, 0, SPLASH_WIDTH, SPLASH_HEIGHT);
    if (splash)
        add_to_render_pipeline(image_render, splash, RL_MAP);

    add_to_render_pipeline(render_sheet, NULL, RL_GUI_ELEMENTS);
    add_to_render_pipeline(render_button_hint, NULL, RL_GUI_ELEMENTS);

    uint8_t derived_mac_addr[6] = { 0 };
    ESP_ERROR_CHECK(esp_read_mac(derived_mac_addr, ESP_MAC_WIFI_STA));
    url = RTOS_Malloc(URL_LENGTH);
    if (url)
        snprintf(url, URL_LENGTH, "https://platinenmacher.tech/navi/?device=%02x%02x%02x%02x%02x%02x",
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

    set_screen_free_function(off_screen_free);
    set_short_press_event(turn_to_on);
}
