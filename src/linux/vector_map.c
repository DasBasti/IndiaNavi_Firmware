/*
 * Vector map of the simulator: draws the Mapsforge file INDIANAVI_MAP (default map.map) with the
 * host build of the mapsforge-rs library, see navi/vector_map.h
 *
 * SPDX-License-Identifier: MIT
 */

#include "navi/vector_map.h"

#ifdef VECTOR_MAP

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "mapsforge.h"
#include "mfr_raster.h"

static const char* map_path(void)
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

static void draw_point(void* ctx, mfr_point_t at, uint32_t color, uint8_t radius)
{
    mfr_raster_disc((const mfr_raster_t*)ctx, at.x, at.y, radius, color);
}

bool vector_map_available(void)
{
    FILE* f = fopen(map_path(), "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

error_code_t vector_map_render(const display_t* dsp, double lat, double lon, uint8_t zoom,
    int16_t anchor_x, int16_t anchor_y)
{
    FILE* f = fopen(map_path(), "rb");
    if (!f)
        return PM_FAIL;

    error_code_t result = PM_FAIL;
    mfr_io_t io = { .ctx = f, .read = io_read, .seek = io_seek };
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
        if (tiles < 0)
            fprintf(stderr, "vector map: render failed: %d (%s)\n", tiles, mfr_last_error());
        else
            result = PM_OK;
        mfr_close(map);
    } else {
        fprintf(stderr, "vector map: can not read %s: %d (%s)\n", map_path(), err, mfr_last_error());
    }
    fclose(f);
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
