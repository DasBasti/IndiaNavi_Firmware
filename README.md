# India Navi firmware

India Navi is a 7-color ePaper offline navigation device. It is designed for hiking and getting not for realtime way finding.

## Firmware OTA update

To update the firmware OTA you need to insert a SD card with a file called ```WIFI``` in it's root directory.
The file must contain SSID and Password in two seperate lines and nothing else
```
WiFi_SSID
Network_Password
```
This allows the system to connect to a wifi network. Firmware file must be served on `http://laptop.local:8070/firmware.bin` this might change with a file on the SD card. The file `OTA` must contain the URL to download the firmware image.

Pushing the User button for a second will trigger the update. During the update nothing happens and afert ~10 seconds the device reboots. Update status can be seen in HTTP server log. A failed download is indicated by a resetted connection to the server.  