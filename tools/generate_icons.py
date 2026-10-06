#!/usr/bin/env python3
"""Generates the 16x16 icons of the display in lib/icons_16.

Usage: tools/generate_icons.py

Every icon is 4 bit per pixel, the value is the index of the display color:
0 black, 1 white, 2 green, 3 blue, 4 red, 5 yellow, 6 orange, 7 transparent.
Run tools/generate_icons.py of IndiaNavi_App afterwards to update the icons of the app.
"""
import math
import os

SIZE = 16
TARGET = os.path.join(os.path.dirname(__file__), '..', 'lib', 'icons_16')
TRANSPARENT = '7'


def blank():
    return [[TRANSPARENT] * SIZE for _ in range(SIZE)]


def from_art(art, colors):
    """'.' is transparent, every other character is looked up in colors."""
    assert len(art) == SIZE and all(len(row) == SIZE for row in art), 'icon is not 16x16'
    return [[colors.get(c, TRANSPARENT) if c != '.' else TRANSPARENT for c in row] for row in art]


def fill(icon, x, y, color):
    """Fills the transparent area around x, y, the outline has to be closed."""
    stack = [(x, y)]
    while stack:
        x, y = stack.pop()
        if 0 <= x < SIZE and 0 <= y < SIZE and icon[y][x] == TRANSPARENT:
            icon[y][x] = color
            stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]


def slash(icon, color='4'):
    """Strikes the icon through from the bottom left to the top right."""
    for y in range(SIZE):
        for x in (SIZE - 1 - y, SIZE - 2 - y):
            if 0 <= x < SIZE:
                icon[y][x] = color


GPS_ART = [
    '.......00.......',
    '.......00.......',
    '....00000000....',
    '...0000000000...',
    '..00........00..',
    '.00..........00.',
    '.00..........00.',
    '0000...00...0000',
    '0000...00...0000',
    '.00..........00.',
    '.00..........00.',
    '..00........00..',
    '...0000000000...',
    '....00000000....',
    '.......00.......',
    '.......00.......',
]


def gps():
    return from_art(GPS_ART, {'0': '0'})


def gps_locked(color):
    icon = gps()
    fill(icon, 5, 6, color)
    for y in (7, 8):
        for x in (7, 8):
            icon[y][x] = '0'
    return icon


def no_gps():
    icon = gps()
    slash(icon)
    return icon


SD_ART = [
    '...00000000.....',
    '...000000000....',
    '...0060606600...',
    '...0060606600...',
    '...0060606600...',
    '...0066666600...',
    '...0066666600...',
    '...0066666600...',
    '...0066666600...',
    '...0066666600...',
    '...0066666600...',
    '...0066666600...',
    '...0066666600...',
    '...0066666600...',
    '...0000000000...',
    '...0000000000...',
]


def sd_card():
    return from_art(SD_ART, {'0': '0', '6': '6'})


def no_sd_card():
    icon = sd_card()
    slash(icon)
    return icon


WIFI_ARC_OUTER = ['.....000000.....', '..000......000..', '.00..........00.', '.0............0.']
WIFI_ARC_MIDDLE = ['.....000000.....', '...000....000...', '..00........00..']
WIFI_ARC_INNER = ['.....000000.....', '....00....00....']


def wifi(arcs, off=False):
    """arcs is the signal strength from 0 to 3. The arcs are drawn twice, one row apart, so they are two pixels thick."""
    icon = blank()
    for visible, top, art in ((arcs >= 3, 0, WIFI_ARC_OUTER), (arcs >= 2, 6, WIFI_ARC_MIDDLE),
                              (arcs >= 1, 11, WIFI_ARC_INNER)):
        if visible:
            for dy, row in enumerate(art):
                for x, c in enumerate(row):
                    if c == '0':
                        icon[top + dy][x] = icon[top + dy + 1][x] = '0'
    # the ends of the outer arc are steep, they get a second column
    if arcs >= 3:
        for y in (3, 4):
            icon[y][2] = icon[y][13] = '0'
    for y in (14, 15):
        for x in (7, 8):
            icon[y][x] = '0'
    if off:
        slash(icon)
    return icon


WIFI_AP_ART = [
    '................',
    '.00..........00.',
    '00...00..00...00',
    '00..00....00..00',
    '00..00.00.00..00',
    '00..00.00.00..00',
    '00..00....00..00',
    '00...00..00...00',
    '.00....00....00.',
    '.......00.......',
    '......0000......',
    '.....00..00.....',
    '.....00..00.....',
    '....00....00....',
    '....00000000....',
    '....00000000....',
]



def ble(connected):
    """The Bluetooth rune, with a dot on both sides while a phone is connected. The lines are two pixels wide."""
    icon = blank()
    for y in range(SIZE):  # spine
        icon[y][7] = icon[y][8] = '0'
    for y in range(3, 13):  # the arms cross the spine
        for x in (y - 1, y):
            icon[y][x] = '0'  # top left to bottom right
            icon[15 - y][x] = '0'  # bottom left to top right
    for y, x in ((1, 9), (2, 10), (13, 10), (14, 9)):  # the tips go back to the spine
        icon[y][x] = icon[y][x + 1] = '0'
    if connected:
        for x in (0, 1, 13, 14):
            for y in (7, 8):
                icon[y][x] = '3'
    return icon


def battery(rows):
    """rows is the charge from 0 to 10"""
    icon = blank()
    for x in range(6, 10):  # terminal
        for y in (0, 1):
            icon[y][x] = '0'
    for x in range(4, 12):
        icon[2][x] = icon[3][x] = icon[14][x] = icon[15][x] = '0'
    for y in range(2, 16):
        icon[y][4] = icon[y][5] = icon[y][10] = icon[y][11] = '0'
    for y in range(14 - rows, 14):
        for x in range(6, 10):
            icon[y][x] = '2'
    return icon


NORTH_ART = [
    '.......00.......',
    '......0000......',
    '......0000......',
    '.....00.000.....',
    '.....00.000.....',
    '....00..0000....',
    '....00..0000....',
    '...0000000000...',
    '...0000000000...',
    '................',
    '....00...00.....',
    '....000..00.....',
    '....0000.00.....',
    '....00.0000.....',
    '....00..000.....',
    '....00...00.....',
]

PATH_ART = [
    '..........0000..',
    '.........000000.',
    '.........00..00.',
    '.........00..00.',
    '.........000000.',
    '..........0000..',
    '...........00...',
    '..........00....',
    '.........00.....',
    '..00000000......',
    '.00000000.......',
    '.00.............',
    '.00000000.......',
    '..00000000......',
    '.........00.....',
    '..........00....',
]

ICONS = {
    'GPS.png': gps(),
    'GPS_lock': gps_locked('2'),
    'GPS_search': gps_locked('5'),
    'noGPS.png': no_gps(),
    'SD.png': sd_card(),
    'noSD': no_sd_card(),
    'WIFI_0': wifi(0, off=True),
    'WIFI_1': wifi(1),
    'WIFI_2': wifi(2),
    'WIFI_3': wifi(3),
    'WIFI_AP': from_art(WIFI_AP_ART, {'0': '0'}),
    'BLE': ble(False),
    'BLE_conn': ble(True),
    'bat_0.png': battery(0),
    'bat_10.png': battery(2),
    'bat_30.png': battery(3),
    'bat_50.png': battery(5),
    'bat_80.png': battery(8),
    'bat_100.png': battery(10),
    'norden': from_art(NORTH_ART, {'0': '0'}),
    'path.png': from_art(PATH_ART, {'0': '0'}),
}

# Names of the C arrays, files that end with .png keep the name of the old icons
SYMBOLS = {name: name.split('.')[0] for name in ICONS}


def to_bytes(icon):
    pixels = [int(c) for row in icon for c in row]
    return [(pixels[i] << 4) | pixels[i + 1] for i in range(0, len(pixels), 2)]


def main():
    for name, icon in ICONS.items():
        symbol = SYMBOLS[name]
        data = to_bytes(icon)
        lines = ['#include <stdint.h>', f'const uint8_t {symbol}[] ={{']
        for i in range(0, len(data), 8):
            lines.append(', '.join(f'0x{b:02x}' for b in data[i:i + 8]) + ', ')
        lines.append('};')
        path = os.path.join(TARGET, f'{name}.c')
        with open(path, 'w') as out:
            out.write('\n'.join(lines) + '\n')
    if os.environ.get('PREVIEW'):
        for name, icon in ICONS.items():
            print(name)
            for row in icon:
                print(''.join(row).replace('7', '.'))


if __name__ == '__main__':
    main()
