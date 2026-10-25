#!/usr/bin/env python3
"""Generates the "searching for position" picture of the map screen in lib/artwork.

Usage: tools/generate_searching_image.py [--preview preview.png]

The hiker and the logo come from tools/art/splash.png (the splash screen of IndiaNavi_Converter).
Every pixel is put into a class (outline, logo bands, backpack, hat, shoes, ...) and every class
gets a paint: one ink of the 7-color e-paper or two inks in a 4x4 pattern, anchored at the
screen pixel like the patterns of the vector map (docs/COLOR_SCHEMA.md of mapsforge-rs). The
gradient of the lettering becomes three bands of red, orange and yellow, there is no mushy
error diffusion.

The picture is stored run length encoded with the paint index (searching_position.c), it is
decoded while it is drawn, no buffer is needed:
    byte = paint << 4 | n     n = 1..15: a run of n pixels
                              n = 0: the run length is the next byte + 16 (16..271)
Paint 0 is transparent, the background is left as it is.

Needs Pillow and numpy, and the DejaVu fonts for the text.
"""
import argparse
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SOURCE = os.path.join(ROOT, 'tools', 'art', 'splash.png')
TARGET = os.path.join(ROOT, 'lib', 'artwork', 'searching_position.c')

FONT_DIRS = ['/usr/share/fonts/TTF', '/usr/share/fonts/truetype/dejavu', '/usr/share/fonts/dejavu']

# picture size, it is drawn below the top bar and ends above the scale and the height graph
WIDTH = 448
HEIGHT = 500

TEXT = 'Searching for GPS...'
SUBTEXT = 'The map appears once the position is known'
CREDIT = ('Platinenmacher &', 'theBrutzler')

# inks of the display (color_t)
BLACK, WHITE, GREEN, BLUE, RED, YELLOW, ORANGE = range(7)
INK_RGB = {BLACK: (0, 0, 0), WHITE: (255, 255, 255), GREEN: (0, 160, 0), BLUE: (0, 0, 255),
           RED: (220, 0, 0), YELLOW: (255, 230, 0), ORANGE: (255, 128, 0)}

# 4x4 masks, bit (y * 4 + x) set: ink a, else ink b. Levels of the Bayer matrix like mfr_raster.
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def bayer_mask(level):
    """Mask with ink a on level/16 of the pixels."""
    mask = 0
    for y in range(4):
        for x in range(4):
            if BAYER[y][x] < level:
                mask |= 1 << (y * 4 + x)
    return mask


SOLID = 0xffff
P50 = bayer_mask(8)
P25 = bayer_mask(4)

# the classes of the picture and their paint (ink a, ink b, mask), index 0 is transparent
PAINTS = [
    ('none', WHITE, WHITE, 0),
    ('outline', BLACK, BLACK, SOLID),
    ('red', RED, RED, SOLID),
    ('orange', ORANGE, ORANGE, SOLID),
    ('yellow', YELLOW, YELLOW, SOLID),
    ('green', GREEN, GREEN, SOLID),
    ('dark_green', BLACK, GREEN, P50),     # shadow of the backpack
    ('blue', BLUE, BLUE, SOLID),
    ('hat', BLACK, ORANGE, P50),           # brown crown
    ('brim', BLACK, ORANGE, P25),          # lighter brown brim
    ('white', WHITE, WHITE, SOLID),
    ('panel', BLUE, WHITE, P25),           # light blue solar panels, white sheen
]
CLASS = {name: i for i, (name, *_rest) in enumerate(PAINTS)}


def font(name, size):
    for d in FONT_DIRS:
        path = os.path.join(d, name)
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    raise SystemExit(f'font {name} not found in {FONT_DIRS}')


def classify(rgb):
    """Class of every pixel of the splash screen (numpy, full resolution)."""
    r, g, b = [rgb[..., i].astype(np.int32) for i in range(3)]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    sat = mx - mn
    lum = (299 * r + 587 * g + 114 * b) // 1000
    h, w = r.shape
    ys, xs = np.mgrid[0:h, 0:w]

    out = np.full((h, w), CLASS['none'], np.uint8)
    gray = sat < 40
    out[gray & (lum < 128)] = CLASS['outline']

    colored = ~gray
    dark = colored & (mx < 90)
    out[dark] = CLASS['outline']
    colored &= ~dark

    greenish = colored & (g > r + 40) & (g > b + 40)
    out[greenish & (g >= 190)] = CLASS['green']
    out[greenish & (g < 190)] = CLASS['dark_green']

    blueish = colored & (b > r + 40) & (b > g + 40)
    out[blueish] = CLASS['blue']

    hat_box = (xs >= 265) & (xs <= 370) & (ys >= 260) & (ys <= 330)
    brown = colored & hat_box & (sat < 90) & (r > 130)
    out[brown & (r < 175)] = CLASS['hat']
    out[brown & (r >= 175)] = CLASS['brim']
    band = colored & hat_box & ~brown & (r > g + 60) & (r > b + 60)
    out[band] = CLASS['red']

    # the lettering: red at the top to yellow at the bottom, in three bands
    warm = colored & ~greenish & ~blueish & ~(hat_box & (brown | band)) & (r >= g) & (r > b + 60)
    hue = 60.0 * (g - b) / np.maximum(sat, 1)
    out[warm & (hue < 16)] = CLASS['red']
    out[warm & (hue >= 16) & (hue < 40)] = CLASS['orange']
    out[warm & (hue >= 40)] = CLASS['yellow']
    return out


def downscale(classes, scale):
    """Scales the class map, every pixel gets the class with most area, outlines are preferred."""
    h, w = classes.shape
    size = (round(w * scale), round(h * scale))
    weights = []
    for c in range(len(PAINTS)):
        layer = Image.fromarray(((classes == c) * 255).astype(np.uint8))
        weights.append(np.asarray(layer.resize(size, Image.BOX), np.float32))
    weights = np.stack(weights)
    weights[CLASS['outline']] *= 1.6   # thin lines must not break
    weights[CLASS['none']] *= 0.9
    return np.argmax(weights, axis=0).astype(np.uint8)


def paste(canvas, part, left, top):
    h, w = part.shape
    region = canvas[top:top + h, left:left + w]
    region[part != CLASS['none']] = part[part != CLASS['none']]


def draw_mask(size, fn):
    """Draws with fn(ImageDraw) into a 1 bit mask of the picture size."""
    img = Image.new('1', size, 0)
    fn(ImageDraw.Draw(img))
    return np.asarray(img, bool)


def satellite(canvas, cx, cy, angle):
    """Satellite with two solar panels, rotated by angle (degrees)."""
    a = math.radians(angle)
    ca, sa = math.cos(a), math.sin(a)

    def tr(points):
        return [(cx + x * ca - y * sa, cy + x * sa + y * ca) for x, y in points]

    def rect(x0, y0, x1, y1):
        return tr([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])

    size = (canvas.shape[1], canvas.shape[0])
    for side in (-1, 1):
        x0, x1 = sorted((side * 20, side * 66))
        poly = rect(x0, -15, x1, 15)
        canvas[draw_mask(size, lambda d: d.polygon(poly, fill=1))] = CLASS['panel']
        def grid(d, x0=x0, x1=x1, poly=poly):
            for x in range(x0 + 15, x1, 15):
                d.line(rect(x, -15, x, 15)[:2], fill=1, width=2)
            d.line(rect(x0, 0, x1, 0)[:2], fill=1, width=2)
            d.polygon(poly, outline=1, width=3)
        canvas[draw_mask(size, grid)] = CLASS['outline']
    # arm between body and panels
    arm = rect(-20, -3, 20, 3)
    canvas[draw_mask(size, lambda d: d.polygon(arm, fill=1))] = CLASS['outline']
    body = rect(-14, -18, 14, 18)
    canvas[draw_mask(size, lambda d: d.polygon(body, fill=1))] = CLASS['orange']
    canvas[draw_mask(size, lambda d: d.polygon(rect(-14, -18, 14, -8), fill=1))] = CLASS['yellow']
    canvas[draw_mask(size, lambda d: d.polygon(body, outline=1, width=3))] = CLASS['outline']
    # antenna dish pointing down to the hiker
    dish = tr([(-12, 18), (12, 18), (8, 26), (-8, 26)])
    canvas[draw_mask(size, lambda d: d.polygon(dish, fill=1))] = CLASS['outline']
    tip = tr([(0, 26), (0, 34)])
    canvas[draw_mask(size, lambda d: d.line(tip, fill=1, width=3))] = CLASS['outline']


def signal(canvas, cx, cy, direction, radii, color):
    """Arcs around cx/cy opening towards direction (degrees, 0 is right, 90 is down)."""
    size = (canvas.shape[1], canvas.shape[0])
    for r in radii:
        box = (cx - r, cy - r, cx + r, cy + r)
        m = draw_mask(size, lambda d: d.arc(box, direction - 28, direction + 28, fill=1, width=6))
        canvas[m] = CLASS['outline']
        m = draw_mask(size, lambda d: d.arc([v + (2 if i < 2 else -2) for i, v in enumerate(box)],
                                            direction - 24, direction + 24, fill=1, width=2))
        canvas[m] = CLASS[color]


def text(canvas, string, fnt, top, cls, center=None, left=None):
    size = (canvas.shape[1], canvas.shape[0])
    width = fnt.getbbox(string)[2]
    x = left if left is not None else ((center if center else size[0] / 2) - width / 2)

    def draw(d):
        d.fontmode = '1'   # no anti aliasing, there is no gray
        d.text((x, top), string, font=fnt, fill=1)
    canvas[draw_mask(size, draw)] = CLASS[cls]


def build():
    src = Image.open(SOURCE).convert('RGBA')
    white = Image.new('RGBA', src.size, (255, 255, 255, 255))
    white.alpha_composite(src)
    rgb = np.asarray(white.convert('RGB'))
    classes = classify(rgb)
    # the credit is too small to scale, it is written again below
    classes[520:575, 345:] = CLASS['none']

    # logo and hiker as on the splash screen
    left, top, right, bottom = 55, 66, 360, 566
    scale = 0.76
    art = downscale(classes[top:bottom, left:right], scale)

    canvas = np.full((HEIGHT, WIDTH), CLASS['none'], np.uint8)
    art_left, art_top = 18, 0
    paste(canvas, art, art_left, art_top)

    # the hat in the scaled picture, the signal goes there
    hat_x = art_left + (318 - left) * scale
    hat_y = art_top + (282 - top) * scale

    sat_x, sat_y = 374, 76
    direction = math.degrees(math.atan2(hat_y - sat_y, hat_x - sat_x))
    # the dish points to the hiker
    satellite(canvas, sat_x, sat_y, direction - 90)
    signal(canvas, sat_x, sat_y, direction, (60, 82, 104), 'red')

    credit_font = font('DejaVuSans-Bold.ttf', 11)
    credit_left = art_left + round((322 - left) * scale) + 16
    for i, line in enumerate(CREDIT):
        text(canvas, line, credit_font, 330 + i * 13, 'outline', left=credit_left)

    text(canvas, TEXT, font('DejaVuSans-Bold.ttf', 30), 400, 'outline')
    text(canvas, SUBTEXT, font('DejaVuSansCondensed-Bold.ttf', 16), 444, 'outline')
    # an underline in the colors of the logo
    for i, cls in enumerate(('red', 'orange', 'yellow')):
        canvas[438:442, 74 + i * 100:74 + (i + 1) * 100] = CLASS[cls]
    return canvas


def encode(canvas):
    flat = canvas.flatten()
    out = bytearray()
    i = 0
    while i < len(flat):
        c = int(flat[i])
        n = 1
        while i + n < len(flat) and flat[i + n] == c and n < 271:
            n += 1
        if n < 16:
            out.append(c << 4 | n)
        else:
            out += bytes((c << 4, n - 16))
        i += n
    return bytes(out)


def decode(data, width, height):
    """Same as artwork_draw in artwork.c, for the preview."""
    img = np.full((height, width, 3), 255, np.uint8)
    pos = 0
    i = 0
    while i < len(data):
        c, n = data[i] >> 4, data[i] & 0xf
        i += 1
        if n == 0:
            n = data[i] + 16
            i += 1
        _, a, b, mask = PAINTS[c]
        for p in range(pos, pos + n):
            if c:
                y, x = divmod(p, width)
                ink = a if mask >> ((y & 3) * 4 + (x & 3)) & 1 else b
                img[y, x] = INK_RGB[ink]
        pos += n
    assert pos == width * height
    return img


def write_c(data):
    lines = [
        '/* Generated by tools/generate_searching_image.py, do not edit. */',
        '#include "artwork.h"',
        '',
        'static const artwork_paint_t paints[] = {',
    ]
    for name, a, b, mask in PAINTS:
        lines.append(f'    {{ {a}, {b}, 0x{mask:04x} }}, // {name}')
    lines += ['};', '', f'static const uint8_t data[{len(data)}] = {{']
    for i in range(0, len(data), 16):
        lines.append('    ' + ' '.join(f'0x{v:02x},' for v in data[i:i + 16]))
    lines += [
        '};',
        '',
        'const artwork_t searching_position_artwork = {',
        f'    .width = {WIDTH},',
        f'    .height = {HEIGHT},',
        '    .paints = paints,',
        f'    .paint_count = {len(PAINTS)},',
        '    .data = data,',
        '    .data_size = sizeof(data),',
        '};',
        '',
    ]
    with open(TARGET, 'w') as f:
        f.write('\n'.join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--preview', help='also save the decoded picture as PNG, 2x')
    args = parser.parse_args()
    data = encode(build())
    write_c(data)
    print(f'{TARGET}: {len(data)} bytes')
    if args.preview:
        img = Image.fromarray(decode(data, WIDTH, HEIGHT))
        img.resize((WIDTH * 2, HEIGHT * 2), Image.NEAREST).save(args.preview)


if __name__ == '__main__':
    main()
