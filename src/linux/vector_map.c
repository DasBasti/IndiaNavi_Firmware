/*
 * Vector map of the simulator: draws the Mapsforge maps of INDIANAVI_MAP (default map.map) with
 * the host build of the mapsforge-rs library, see navi/vector_map.h. INDIANAVI_MAP is one map
 * file or a folder, all .map files in the folder are used like the MAPS folder of the SD card.
 *
 * SPDX-License-Identifier: MIT
 */

#include "navi/vector_map.h"

#ifdef VECTOR_MAP

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gui/map_labels.h"
#include "map_catalog.h"
#include "mapsforge.h"
#include "mfr_raster.h"

static map_catalog_t catalog;
/* the files of the catalog entries, names on the PC are longer than 8.3 */
static char paths[MAP_CATALOG_MAX][PATH_MAX];
static mfr_scene_t* scene;

static const char* map_location(void)
{
    const char* path = getenv("INDIANAVI_MAP");
    return path ? path : "map.map";
}

static ssize_t io_read(void* ctx, uint8_t* buf, size_t len)
{
    size_t n = fread(buf, 1, len, (FILE*)ctx);
    return n == 0 && ferror((FILE*)ctx) ? -1 : (ssize_t)n;
}

static int io_seek(void* ctx, uint64_t offset)
{
    return fseeko((FILE*)ctx, (off_t)offset, SEEK_SET);
}

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

static void draw_point(void* ctx, mfr_point_t at, uint32_t color, uint8_t radius)
{
    mfr_raster_disc((const mfr_raster_t*)ctx, at.x, at.y, radius, color);
}

/* reads the header of a map into the catalog */
static void add_to_catalog(map_catalog_t* catalog, const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return;
    mfr_io_t io = { .ctx = f, .read = io_read, .seek = io_seek };
    int err = MFR_OK;
    mfr_map_t* map = mfr_open(&io, &err);
    if (map) {
        double b[4];
        mfr_bounds(map, b);
        map_bounds_t bounds = { .min_lat = b[0], .min_lon = b[1], .max_lat = b[2], .max_lon = b[3] };
        char name[MAP_CATALOG_NAME_LEN];
        snprintf(name, sizeof(name), "M%02u.MAP", catalog->count);
        uint8_t index = catalog->count;
        fseeko(f, 0, SEEK_END);
        if (map_catalog_add(catalog, name, (uint64_t)ftello(f), mfr_created(map), bounds))
            snprintf(paths[index], sizeof(paths[index]), "%s", path);
        else
            fprintf(stderr, "vector map: more than %d maps, %s is not used\n", MAP_CATALOG_MAX, path);
        mfr_close(map);
    } else {
        fprintf(stderr, "vector map: can not read %s: %d (%s)\n", path, err, mfr_last_error());
    }
    fclose(f);
}

static int is_map_file(const char* name)
{
    size_t length = strlen(name);
    return length > 4 && strcasecmp(name + length - 4, ".map") == 0;
}

bool vector_map_supported(void)
{
    return true;
}

/* the paths of the maps are kept for the catalog of the map screen only */
error_code_t vector_map_scan(map_catalog_t* catalog)
{
    map_catalog_clear(catalog);
    const char* location = map_location();
    struct stat info;
    if (stat(location, &info) != 0)
        return PM_OK;
    if (S_ISDIR(info.st_mode)) {
        DIR* dir = opendir(location);
        struct dirent* entry;
        while (dir && (entry = readdir(dir))) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", location, entry->d_name);
            if (is_map_file(entry->d_name) && stat(path, &info) == 0 && S_ISREG(info.st_mode))
                add_to_catalog(catalog, path);
        }
        if (dir)
            closedir(dir);
    } else {
        add_to_catalog(catalog, location);
    }
    map_catalog_resolve(catalog);
    return PM_OK;
}

bool vector_map_available(void)
{
    vector_map_scan(&catalog);
    for (uint8_t i = 0; i < catalog.count; i++)
        if (catalog.maps[i].superseded)
            fprintf(stderr, "vector map: %s is replaced by a newer map\n", paths[i]);
    return map_catalog_used(&catalog) > 0;
}

/* adds the shapes of one map to the scene, returns the tiles read or < 0 */
static int collect_map(uint8_t index, double lat, double lon, uint8_t zoom, uint16_t width, uint16_t height,
    int16_t anchor_x, int16_t anchor_y)
{
    FILE* f = fopen(paths[index], "rb");
    if (!f)
        return MFR_ERR_IO;
    mfr_io_t io = { .ctx = f, .read = io_read, .seek = io_seek };
    int err = MFR_OK;
    int tiles;
    mfr_map_t* map = mfr_open(&io, &err);
    if (map) {
        tiles = mfr_scene_collect(scene, map, lat, lon, zoom, width, height, anchor_x, anchor_y);
        if (tiles < 0)
            fprintf(stderr, "vector map: %s: render failed: %d (%s)\n", paths[index], tiles, mfr_last_error());
        mfr_close(map);
    } else {
        fprintf(stderr, "vector map: can not read %s: %d (%s)\n", paths[index], err, mfr_last_error());
        tiles = err;
    }
    fclose(f);
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
    if (count == 0)
        return PM_OK;
    if (!scene && !(scene = mfr_scene_new()))
        return UNAVAILABLE;

    int drawn = 0;
    for (uint8_t i = 0; i < count; i++)
        if (collect_map(selected[i], lat, lon, zoom, width, height, anchor_x, anchor_y) >= 0)
            drawn++;

    mfr_raster_t raster = { .ctx = (void*)dsp, .span = span, .width = width, .height = height, .white = WHITE };
    mfr_draw_t draw = {
        .ctx = &raster,
        .fill_polygon = draw_fill_polygon,
        .polyline = draw_polyline,
        .point = draw_point,
        .label = draw_label,
    };
    mfr_scene_draw(scene, MFR_PALETTE_EINK7, &draw);
    return drawn > 0 ? PM_OK : PM_FAIL;
}

#else /* VECTOR_MAP */

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
