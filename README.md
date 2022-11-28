# India Navi firmware

India Navi is a 7-color ePaper offline navigation device. It is designed for hiking and not getting lost. It is not intended for realtime way finding.

## Firmware OTA update

Work in progress

## Workflow for creating SD cards

The SD card needs to be fromated in exfat/fat32. It comes with several files:

    ├── MAPS <- Map files go here
    │   ├── 0 <- Splash screen files
    │   ├── 14 <- Files for zoom level 14
    │   └── 16 <- Files for zoom level 16
    ├── OTA <- Update URL (work in progress)
    ├── TIMEZONE <- Timezone
    ├── track.gpx <- Track to render on map
    └── WIFI <- WiFi access data for OTA

To generate Map files use the project [India Navi Converter](https://github.com/DasBasti/IndiaNavi_Converter/tree/5-get-data-from-opentopomaporg)
