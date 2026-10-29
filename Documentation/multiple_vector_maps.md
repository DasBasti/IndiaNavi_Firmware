# Multiple vector maps on the SD card (concept)

Status: all four phases are implemented: 1 (catalog, several maps on one screen, new tops old), 2 (maps over
WiFi, several maps in the app), 3 (uploads that can be continued, measured speed in the app) and 4 (tiles where
no vector map covers the screen).

Today the IndiaNavi draws its vector map from exactly one file, `MAPS/map.map` (`src/esp32/vector_map.c`).
The app (IndiaNavi mode) downloads Mapsforge maps per region from download.mapsforge.org. A walk near a
border, a holiday in two regions, or a country that is bigger than one file needs more than one map on the
card. This document describes how the firmware, the WiFi API and the app can support several map files.

## Goals

- Any number of `.map` files (practically up to 32) can be on the card. The device draws all maps that cover
  the visible area, so there are no holes at the borders between two regions.
- `MAPS/map.map` keeps working, a card prepared for the current firmware needs no change.
- Maps can be copied with a card reader or sent by the app over WiFi.
- The simulated IndiaNavi in the app draws the same picture as the device.

Not a goal: merging maps into one file on the phone. Map files are hundreds of MB to several GB, the phone
would need the space twice and the merged file can not be updated per region.

## Constraints of the device today

| | today | consequence |
|---|---|---|
| File names | `CONFIG_FATFS_LFN_NONE`, 8.3 names only | map files are named like `RHEINL01.MAP`. A long name copied by a PC still works, FatFs sees its short alias (`RHEINL~1.MAP`) |
| File size | at most 4 GB - 1 per file on FAT32 (exFAT has no such limit) | big countries do not fit into one file on FAT32, several maps are the way to cover them |
| Seeking | link map (`clmt`) for one file, 5 s to build for 3 GB | one link map per map file has to be kept |
| Rendering | `mfr_render` collects, sorts and draws one map | the drawing order has to be shared between maps, see below |
| Upload | only `track.gpx` and `MAPS/{z}/{x}/{y}.raw` are accepted | new path rule for maps |
| WiFi speed | about 180 kB/s with tiles (small files, 2 connections) | a 300 MB map takes 5 to 30 minutes, has to be measured for big files |

## Where the maps are

All files `MAPS/*.MAP` (the root of the `MAPS` folder, next to the numbered tile folders) are vector maps.
`MAPS/map.map` is one of them. Subfolders are not searched, so the tile folders `MAPS/14/`, `MAPS/16/` are
never scanned.

## Map catalog

The firmware builds a catalog of the maps once, not on every render:

- at boot, after `POST /api/reload`, after a map was uploaded or deleted over WiFi;
- for every `MAPS/*.MAP`: open it, read the header (`mfr_open`, `mfr_bounds`), close it;
- an entry keeps the 8.3 name, file size, first cluster, bounding box, creation date and the zoom intervals,
  about 64 bytes; 32 maps are 2 kB;
- files that are not Mapsforge maps are left out and logged;
- new tops old: a map is left out when a newer map (by the creation date in its header) covers at least 90 % of
  its bounding box. An updated download of a region replaces the old file without deleting it, a newer map of a
  whole country replaces older regional maps in it, two neighbouring regions stay.

The catalog is `lib/map_catalog`, with host tests in `test/host/Platinenmacher/test_map_catalog`.

`vector_map_available()` builds the catalog: the vector map is available if a map is in it. It is called when
the map screen is created, so `POST /api/reload` (which creates the map screen again) also finds new maps.

Reading 32 headers at boot costs about one second (header plus tag tables, a few kB each). The tag tables
are not kept, they are read again when a map is drawn.

## Drawing several maps

### Which maps

For a render the firmware computes the area of the screen in degrees (corners of the view at its zoom level)
and takes the maps whose bounding box overlaps it. Usually that is one map, near a border two. At most 4
maps are drawn, the ones with the smallest bounding boxes first (a detailed regional map before a
continent map).

### One scene for all maps

Drawing the maps one after the other does not work: the white land polygons (`natural=nosea`,
`freizeitkarte=land`) and the sea of the second map would cover the forests and roads of the first one where
the maps overlap. Mapsforge country maps also have rectangular bounding boxes, so they overlap a lot.

Instead the shapes of all maps go into one scene, which is sorted once and drawn once, exactly like the
shapes of several tiles of one map are today:

```
scene = new scene
for map in maps that cover the screen:        // one after the other
    open map (with its link map)
    collect the visible tiles of the map into the scene
    close map
sort the scene by layer and draw order
draw the scene, then the place names
```

The land of map B (draw order `LAND`) then lies below the forest of map A (`FOREST`), and the sea (`SEA`) below
everything, as within one map.

Shapes that are in both maps are drawn twice at the same pixels, which looks the same as once. Place names
are already skipped when they would overlap another name, so a village in both maps gets one label.

Maps with different zoom intervals work, each map uses its own sub file for the zoom level.

### Changes in mapsforge-rs

`MapFile::render` collects and draws in one call. It is split into:

- `Scene` (public): `Scene::new()`, `scene.clear()`
- `MapFile::collect(&mut self, view, styles, &mut scene) -> Result<usize>` (the tile loop of today's `render`)
- `scene.draw(&mut target)` (today's `emit`)

`render` stays as `collect` + `draw` for one map. The C API gets:

```c
mfr_scene_t* mfr_scene_new(void);
int mfr_scene_collect(mfr_scene_t* scene, mfr_map_t* map, double lat, double lon, uint8_t zoom,
    uint16_t width, uint16_t height, int16_t anchor_x, int16_t anchor_y);
void mfr_scene_draw(mfr_scene_t* scene, uint8_t palette, const mfr_draw_t* draw);
void mfr_scene_free(mfr_scene_t* scene);
```

The scene holds only screen points (`i16`), styles and the label texts, not the file data, so a map can be
closed after it was collected. For two maps at the 500 m scale this is a few hundred kB in PSRAM.

### Seeking in several files

The link map cache (`clmt`) becomes a small cache with one entry per map, keyed by first cluster and size as
today, so changing between two maps at a border does not cost 5 s per render. Built lazily on the first
render of a map, kept for the 4 most recently used maps.

### When no map covers the position

The map screen decides for every screen (`map_layer_render` in `src/screens/map_screen.c`): when a vector map
of the catalog covers a part of the screen (`vector_map_covers`, the same choice as for drawing), the vector maps
are drawn, otherwise the tiles `MAPS/{zoom}/{x}/{y}.raw` (`map_render`), with "Not Found" in tiles that are not
on the card. So a track that leads beyond the vector maps shows the tiles the app prepared for it. The position,
the marker, the track and its waypoints are at the same pixels in both, the screen only changes its background.

- The tile loading callbacks are attached in both modes, they only run when `map_render` draws the tiles.
- The decision is by the bounding boxes of the maps: a country map with a rectangular box covers parts of its
  neighbours, where it has no data, there the screen stays white. Tiles under the vector maps (both on one
  screen) are not drawn, the screens near the edge of the vector maps show the vector maps.
- Without a position nothing is drawn as before while there are vector maps; without vector maps the device
  works with tiles only, as before.
- The simulated IndiaNavi of the app does the same with the tiles of the converter on the phone
  (`renderNaviScreen` with `readTile`).

## WiFi API

Version 2 of the upload API (`docs/wifi_upload_api.md` in the app):

| Endpoint | |
|---|---|
| `GET /api/maps` | the catalog read fresh from the card: `{"maps": [{ "file": "rheinl01.map", "size": 301234567, "created": 1759550000000, "bounds": [minLat, minLon, maxLat, maxLon], "used": true }]}`, `used` is false for a replaced map |
| `PUT /sd/MAPS/{name}.map` | a map, `{name}` 1 to 8 characters `A-Z 0-9 _ -` (`map_catalog_is_upload_name`). Written as `{name}.tmp` and renamed when complete, like the other files |
| `DELETE /sd/MAPS/{name}.map` | removes a map |
| `POST /api/reload` | the map screen reads the maps again (it is created again) |
| `GET /api/info` | gets `"api": 2` and `"vector_maps": true` when the firmware draws vector maps |

- `content_len` of the ESP-IDF server is a `size_t` (32 bit), which fits up to 4 GB - 1 as FAT32 does.
- Free space is checked as for tiles. A map is only uploaded while the device is charging (WiFi is only on
  then), the display shows the progress box as for tiles, with MB; the bar follows the bytes.
- A map is one request of many minutes: the WiFi (switched off after 10 minutes without a request) and the
  announced transfer (stopped after 2 minutes without a stored file) are kept alive by the bytes that come in.
- `vector_map_scan()` reads the headers into a catalog of the caller, so the request does not touch the catalog
  the map screen draws from.
- Phase 3, resume (API version 3): a map is sent with `Content-Range: bytes {start}-{end}/{total}`. When the
  connection breaks, `{name}.tmp` keeps the bytes that came in instead of being deleted. `GET /api/maps` lists it
  under `partial` with its size and, from the first 44 bytes of the map header
  (`map_catalog_parse_header_start`), the size and creation date of the whole map. The app continues only when
  they match the map on the phone, so the part of an older version is never continued with a newer one. The rest
  is sent as one request with `start` = size of the part, the device seeks to the end of the part once (a seek
  in a big FAT file walks its cluster chain, so the rest is not sent in many small parts). A `start` that is not
  the size of the part is answered with `416`. `Content-Range` is parsed by `lib/content_range` (host tested).
- The app streams the rest from a copy in its cache (the native upload only sends whole files), measures the
  speed of every upload of at least 10 seconds and estimates the time of the next ones from it.

## App

### Names on the card

The app gives every map a short 8.3 name from its region and a number: `germany-rheinland-pfalz.map` becomes
`rheinl01.map`, a second map starting with the same 6 letters `rheinl02.map` (`cardName` in
`modules/map_catalog.js`). The app remembers which phone map it sent under which name (setting `cardMaps`), a
map counts as on the card when the file there has its size. Maps on the card that the app did not send (copied
with a card reader) are shown with their file name.

### Maps screen

- **Maps screen**: a checkbox per map instead of one selected map: the maps for the IndiaNavi. The simulation
  draws them.
- **WiFi transfer**, section Vector maps (when connected to a device with API version 2): the checked maps with
  "Send" for the ones that are not on the card (and "Send N maps"), the maps on the card with "Delete" and the
  note when one is replaced by a newer map. The free space is checked before a map is sent. The upload is
  streamed from the file (`UploadTask` of expo-file-system), with progress and the time that is left.
- The simulated IndiaNavi of the file transfer (`modules/simulated_device.js`) keeps the maps that are sent to it
  and answers `GET /api/maps`.
- Open: the converter map could show the bounding boxes of the checked maps.

### Simulated IndiaNavi

`collectScene` in `modules/mapsforge.js` takes a list of maps and collects all of them before the one sort. The
maps of a screen are chosen by `modules/map_catalog.js`, a port of `lib/map_catalog` with the same rules and
tests: not replaced, overlapping the screen, the smallest first, at most 4. The sort is stable on both sides, so
shapes with the same layer and draw order are drawn in the order of the maps and of the files, and the simulated
display matches the device.

## Phases

1. **Firmware, card reader**: catalog, scene over several maps, link map cache per map, mapsforge-rs scene
   API. Maps are copied to `MAPS/` with a card reader. `map.map` keeps working.
2. **App and API**: several checked maps in the app and in the simulation, `GET /api/maps`, upload and delete
   of maps over WiFi, the "On the IndiaNavi" list.
3. **Big files**: resumable uploads, measured upload speed and the time estimate in the app. The speed of the
   device for one big file is still to be measured on hardware.
4. **Tiles as fallback**: vector or tiles per render, so a track can use tiles where no vector map exists. Tested
   with the PC simulator (vector maps at their border, tiles beyond them).

## Open questions

- How fast does the device write one big file over WiFi? Below about 500 kB/s the card reader should be the
  recommended way for maps over 200 MB.
- Long file names (`CONFIG_FATFS_LFN_HEAP`) would make the names on the card readable, at the cost of some
  RAM and the 8.3 rule of the upload API. Not needed.

Decided: new tops old (see the catalog), 4 maps per screen are enough.
