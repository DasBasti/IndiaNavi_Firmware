# Memory & Stability Audit

Date: 2026-09-29 · Branch: `sdk-update` · Scope: all project code in `src/`, `include/`,
`lib/Platinenmacher*`, `lib/helper`, `lib/nmea_parser`. Third-party code (`components/mdns`,
`lib/sxml`, `lib/qrcodegen`) was not audited.

Severity: **Critical** = crash, reboot, deadlock or memory corruption reachable in normal use ·
**High** = reachable with bad or unusual input (SD files, GPS noise) or in a common state
(charging) · **Medium** = leak or race that degrades over time · **Low** = debug-only or dead code.

## Most likely causes of the instability

1. **NMEA parser buffer overflow**: `item_str` was 16 bytes and every received character was
   appended without a bounds check. Any long NMEA field ($GPTXT, PMTK text, line noise, a baud
   mismatch) overwrote the parser struct, including the `buffer` pointer.
2. **Wild function call in the PMTK parser**: the response to the `PMTK353` command sent at
   every boot indexed `message_parser[353]` (the array has 2 entries) and called the function
   pointer found there.
3. **SD mutex deadlock (S3)**: `load_map_tiles_to_permanent_memory` `continue`d while holding
   `sd_semaphore` when a tile was missing. After that every SD access blocked forever.
4. **`vTaskDelete(NULL)` on the main task**: `turn_to_on()` and the STOP_CHARGING handler called
   `vTaskDelete(wifiTask_h)`. When WiFi was never started the handle is NULL, which deletes the
   *calling* task, so the button and event loop stopped. When WiFi had already stopped, the stale
   handle crashed the device.
5. **Reboot when the charger is plugged in**: the WiFi task ran `ESP_ERROR_CHECK(loadFile(...))`.
   `loadFile` returns `PM_FAIL` (1) when there is no `WIFI` file, which aborts.
6. **Heap exhaustion while charging**: `do_background_ota()` ran every 30 s and leaked the OTA
   file buffer and URL each time.

## Findings

| # | Sev | Location | Issue | Status |
|---|-----|----------|-------|--------|
| 1 | Critical | `nmea_parser.c` `gps_decode` | `item_str[16]` written without bound | Fixed: bounded append, item length raised to 32 |
| 2 | Critical | `nmea_parser.c` `esp_handle_uart_pattern` | Line of `pos+1` bytes read into a 512-byte buffer with no check | Fixed: oversize lines are dropped |
| 3 | Critical | `pmtk_parser.c` | `message_parser[message_id]` OOB call; loop used `sizeof` bytes as count; `pmtkSystemMessages[n]` unchecked | Fixed |
| 4 | High | `nmea_parser.c` init | `plugins` pointed into the caller's stack (`config` local in GPS task) | Fixed: plugins copied into the handle |
| 5 | Critical | `gui_map_callbacks.c` S3 loader | `continue` while holding SD mutex, so deadlock | Fixed |
| 6 | Critical | `gui_map_callbacks.c` ESP32 loader | `img->data` pointed to freed buffer on error/timeout (use-after-free in render); `check_if_map_tile_is_loaded` left a dangling pointer | Fixed: pointer only set on success, NULLed after free |
| 7 | Critical | `main.c`, `off_screen.c` | `vTaskDelete` with NULL/stale handle; WiFi task killed while holding SD mutex / HTTP / OTA resources | Fixed: cooperative `wifi_request_stop()`; task cleans up (esp_wifi_stop/deinit, handlers, event group) and deletes itself; handles guarded |
| 8 | Critical | `wifi.c` | `ESP_ERROR_CHECK(loadFile())` aborts without `WIFI` file | Fixed |
| 9 | Critical | `wifi.c`, `gps.c`, `ota.c`, `map_loader.c` | `readline()` unbounded into `ssid[32]`, `password[64]`, `tz[50]`, `wp_line[50]`; OTA url `malloc(countline())` is 0 when the file has no newline | Fixed: new `readline_n()`; SSID/password length validated |
| 10 | Critical | `sd.c` `loadFile` | Read the whole file into fixed caller buffers (`wifi_file[97]`, `timezone_file[100]`, 32 KB TRACK buffer) and never `\0`-terminated (callers use `strlen`/`readline`) | Fixed: `dest_size` added, reads truncated, always terminated; allocated buffers are size+1 |
| 11 | Critical | `map_screen.c` + `gpx.c` | `height_graph_data` allocated `sizeof*(n-1)+1` bytes but indexed up to n-1, a heap overflow. `waypoints_num` was last index, not count | Fixed |
| 12 | High | `map.c` waypoints | `map_free_waypoints` didn't reset list head/tail, so the next list appended to freed memory and numbering continued (made #11 worse on re-create) | Fixed |
| 13 | High | `gpx.c` | `char buf[255]` filled with unbounded token length: stack overflow on any long comment/description/name | Fixed: truncate |
| 14 | High | `gpx.c` | On `SXML_ERROR_BUFFERDRY` the parser restarted at offset 0, so a truncated or odd GPX looped forever, allocating waypoints | Fixed: stop parsing |
| 15 | High | `gpx.c` | Static state (`first_wp`, `state`, `wp`) not reset between parses; `track_name` leaked when set twice; unfinished waypoint leaked | Fixed |
| 16 | High | `umlaut.c` | Read past the terminator when a string ended with `0xC3`; comparisons on signed `char`; wrong mappings (ö→ae, ü→ae, ß wrong byte) | Fixed |
| 17 | High | `display.c` `display_text_draw` | Characters outside 0x20–0x7F (UTF-8 in track names) read far outside the font array | Fixed: replaced by `?` |
| 18 | High | `gps.c` | `gps_enter_standby()` / initial commands: `ESP_ERROR_CHECK` with possibly NULL parser handle, so abort on power-off | Fixed |
| 19 | High | `acep_5in65_7c.c` | On busy timeout `ACEP_5IN65_Display` returned without releasing the SPI bus, so the next refresh blocked | Fixed |
| 20 | High | `sd.c` `StartSDTask` | Mutex given even when mount failed, then retried every 100 ms without holding it | Fixed: mutex held until mount succeeds, retry every 2 s |
| 21 | High | `sd.c` | `fileExists`/`createFileBuffer` waited `portMAX_DELAY`: without an SD card the GUI hung forever loading `track.gpx` | Fixed: 1 s timeout (`SD_MUTEX_TIMEOUT`) |
| 22 | Medium | `sd.c` | `openFileForWriting`/`writeToFile`/`closeFile`/`deleteFile` without SD mutex (race with unmount/other tasks) | Fixed |
| 23 | Medium | `ota.c` | Buffer + URL leaked every 30 s; CA store never freed | Fixed |
| 24 | Medium | `sd.c` | `openFileForWriting` leaked `path` (pointer was advanced before free); `closePhysicalFile` never closed/freed the `FIL` | Fixed |
| 25 | Medium | `gui.c` / screens | Every off→on cycle (possible while charging) recreated the top bar and map screen without freeing: map tiles (up to 9×32 KB on S3), labels, graph, gpx data. `off_screen_free` leaked `splash`; free function could run twice (double free) | Fixed: top bar created once; `free_screen()` clears the callback first and empties pipelines; `map_screen_free()` added; pointers NULLed |
| 26 | Medium | `wifi.c` | After first connect the event handlers were unregistered, so after a disconnect it never reconnected (task stuck); event group leaked per failed round | Fixed |
| 27 | Medium | `map_screen.c` `toggleZoom` | Modified map from button task while GUI task renders it | Fixed: flag, applied in pre-render callback |
| 28 | Medium | `off_screen.c`, `picture_screen.c` | Splash image smaller than 448×600/2 was rendered past the end of its buffer | Fixed: size validated |
| 29 | Medium | `map_screen.c` | `strncpy` of track name without terminator | Fixed |
| 30 | Medium | `gps.c` | `strncpy` without terminator for unknown statements; `xQueueSendFromISR` from task context | Fixed |
| 31 | Low | `map.c` | `map_get_tile` bound check `>` instead of `>=`; waypoint tile index used `width` instead of `height` | Fixed |
| 32 | Low | `gui.c` | `render_needed` not `volatile`; `localtime()` → `localtime_r()`; post-render hook called through incompatible function-pointer type | Fixed |
| 33 | Low | `test_screen.c` | `tab_format[tabs]` OOB for 15-char task names; `task_info` unbounded | Fixed |
| 34 | Low | `game_of_life_screen.c` | World buffer sized (w-2)(h-2)/2 but indexed with w/h | Fixed |
| 35 | Low | `map_loader.c` (task never started) | URL buffer too small, leaks, `isConnected() != PM_OK` inverted | Fixed |
| 36 | Low | all `*_create` | `RTOS_Malloc` results not checked | NULL checks added in `label/image/map/graph/battery/display` create functions and screen setup |
| 37 | Low | `main.c` | `battery_indicator->charging = true` when charging *stops* | Fixed |

## Not changed (recommendations)

- **GPS track log is never written**: `gps_track->loaded` is never set, so `log.gpx` only gets
  the header and positions pile up in the queue. Enabling it is a behaviour change, so it's left
  as is.
- **Test screen ADC conflict**: `record_battery_voltage()` creates ADC1 while the power task
  owns it, so `ESP_ERROR_CHECK` aborts. Only reachable via `APP_TEST_SCREEN`.
- **`TASK_EVENT_DISABLE_GPS/DISPLAY`** still delete the tasks from outside. Nothing sends these
  events today; if they are used, convert them to the cooperative pattern used for WiFi.
- **WiFi restart right after stop**: a START_CHARGING that arrives while the WiFi task is still
  shutting down is ignored until the next charge cycle.
- **OTA polling**: with an `OTA` file present, an update is attempted every 30 s, falling back to
  the hardcoded `laptop.local` URL.
- **sxml quirk**: a comment directly after `<?xml …?>` without a newline makes sxml report
  "buffer dry". Such files no longer hang the device but are not parsed.
- Shared data (`current_position`, battery label text) is read by the GUI without locking. Only
  cosmetic torn values are possible.

## Verification

- `indianavi_s3_n16r8_debug`, `esp32dev_debug`, `esp32dev_release`, `linux_native` build (no new
  warnings).
- Host tests `pio test -e native`: 43 passed, 1 skipped (was 35). New tests: `readline_n`
  truncation, umlaut conversion and end-of-string bounds, GPX long tokens and state reset,
  `map_get_tile` boundaries, `map_free`, waypoint list restart after free.
- Not tested on hardware.
