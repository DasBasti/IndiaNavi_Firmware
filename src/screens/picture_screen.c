/*
 * Off screen component for GUI
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui.h"
#include "navi/sd.h"

static const display_t* dsp;
static const char* fn = "//lost.raw";

static uint8_t* splash_image_data;
image_t* splash;

#define SPLASH_WIDTH 448
#define SPLASH_HEIGHT 600
#define SPLASH_DATA_SIZE (SPLASH_WIDTH * SPLASH_HEIGHT / 2)

void picture_screen_free()
{
    free_all_render_pipelines();
    RTOS_Free(splash);
    splash = NULL;
    RTOS_Free(splash_image_data);
    splash_image_data = NULL;
}

void picture_set_image_path(const char *path)
{
    fn = path;
}

void picture_screen_create(const display_t* display)
{
    FIL t_img = {0};
    UINT br = 0;
    FILINFO t_img_nfo;
    FRESULT res = FR_NOT_READY;
    
    dsp = display;

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

    set_screen_free_function(picture_screen_free);
}

