/*
 * Off screen component for GUI
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui.h"
#include "tasks.h"

static const display_t* dsp;
static const char* fn = "//lost.raw";
static uint8_t* splash_image_data;
image_t* splash;

void picture_set_image_path(const char *path)
{
    fn = path;
}

void picture_screen_create(const display_t* display)
{
    FIL t_img;
    uint32_t br;
    FILINFO t_img_nfo;
    FRESULT res = FR_NOT_READY;
    
    dsp = display;

    /* Create splash screen image component from splash.raw on SD card*/
    waitForSDInit();
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

    splash = image_create(splash_image_data, 0, 0, 448, 600);

    add_to_render_pipeline(image_render, splash, RL_MAP);

}

void picture_screen_free()
{
    free_all_render_pipelines();
    RTOS_Free(splash);
    RTOS_Free(splash_image_data);
}