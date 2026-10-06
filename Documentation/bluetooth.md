# Bluetooth LE

The ESP32-S3 board is a Bluetooth LE peripheral (NimBLE). The protocol, the byte layouts, the pairing rules and
the firmware update are described in the app repository: `IndiaNavi_App/docs/ble_api.md`.

Where it is in the code:

| File | Content |
|---|---|
| `lib/ble_protocol` | encoding and decoding of all values, tested on the host (`test/host/Platinenmacher/test_ble_protocol`) |
| `src/esp32/ble.c` | GATT service, advertising, pairing, notifications |
| `src/esp32/ble_ota.c` | firmware update over Bluetooth (own task, flow control) |
| `src/esp32/fw_update.c` | writing the image, shared with the HTTP firmware upload, progress for the display |
| `src/esp32/display_settings.c` | track, height graph and update interval, stored in NVS |
| `src/esp32/gps.c` | `gps_set_time_from_phone()`, `gps_set_position_from_phone()` (PMTK740 / PMTK741) |
| `src/esp32/gui.c` | passkey box, firmware progress box, update interval |

Bluetooth is started by the main task when the GUI leaves the off screen (`TASK_EVENT_ENABLE_BLE`) and stopped
when the off screen is shown (`TASK_EVENT_DISABLE_BLE`). Other boards build without it (`ESP_S3` is not defined).
