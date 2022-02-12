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

## Workflow
- GPX Karte auf Webseite hochladen + ID vom Gerät (id)
- Website gibt Status aus.
- Gerät nach Halten des Buttons für 2 Sekunden in Downloadmodus setzen.
- Gerät fragt nach http://platinenmacher.tech/navi/.../id
  - 404 -> Keine Karte vorhanden -> Anzeige des Fehlers und beenden des Modus
  - 202 -> Karte noch in Bearbeitung
  - 200 -> Karte kann runtergeladen werden -> Download URL + Zoom(1/2) + Folder (von-bis) + File (von-bis)
                                          - http://platinenmacher.tech/navi/files/tiles/[14,16]/[xxxx-yyyy]/[aaaa-bbbb].raw
  - Loop zum Runterladen der Tiles auf die SD-Karte
    - GET Download
    - Update Screen
  - Nach Download Modus verlassen
  - Button halten für 2 Sekunden verlässt Modus



## ToDo
- [ ] Workflow streamlinen
- [ ] Map Pakete runterladen (WiFi vom Server/Amazon)


- [ ] Geräte Config in JSON
- [ ] TIMEZONE!