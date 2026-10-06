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
    '...0...00...0...',
    '..0..........0..',
    '..0..........0..',
    '.0............0.',
    '000....00....000',
    '000....00....000',
    '.0............0.',
    '..0..........0..',
    '..0..........0..',
    '...0...00...0...',
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


def sd_card():
    icon = blank()
    outline = [(x, 0) for x in range(3, 10)] + [(10, 1), (11, 2)]
    outline += [(x, 15) for x in range(3, 13)]
    for y in range(1, 15):
        outline.append((3, y))
    for y in range(3, 15):
        outline.append((12, y))
    outline += [(3, 0)]
    for x, y in outline:
        icon[y][x] = '0'
    fill(icon, 6, 8, '6')
    for y in range(2, 6):  # contacts
        for x in (5, 7, 9):
            icon[y][x] = '0'
    return icon


def no_sd_card():
    icon = sd_card()
    slash(icon)
    return icon


WIFI_ARC_OUTER = ['.....000000.....', '..000......000..', '.00..........00.', '.0............0.']
WIFI_ARC_MIDDLE = ['.....000000.....', '...000....000...', '..00........00..']
WIFI_ARC_INNER = ['.....000000.....', '....00....00....']


def wifi(arcs, off=False):
    """arcs is the signal strength from 0 to 3"""
    icon = blank()
    for visible, top, art in ((arcs >= 3, 3, WIFI_ARC_OUTER), (arcs >= 2, 7, WIFI_ARC_MIDDLE),
                              (arcs >= 1, 10, WIFI_ARC_INNER)):
        if visible:
            for dy, row in enumerate(art):
                for x, c in enumerate(row):
                    if c == '0':
                        icon[top + dy][x] = '0'
    for y in (13, 14):
        for x in (7, 8):
            icon[y][x] = '0'
    if off:
        slash(icon)
    return icon


WIFI_AP_ART = [
    '................',
    '..0..........0..',
    '.0...0....0...0.',
    '0...0......0...0',
    '0...0..00..0...0',
    '0...0.0000.0...0',
    '0...0..00..0...0',
    '.0...0.00.0...0.',
    '..0....00....0..',
    '.......00.......',
    '......0..0......',
    '......0..0......',
    '.....0....0.....',
    '.....0....0.....',
    '....0......0....',
    '....00000000....',
]


def battery(rows):
    icon = blank()
    for x in range(6, 10):  # terminal
        for y in (0, 1):
            icon[y][x] = '0'
    for x in range(4, 12):
        icon[2][x] = icon[15][x] = '0'
    for y in range(2, 16):
        icon[y][4] = icon[y][11] = '0'
    for y in range(15 - rows, 15):
        for x in range(5, 11):
            icon[y][x] = '2'
    return icon


NORTH_ART = [
    '.......00.......',
    '......0.00......',
    '......0.00......',
    '.....0..000.....',
    '.....0..000.....',
    '....0...0000....',
    '....0...0000....',
    '...0....00000...',
    '...0000000000...',
    '................',
    '.....0...0......',
    '.....00..0......',
    '.....0.0.0......',
    '.....0..00......',
    '.....0...0......',
    '.....0...0......',
]

PATH_ART = [
    '..........0000..',
    '.........0....0.',
    '.........0.00.0.',
    '.........0.00.0.',
    '.........0....0.',
    '..........0..0..',
    '...........00...',
    '..........0.....',
    '.........0......',
    '..0000000.......',
    '.0..............',
    '.0..............',
    '..0000000.......',
    '.........0......',
    '..........0.....',
    '.........00.....',
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
    'bat_0.png': battery(0),
    'bat_10.png': battery(2),
    'bat_30.png': battery(4),
    'bat_50.png': battery(6),
    'bat_80.png': battery(9),
    'bat_100.png': battery(12),
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
