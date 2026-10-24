/*
 * Vector map, see navi/vector_map.h
 *
 * SPDX-License-Identifier: MIT
 */

#include "navi/vector_map.h"

#ifdef VECTOR_MAP

#include "esp_log.h"
#include "esp_timer.h"

#include <ff.h>

#include "mapsforge.h"
#include "mfr_raster.h"
#include "navi/sd.h"

#define MAP_FILE "//MAPS/map.map"

static const char* TAG = "vector_map";

static ssize_t io_read(void* ctx, uint8_t* buf, size_t len)
{
    UINT read = 0;
    if (f_read((FIL*)ctx, buf, len, &read) != FR_OK)
        return -1;
    return read;
}

static int io_seek(void* ctx, uint64_t offset)
{
    return f_lseek((FIL*)ctx, offset) == FR_OK ? 0 : -1;
}

/* a horizontal run of pixels, the primitive all other shapes are drawn with */
static void span(void* ctx, int y, int x0, int x1, uint32_t color)
{
    display_rect_fill((const display_t*)ctx, x0, y, x1 - x0 + 1, 1, (uint8_t)color);
}

static void draw_fill_polygon(void* ctx, const mfr_point_t* points, const uint32_t* ring_lengths,
    uint32_t ring_count, uint32_t color)
{
    mfr_raster_fill_polygon((const mfr_raster_t*)ctx, points, ring_lengths, ring_count, color);
}

static void draw_polyline(void* ctx, const mfr_point_t* points, uint32_t count, uint32_t color, uint8_t width)
{
    mfr_raster_polyline((const mfr_raster_t*)ctx, points, count, color, width);
}

static void draw_point(void* ctx, mfr_point_t at, uint32_t color, uint8_t radius)
{
    mfr_raster_disc((const mfr_raster_t*)ctx, at.x, at.y, radius, color);
}

bool vector_map_available(void)
{
    if (!sd_lock())
        return false;
    FILINFO info;
    bool exists = f_stat(MAP_FILE, &info) == FR_OK;
    sd_unlock();
    return exists;
}

error_code_t vector_map_render(const display_t* dsp, double lat, double lon, uint8_t zoom,
    int16_t anchor_x, int16_t anchor_y)
{
    if (!sd_lock()) {
        ESP_LOGW(TAG, "SD card is busy");
        return UNAVAILABLE;
    }

    int64_t start = esp_timer_get_time();
    error_code_t result = PM_OK;
    FIL file;
    if (f_open(&file, MAP_FILE, FA_READ) != FR_OK) {
        ESP_LOGE(TAG, "Can not open %s", MAP_FILE);
        sd_unlock();
        return PM_FAIL;
    }

    mfr_io_t io = { .ctx = &file, .read = io_read, .seek = io_seek };
    int err = MFR_OK;
    mfr_map_t* map = mfr_open(&io, &err);
    if (map) {
        mfr_raster_t raster = { .ctx = (void*)dsp, .span = span, .width = dsp->size.width, .height = dsp->size.height };
        mfr_draw_t draw = {
            .ctx = &raster,
            .fill_polygon = draw_fill_polygon,
            .polyline = draw_polyline,
            .point = draw_point,
        };
        display_fill(dsp, WHITE);
        int tiles = mfr_render(map, lat, lon, zoom, dsp->size.width, dsp->size.height, anchor_x, anchor_y,
            MFR_PALETTE_EINK7, &draw);
        if (tiles < 0) {
            ESP_LOGE(TAG, "Render failed: %d", tiles);
            result = PM_FAIL;
        } else {
            ESP_LOGI(TAG, "Rendered %d tiles in %lld ms", tiles, (esp_timer_get_time() - start) / 1000);
        }
        mfr_close(map);
    } else {
        ESP_LOGE(TAG, "Can not read map: %d", err);
        result = PM_FAIL;
    }

    f_close(&file);
    sd_unlock();
    return result;
}

#else /* VECTOR_MAP */

bool vector_map_available(void)
{
    return false;
}

error_code_t vector_map_render(const display_t* dsp, double lat, double lon, uint8_t zoom,
    int16_t anchor_x, int16_t anchor_y)
{
    return UNAVAILABLE;
}

#endif
