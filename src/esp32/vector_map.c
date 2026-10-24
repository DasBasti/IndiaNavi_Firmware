/*
 * Vector map, see navi/vector_map.h
 *
 * SPDX-License-Identifier: MIT
 */

#include "navi/vector_map.h"

#ifdef VECTOR_MAP

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include <ff.h>

#include "esp_heap_caps.h"

#include "mapsforge.h"
#include "mfr_raster.h"
#include "navi/sd.h"

#define MAP_FILE "//MAPS/map.map"

static const char* TAG = "vector_map";

/* heap of the library: PSRAM, the internal RAM is needed for DMA buffers (SD card) */
void* mfr_alloc(size_t size)
{
    void* p = heap_caps_aligned_alloc(8, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_aligned_alloc(8, size, MALLOC_CAP_8BIT);
}

void mfr_free(void* ptr)
{
    heap_caps_free(ptr);
}

#define IO_BOUNCE_SIZE 2048

typedef struct {
    uint32_t reads, seeks;
    int64_t read_us, seek_us;
} io_stats_t;
static io_stats_t stats;

/*
 * Cluster link map of the map file. Seeking in a file of several GB walks its cluster chain from
 * the start (about 70 ms per seek), with a link map a seek is a table lookup. Building it walks the
 * chain once (5 s for 3 GB on FAT32), so it is kept and reused as long as the file starts at the
 * same cluster and has the same size.
 */
static DWORD* clmt;
static size_t clmt_size; // in DWORDs
static bool clmt_valid;
static uint32_t clmt_start_cluster;
static FSIZE_t clmt_file_size;

typedef struct {
    FIL file;
    uint8_t* bounce; // internal DMA capable memory, the SD card can not read into PSRAM without a copy
} map_io_t;

static ssize_t io_read(void* ctx, uint8_t* buf, size_t len)
{
    map_io_t* io = ctx;
    if (len > IO_BOUNCE_SIZE)
        len = IO_BOUNCE_SIZE;
    UINT read = 0;
    int64_t t = esp_timer_get_time();
    FRESULT res = f_read(&io->file, io->bounce, len, &read);
    stats.read_us += esp_timer_get_time() - t;
    stats.reads++;
    if (res != FR_OK)
        return -1;
    memcpy(buf, io->bounce, read);
    return read;
}

static int io_seek(void* ctx, uint64_t offset)
{
    int64_t t = esp_timer_get_time();
    FRESULT res = f_lseek(&((map_io_t*)ctx)->file, offset);
    stats.seek_us += esp_timer_get_time() - t;
    stats.seeks++;
    return res == FR_OK ? 0 : -1;
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

static void prepare_fast_seek(FIL* file)
{
    int64_t start = esp_timer_get_time();
    if (clmt_valid && clmt_start_cluster == file->obj.sclust && clmt_file_size == f_size(file)) {
        file->cltbl = clmt;
        return;
    }
    clmt_valid = false;
    if (!clmt) {
        clmt_size = 64;
        clmt = mfr_alloc(clmt_size * sizeof(DWORD));
    }
    for (int attempt = 0; clmt && attempt < 2; attempt++) {
        file->cltbl = clmt;
        clmt[0] = clmt_size;
        FRESULT res = f_lseek(file, CREATE_LINKMAP);
        if (res == FR_OK) {
            ESP_LOGI(TAG, "Link map of %lu entries built in %lld ms", (unsigned long)clmt[0], (esp_timer_get_time() - start) / 1000);
            f_lseek(file, 0);
            clmt_valid = true;
            clmt_start_cluster = file->obj.sclust;
            clmt_file_size = f_size(file);
            return;
        }
        if (res != FR_NOT_ENOUGH_CORE)
            break;
        // clmt[0] is the number of DWORDs needed
        size_t needed = clmt[0];
        mfr_free(clmt);
        clmt_size = needed;
        clmt = mfr_alloc(clmt_size * sizeof(DWORD));
    }
    ESP_LOGW(TAG, "No link map, seeks are slow");
    file->cltbl = NULL;
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
    memset(&stats, 0, sizeof(stats));
    error_code_t result = PM_OK;
    map_io_t* map_io = heap_caps_calloc(1, sizeof(map_io_t), MALLOC_CAP_8BIT);
    if (map_io)
        map_io->bounce = heap_caps_malloc(IO_BOUNCE_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!map_io || !map_io->bounce) {
        ESP_LOGE(TAG, "Out of memory");
        heap_caps_free(map_io);
        sd_unlock();
        return UNAVAILABLE;
    }
    if (f_open(&map_io->file, MAP_FILE, FA_READ) != FR_OK) {
        ESP_LOGE(TAG, "Can not open %s", MAP_FILE);
        heap_caps_free(map_io->bounce);
        heap_caps_free(map_io);
        sd_unlock();
        return PM_FAIL;
    }

    prepare_fast_seek(&map_io->file);

    mfr_io_t io = { .ctx = map_io, .read = io_read, .seek = io_seek };
    int err = MFR_OK;
    mfr_map_t* map = mfr_open(&io, &err);
    int64_t opened = esp_timer_get_time();
    if (map) {
        ESP_LOGI(TAG, "open: %lld ms, %lu reads %lld ms, %lu seeks %lld ms", (opened - start) / 1000,
            (unsigned long)stats.reads, stats.read_us / 1000, (unsigned long)stats.seeks, stats.seek_us / 1000);
        double bounds[4];
        mfr_bounds(map, bounds);
        ESP_LOGI(TAG, "Map %lu bytes, bounds %.4f,%.4f - %.4f,%.4f; view %.5f,%.5f zoom %u", (unsigned long)f_size(&map_io->file),
            bounds[0], bounds[1], bounds[2], bounds[3], lat, lon, zoom);
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
            ESP_LOGE(TAG, "Render failed: %d (%s)", tiles, mfr_last_error());
            result = PM_FAIL;
        } else {
            ESP_LOGI(TAG, "Rendered %d tiles in %lld ms (%lu reads %lld ms, %lu seeks %lld ms)", tiles,
                (esp_timer_get_time() - start) / 1000, (unsigned long)stats.reads, stats.read_us / 1000,
                (unsigned long)stats.seeks, stats.seek_us / 1000);
        }
        mfr_close(map);
    } else {
        ESP_LOGE(TAG, "Can not read map: %d (%s)", err, mfr_last_error());
        result = PM_FAIL;
    }

    f_close(&map_io->file);
    heap_caps_free(map_io->bounce);
    heap_caps_free(map_io);
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
