/*
 * Off screen component for GUI
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui.h"
#include "tasks.h"

#include <esp_mac.h>
#include <qrcodegen.h>

static const display_t* dsp;
static char* infoText;
static label_t* infoBox;
static uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
static uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];

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
            display_rect_fill(dsp, (3 * x)+5, (3 * y)+380, 3, 3, (qrcodegen_getModule(qrcode, x, y) ? BLACK : WHITE));
        }
    }
    return PM_OK;
}

void off_screen_create(const display_t* display)
{
    FIL t_img;
    uint32_t br;
    FILINFO t_img_nfo;
    uint8_t* splash_image_data = NULL;
    FRESULT res = FR_NOT_READY;
    char* fn = "//splash.raw";

    dsp = display;
    infoText = RTOS_Malloc(dsp->size.width / f8x8.width);
    xSemaphoreTake(print_semaphore, portMAX_DELAY);
    sprintf(infoText, GIT_HASH);
    xSemaphoreGive(print_semaphore);

    /* Create splash screen image component from splash.raw on SD card*/
    if (xSemaphoreTake(sd_semaphore, pdTICKS_TO_MS(1000))) {
        // Check file info
        res = f_stat((const TCHAR*)fn, &t_img_nfo);
        ESP_LOGI(__func__, "Load image %s is: %d (%ld)", fn, res, t_img_nfo.fsize);
        if (FR_OK == res) {
            // Allocate file size
            splash_image_data = RTOS_Malloc(t_img_nfo.fsize);
        }
        ESP_LOGI(__func__, "Load image to: %p", splash_image_data);
        res = f_open(&t_img, (const TCHAR*)fn, FA_READ);
        ESP_LOGI(__func__, "Image is opened %d: %p", res, splash_image_data);
        if (FR_OK == res && splash_image_data != 0) {
            res = f_read(&t_img,
                splash_image_data, t_img_nfo.fsize,
                (UINT*)&br);
            f_close(&t_img);
        } else {
            ESP_LOGI(__func__, "Error from SD card: %d", res);
        }
        xSemaphoreGive(sd_semaphore);
    }

    image_t* splash = image_create(splash_image_data, 0, 0, 448, 600);

    add_to_render_pipeline(image_render, splash, RL_MAP);

    infoBox = label_create(infoText, &f8x8, 0, dsp->size.height - 14,
        dsp->size.width - 1, 13);
    infoBox->borderWidth = 1;
    infoBox->borderLines = ALL_SOLID;
    infoBox->alignVertical = MIDDLE;
    infoBox->backgroundColor = WHITE;

    add_to_render_pipeline(label_render, infoBox, RL_GUI_ELEMENTS);

    label_t* push_button = label_create("Device is sleeping push button to start", &f8x16, 0, 16, dsp->size.width - 24, 16);
    push_button->alignHorizontal = RIGHT;

    add_to_render_pipeline(label_render, push_button, RL_GUI_ELEMENTS);

    add_to_render_pipeline(render_arrow, NULL, RL_GUI_ELEMENTS);

    uint8_t derived_mac_addr[6] = { 0 };
    ESP_ERROR_CHECK(esp_read_mac(derived_mac_addr, ESP_MAC_WIFI_STA));
    char url[61] = { 0 };
    snprintf(url, sizeof(url), "https://platinenmacher.tech/navi/?device=%x%x%x%x%x%x",
        derived_mac_addr[0], derived_mac_addr[1], derived_mac_addr[2],
        derived_mac_addr[3], derived_mac_addr[4], derived_mac_addr[5]);
    ESP_ERROR_CHECK(qrcodegen_encodeText(url, tempBuffer, qrcode, qrcodegen_Ecc_LOW,
                        qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)
            ? ESP_OK
            : ESP_FAIL);

    add_to_render_pipeline(render_qr, qrcode, RL_GUI_ELEMENTS);
    label_t *qr_label = label_create("Scan for Track", &f8x8, 5, 365,
        dsp->size.width - 1, 13);
    qr_label->alignVertical = MIDDLE;
    add_to_render_pipeline(label_render, qr_label, RL_GUI_ELEMENTS);

}

void off_screen_free()
{
    free_all_render_pipelines();
    RTOS_Free(infoText);
    RTOS_Free(infoBox);
}