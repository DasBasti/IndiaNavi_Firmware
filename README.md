# India Navi firmware

India Navi is an offline navigation device with a 7-color 5.65" e-paper display. It is built for hiking and
not getting lost, not for turn-by-turn navigation: it shows your position on pre-rendered map tiles from the
SD card together with a GPX track and its height profile, and refreshes the screen every minute.

The firmware runs on an ESP32-S3 (ESP-IDF via PlatformIO). A companion phone app
(`IndiaNavi_App`) talks to the device over Bluetooth LE and WiFi.

## Features

- Map view with zoom levels 16 (100 m scale) and 14 (500 m scale), position marker, GPX track with direction
  arrows, waypoints and height graph
- Quectel L96 GNSS (GPS, GLONASS, Galileo). The time and position from the phone can be used to get a faster first fix
- GPX track log (`log.gpx`) written to the SD card, with a new segment for each boot
- Bluetooth LE peripheral (ESP32-S3 only): time, position, WiFi on/off, display settings, firmware update
  and pairing by passkey
- WiFi access point (`IndiaNavi-XXXX`) with an HTTP API for uploading maps and tracks, and for firmware updates
- Firmware updates from the app over WiFi or Bluetooth, with automatic rollback
- Power management: deep sleep with button wake-up, charge detection and a "battery empty" screen

## Hardware

| | ESP32-S3 board (main target) | ESP32 dev board (older) |
|---|---|---|
| PlatformIO board | `indianavi-s3-n16r8` (16 MB flash, 8 MB octal PSRAM) | `esp32dev` |
| Display | Waveshare ACeP 5.65" 7-color 600×448, SPI | same |
| GNSS | Quectel L96 on UART2, 9600 baud | same |
| SD card | SDMMC 4-bit, card detect | SDMMC default slot |
| Bluetooth LE | yes (NimBLE) | no |
| Partitions | `partitions_16M.csv` (2 × 6.25 MB OTA) | `partitions_custom.csv` |

The pins are in [`include/pins.h`](include/pins.h). Datasheets of the GNSS module and the accelerometer
(LSM303AH, not used yet) are in [`Documentation/`](Documentation).

## Building

Install [PlatformIO](https://platformio.org/) (`pip install platformio`). The build script picks the right
environment:

```sh
./build.sh                      # debug build for the ESP32-S3 board
./build.sh release --install    # release build, flash it
./build.sh debug --install --monitor --port /dev/ttyUSB0
./build.sh --board esp32dev     # older ESP32 board
./build.sh --help
```

Or use PlatformIO directly:

```sh
pio run -e indianavi_s3_n16r8_release -t upload
pio device monitor -e indianavi_s3_n16r8_release
```

| Environment | Use |
|---|---|
| `indianavi_s3_n16r8_debug` / `_release` | ESP32-S3 board |
| `esp32dev_debug` / `_release` | ESP32 dev board. The debug build uses a fixed position (`NO_GPS`) |
| `esp32dev_jtag` | debugging with an ESP-Prog |
| `native` | host unit tests |
| `linux_native` | renders the map screen in an X11 window on the PC |

Release builds take the version from git (`.github/git_version.py`). It is shown on the off screen and reported
by the APIs. The ESP-IDF options are in the `sdkconfig.defaults*` files.

## Tests

```sh
pio test -e native
```

The host tests in `test/host` cover the display and GUI components, the GPX parser, the map, the helpers, the
Bluetooth protocol encoding and the battery state. Tests in `test/embedded` run on the device. CI builds all
board environments, runs the host tests, cppcheck and CodeQL (see `.github/workflows`).

## Using the device

| Situation | Button |
|---|---|
| Off (deep sleep) | hold for 2 s to switch on |
| Map screen | short press: switch the zoom level |
| Any screen | hold for 3 s: switch off |
| Off screen while charging | short press: switch on |

- When a charger is connected the WiFi access point starts and the screen shows a QR code to join it.
  Without a charger WiFi switches off after 10 minutes without use.
- Without a charger the display only starts above 65% battery, to avoid a brown-out during a refresh. Below
  that the device goes back to deep sleep right away; connect a charger and press the button.
- At 5% battery (three readings in a row) the device shows the "battery empty" screen and goes to deep sleep.
  The GPS module stays in standby to keep its clock.
- A new firmware is confirmed after it has run for 60 seconds. If it crashes before that, the bootloader goes back
  to the previous one.

## Bluetooth LE

The device advertises as `IndiaNavi-XXXX` (the same name as the access point). Only one phone can be paired.
The phone has to enter the 6-digit passkey shown on the display. A new phone can pair in the first 2 minutes after
Bluetooth starts, or after the app asked the device to forget its phone.

All characteristics need an encrypted, authenticated link. The protocol is described in
`IndiaNavi_App/docs/ble_api.md`. Where it is implemented is in [`Documentation/bluetooth.md`](Documentation/bluetooth.md).

## WiFi and upload API

The device runs an access point `IndiaNavi-XXXX`. Its password is generated on first boot, stored in NVS and
shown on the display as text and as a QR code. If a `WIFI` file is on the SD card the device also joins that
network (AP+STA) and announces itself as `indianavi.local` over mDNS. In the access point the device is
`192.168.4.1`.

The HTTP server (port 80) implements the upload API of the app (`IndiaNavi_App/docs/wifi_upload_api.md`):

| Request | Purpose |
|---|---|
| `GET /api/info` | device id, firmware version, SD card space |
| `GET /sd/{folder}/` | list a folder |
| `PUT /sd/track.gpx`, `PUT /sd/MAPS/{z}/{x}/{y}.raw` | upload a file (written to `.tmp`, then renamed) |
| `DELETE /sd/{path}` | delete a track or tile |
| `POST`, `GET`, `DELETE /api/transfer` | announce, read and cancel a multi-file transfer (shows progress on the display) |
| `POST /api/reload` | load the track and map again |
| `PUT /api/firmware` | upload a firmware image |
| `POST /api/restart` | restart, e.g. into a new firmware |

Requests that change something are accepted without a token in the access point. Requests that come in over the
joined network need `Authorization: Bearer <access point password>`.

## Firmware update

The app sends the firmware in one of two ways:

1. **App over WiFi**: `PUT /api/firmware`, then `POST /api/restart`.
2. **App over Bluetooth**: the OTA characteristics, with flow control. Needs at least 30% battery or a charger.

The image is written to the inactive OTA partition and checked before it is selected.

## SD card

Format the card with FAT32 or exFAT. Only 8.3 file names are supported.

    ├── MAPS/
    │   ├── 14/{x}/{y}.raw   map tiles for zoom 14
    │   └── 16/{x}/{y}.raw   map tiles for zoom 16
    ├── art1.raw … art14.raw  images for the off screen, one is picked at random
    ├── lost.raw             shown while there is no position
    ├── track.gpx            track and waypoints shown on the map
    ├── log.gpx              track log written by the device
    ├── TIMEZONE             POSIX TZ string, e.g. CET-1CEST,M3.5.0/2,M10.5.0/3 (this is the default)
    ├── WIFI                 optional: SSID in line 1, password in line 2
    └── config.xml           optional: only <id> is used, as device name in /api/info

Map tiles are 256×256 pixels, 4 bits per pixel (the display color index), 32768 bytes without a header. The
full-screen images are 448×600 pixels in the same format. Use
[IndiaNavi Converter](https://github.com/DasBasti/IndiaNavi_Converter) to create the tiles, or upload them with
the app.

## Source layout

| Path | Content |
|---|---|
| `src/esp32/` | tasks and drivers: `main.c` (button, power, events), `gui.c`, `gps.c`, `sd.c`, `wifi.c`, `upload_server.c`, `ble*.c`, `fw_update.c` |
| `src/screens/` | screens: map, off, battery empty, picture, test, game of life |
| `src/linux/` | X11 build of the map screen |
| `lib/Platinenmacher/` | hardware-independent display, font, GUI components (label, image, map, graph, waypoints), GPX parser |
| `lib/Platinenmacher_HAL_ESP32/` | e-paper driver, GPIO, SPI, regulators |
| `lib/nmea_parser/` | NMEA parser with PMTK/PQ extensions for the L96 |
| `lib/ble_protocol/` | encoding of the Bluetooth values (tested on the host) |
| `lib/battery_state/`, `lib/helper/`, `lib/icons_16/` | battery logic, string helpers, icons (made with `tools/generate_icons.py`) |
| `lib/sxml/` (submodule), `lib/qrcodegen/`, `components/mdns/` | third-party code |

Clone with `git clone --recursive`, or run `git submodule update --init` afterwards.

## Documentation

- [Coding style](Documentation/CodingStyle.md) (format with `clang-format` 14 or newer)
- [Bluetooth LE](Documentation/bluetooth.md)
- [Memory and stability audit](Documentation/MemoryAudit.md)

## License

MIT, see [LICENSE](LICENSE). Third-party components keep their own licenses.
