/*
 * Vector map, see navi/vector_map.h
 *
 * SPDX-License-Identifier: MIT
 */

#include "navi/vector_map.h"

#ifdef VECTOR_MAP

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include <ff.h>

#include "esp_heap_caps.h"

#include "gui/map_icons.h"
#include "gui/map_labels.h"
#include "map_catalog.h"
#include "mapsforge.h"
#include "mfr_raster.h"
#include "navi/sd.h"

#define MAP_FOLDER "//MAPS"

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

/* the maps on the card, built by vector_map_available() */
static map_catalog_t catalog;

/* shapes of all maps of a screen, kept for its memory */
static mfr_scene_t* scene;

/*
 * Cluster link maps of the map files. Seeking in a file of several GB walks its cluster chain
 * from the start (about 70 ms per seek), with a link map a seek is a table lookup. Building one
 * walks the chain once (5 s for 3 GB on FAT32), so they are kept for the most recently drawn maps
 * and reused as long as a file starts at the same cluster and has the same size.
 */
#define LINK_MAPS MAP_CATALOG_DRAWN

typedef struct {
    DWORD* table;
    size_t size; // in DWORDs
    bool valid;
    uint32_t start_cluster;
    FSIZE_t file_size;
    uint32_t used; // render counter of the last use, the oldest entry is replaced
} link_map_t;

static link_map_t link_maps[LINK_MAPS];
static uint32_t render_count;

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

/* names of places: the font of the UI, the position of the place is the middle of the name */
static void draw_label(void* ctx, mfr_point_t at, const char* text, uint32_t len, uint8_t rank)
{
    map_labels_draw((const display_t*)((const mfr_raster_t*)ctx)->ctx, &f8x8, at.x, at.y, text, len, rank);
}

/* pictures of POIs, the library draws the colored symbols when this is missing */
static void draw_icon(void* ctx, mfr_point_t at, uint8_t icon)
{
    map_icons_draw((const display_t*)((const mfr_raster_t*)ctx)->ctx, icon, at.x, at.y);
}

static void draw_point(void* ctx, mfr_point_t at, uint32_t color, uint8_t radius)
{
    mfr_raster_disc((const mfr_raster_t*)ctx, at.x, at.y, radius, color);
}

/* the link map of the file, from the cache or built now */
static void prepare_fast_seek(FIL* file)
{
    int64_t start = esp_timer_get_time();
    link_map_t* entry = NULL;
    for (int i = 0; i < LINK_MAPS; i++) {
        link_map_t* l = &link_maps[i];
        if (l->valid && l->start_cluster == file->obj.sclust && l->file_size == f_size(file)) {
            l->used = render_count;
            file->cltbl = l->table;
            return;
        }
        // an unused entry, or else the one used longest ago
        if (!entry || (entry->valid && (!l->valid || l->used < entry->used)))
            entry = l;
    }

    entry->valid = false;
    entry->used = render_count;
    if (!entry->table) {
        entry->size = 64;
        entry->table = mfr_alloc(entry->size * sizeof(DWORD));
    }
    for (int attempt = 0; entry->table && attempt < 2; attempt++) {
        file->cltbl = entry->table;
        entry->table[0] = entry->size;
        FRESULT res = f_lseek(file, CREATE_LINKMAP);
        if (res == FR_OK) {
            ESP_LOGI(TAG, "Link map of %lu entries built in %lld ms", (unsigned long)entry->table[0], (esp_timer_get_time() - start) / 1000);
            f_lseek(file, 0);
            entry->valid = true;
            entry->start_cluster = file->obj.sclust;
            entry->file_size = f_size(file);
            return;
        }
        if (res != FR_NOT_ENOUGH_CORE)
            break;
        // table[0] is the number of DWORDs needed
        size_t needed = entry->table[0];
        mfr_free(entry->table);
        entry->size = needed;
        entry->table = mfr_alloc(entry->size * sizeof(DWORD));
    }
    ESP_LOGW(TAG, "No link map, seeks are slow");
    file->cltbl = NULL;
}

static map_io_t* map_io_create(void)
{
    map_io_t* map_io = heap_caps_calloc(1, sizeof(map_io_t), MALLOC_CAP_8BIT);
    if (map_io)
        map_io->bounce = heap_caps_malloc(IO_BOUNCE_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (map_io && !map_io->bounce) {
        heap_caps_free(map_io);
        map_io = NULL;
    }
    return map_io;
}

static void map_io_free(map_io_t* map_io)
{
    if (!map_io)
        return;
    heap_caps_free(map_io->bounce);
    heap_caps_free(map_io);
}

static void map_path(char* path, size_t size, const char* name)
{
    snprintf(path, size, MAP_FOLDER "/%s", name);
}

/* reads the header of a map into the catalog, the SD card is locked */
static void add_to_catalog(map_catalog_t* catalog, map_io_t* map_io, const FILINFO* info)
{
    char path[sizeof(MAP_FOLDER) + MAP_CATALOG_NAME_LEN + 1];
    map_path(path, sizeof(path), info->fname);
    if (f_open(&map_io->file, path, FA_READ) != FR_OK) {
        ESP_LOGW(TAG, "Can not open %s", path);
        return;
    }
    mfr_io_t io = { .ctx = map_io, .read = io_read, .seek = io_seek };
    int err = MFR_OK;
    mfr_map_t* map = mfr_open(&io, &err);
    if (map) {
        double b[4];
        mfr_bounds(map, b);
        map_bounds_t bounds = { .min_lat = b[0], .min_lon = b[1], .max_lat = b[2], .max_lon = b[3] };
        uint64_t created = mfr_created(map);
        if (map_catalog_add(catalog, info->fname, info->fsize, created, bounds))
            ESP_LOGI(TAG, "%s: %lu bytes, %.4f,%.4f - %.4f,%.4f, created %llu", info->fname, (unsigned long)info->fsize,
                b[0], b[1], b[2], b[3], (unsigned long long)created);
        else
            ESP_LOGW(TAG, "More than %d maps, %s is not used", MAP_CATALOG_MAX, info->fname);
        mfr_close(map);
    } else {
        ESP_LOGW(TAG, "%s is no map: %d (%s)", path, err, mfr_last_error());
    }
    f_close(&map_io->file);
}

bool vector_map_supported(void)
{
    return true;
}

error_code_t vector_map_scan(map_catalog_t* catalog)
{
    int64_t start = esp_timer_get_time();
    map_catalog_clear(catalog);
    map_io_t* map_io = map_io_create();
    if (!map_io) {
        ESP_LOGE(TAG, "Out of memory");
        return PM_FAIL;
    }
    if (!sd_lock()) {
        map_io_free(map_io);
        return PM_FAIL;
    }
    FF_DIR dir;
    FILINFO info;
    if (f_opendir(&dir, MAP_FOLDER) == FR_OK) {
        while (f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
            if (!(info.fattrib & AM_DIR) && map_catalog_is_map_name(info.fname))
                add_to_catalog(catalog, map_io, &info);
        }
        f_closedir(&dir);
    }
    sd_unlock();
    map_io_free(map_io);

    uint8_t used = map_catalog_resolve(catalog);
    for (uint8_t i = 0; i < catalog->count; i++)
        if (catalog->maps[i].superseded)
            ESP_LOGI(TAG, "%s is replaced by a newer map", catalog->maps[i].name);
    ESP_LOGI(TAG, "%u maps, %u used, read in %lld ms", catalog->count, used, (esp_timer_get_time() - start) / 1000);
    return PM_OK;
}

bool vector_map_available(void)
{
    if (vector_map_scan(&catalog) != PM_OK) {
        map_catalog_clear(&catalog);
        return false;
    }
    return map_catalog_used(&catalog) > 0;
}

bool vector_map_covers(double lat, double lon, uint8_t zoom, uint16_t width, uint16_t height, int16_t anchor_x,
    int16_t anchor_y)
{
    uint8_t selected[MAP_CATALOG_DRAWN];
    map_bounds_t screen = map_catalog_screen_bounds(lat, lon, zoom, width, height, anchor_x, anchor_y);
    return map_catalog_select(&catalog, screen, selected, MAP_CATALOG_DRAWN) > 0;
}

/* adds the shapes of one map to the scene, the SD card is locked. Returns the tiles read or < 0 */
static int collect_map(map_io_t* map_io, const map_entry_t* entry, double lat, double lon, uint8_t zoom,
    uint16_t width, uint16_t height, int16_t anchor_x, int16_t anchor_y)
{
    char path[sizeof(MAP_FOLDER) + MAP_CATALOG_NAME_LEN + 1];
    map_path(path, sizeof(path), entry->name);
    if (f_open(&map_io->file, path, FA_READ) != FR_OK) {
        ESP_LOGE(TAG, "Can not open %s", path);
        return MFR_ERR_IO;
    }
    prepare_fast_seek(&map_io->file);

    int64_t start = esp_timer_get_time();
    mfr_io_t io = { .ctx = map_io, .read = io_read, .seek = io_seek };
    int err = MFR_OK;
    int tiles;
    mfr_map_t* map = mfr_open(&io, &err);
    if (map) {
        tiles = mfr_scene_collect(scene, map, lat, lon, zoom, width, height, anchor_x, anchor_y);
        if (tiles < 0)
            ESP_LOGE(TAG, "%s: render failed: %d (%s)", entry->name, tiles, mfr_last_error());
        else
            ESP_LOGI(TAG, "%s: %d tiles in %lld ms", entry->name, tiles, (esp_timer_get_time() - start) / 1000);
        mfr_close(map);
    } else {
        ESP_LOGE(TAG, "Can not read %s: %d (%s)", path, err, mfr_last_error());
        tiles = err;
    }
    f_close(&map_io->file);
    return tiles;
}

error_code_t vector_map_render(const display_t* dsp, double lat, double lon, uint8_t zoom,
    int16_t anchor_x, int16_t anchor_y)
{
    uint16_t width = dsp->size.width;
    uint16_t height = dsp->size.height;
    uint8_t selected[MAP_CATALOG_DRAWN];
    map_bounds_t screen = map_catalog_screen_bounds(lat, lon, zoom, width, height, anchor_x, anchor_y);
    uint8_t count = map_catalog_select(&catalog, screen, selected, MAP_CATALOG_DRAWN);

    display_fill(dsp, WHITE);
    map_labels_reset();
    if (count == 0) {
        ESP_LOGI(TAG, "No map at %.5f,%.5f", lat, lon);
        return PM_OK;
    }

    if (!scene && !(scene = mfr_scene_new())) {
        ESP_LOGE(TAG, "Out of memory");
        return UNAVAILABLE;
    }
    map_io_t* map_io = map_io_create();
    if (!map_io) {
        ESP_LOGE(TAG, "Out of memory");
        return UNAVAILABLE;
    }
    if (!sd_lock()) {
        ESP_LOGW(TAG, "SD card is busy");
        map_io_free(map_io);
        return UNAVAILABLE;
    }

    int64_t start = esp_timer_get_time();
    memset(&stats, 0, sizeof(stats));
    render_count++;
    int drawn = 0;
    // one map after the other, each is closed when its shapes are in the scene
    for (uint8_t i = 0; i < count; i++)
        if (collect_map(map_io, &catalog.maps[selected[i]], lat, lon, zoom, width, height, anchor_x, anchor_y) >= 0)
            drawn++;
    sd_unlock();
    map_io_free(map_io);

    mfr_raster_t raster = { .ctx = (void*)dsp, .span = span, .width = width, .height = height, .white = WHITE };
    mfr_draw_t draw = {
        .ctx = &raster,
        .fill_polygon = draw_fill_polygon,
        .polyline = draw_polyline,
        .point = draw_point,
        .label = draw_label,
        .icon = draw_icon,
    };
    mfr_scene_draw(scene, MFR_PALETTE_EINK7, &draw);
    ESP_LOGI(TAG, "Rendered %d of %u maps at %.5f,%.5f zoom %u in %lld ms (%lu reads %lld ms, %lu seeks %lld ms)",
        drawn, count, lat, lon, zoom, (esp_timer_get_time() - start) / 1000, (unsigned long)stats.reads,
        stats.read_us / 1000, (unsigned long)stats.seeks, stats.seek_us / 1000);
    return drawn > 0 ? PM_OK : PM_FAIL;
}

#else /* VECTOR_MAP */

bool vector_map_covers(double lat, double lon, uint8_t zoom, uint16_t width, uint16_t height, int16_t anchor_x,
    int16_t anchor_y)
{
    return false;
}

bool vector_map_supported(void)
{
    return false;
}

error_code_t vector_map_scan(map_catalog_t* catalog)
{
    map_catalog_clear(catalog);
    return UNAVAILABLE;
}

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
