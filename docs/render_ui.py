"""Render the key_led OLED screens off-line so the layout can be checked
without flashing. Mirrors the C drawing code 1:1 (same font tables, same
primitives) and writes a 2x2 contact sheet.
"""
import re
import sys
from PIL import Image

import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, 'Core', 'OLED', 'oledfont.h')
OUT = os.path.join(HERE, 'screens.png')

W, H = 128, 64

# ------------------------------------------------------------------ fonts
txt = open(SRC, 'rb').read().decode('gbk', errors='replace')


def grab(name, per):
    i = txt.find(name)
    if i < 0:
        raise SystemExit('font %s not found' % name)
    j = txt.find('};', i)
    vals = re.findall(r'0x[0-9a-fA-F]{2}', txt[i:j])
    out = []
    for k in range(0, len(vals) - per + 1, per):
        out.append([int(v, 16) for v in vals[k:k + per]])
    return out


F6 = grab('F6x8', 6)
F8 = grab('F8X16', 16)
print('glyphs: F6x8=%d  F8X16=%d' % (len(F6), len(F8)))

# ---------------------------------------------------------------- canvas
fb = [[0] * W for _ in range(H)]
draw_on = 1


def sp(x, y, on=1):
    if 0 <= x < W and 0 <= y < H:
        fb[y][x] = 1 if on else 0


def hline(x0, x1, y, on=1):
    if x0 > x1:
        x0, x1 = x1, x0
    for x in range(x0, x1 + 1):
        sp(x, y, on)


def vline(x, y0, y1, on=1):
    if y0 > y1:
        y0, y1 = y1, y0
    for y in range(y0, y1 + 1):
        sp(x, y, on)


def rect(x0, y0, x1, y1, on=1):
    if x0 > x1:
        x0, x1 = x1, x0
    if y0 > y1:
        y0, y1 = y1, y0
    hline(x0, x1, y0, on)
    hline(x0, x1, y1, on)
    vline(x, y0, y1, on) if False else None
    vline(x0, y0, y1, on)
    vline(x1, y0, y1, on)


def fillrect(x0, y0, x1, y1, on=1):
    if x0 > x1:
        x0, x1 = x1, x0
    if y0 > y1:
        y0, y1 = y1, y0
    for y in range(y0, y1 + 1):
        hline(x0, x1, y, on)


def circle(cx, cy, r, on=1):
    x, y, err = r, 0, 1 - r
    while x >= y:
        for dx, dy in ((x, y), (y, x), (-y, x), (-x, y),
                       (-x, -y), (-y, -x), (y, -x), (x, -y)):
            sp(cx + dx, cy + dy, on)
        y += 1
        if err < 0:
            err += 2 * y + 1
        else:
            x -= 1
            err += 2 * (y - x) + 1


def glyph(x, y, ch, size, scale):
    c = ord(ch) - 32
    if c < 0:
        return
    if size == 16:
        if c >= len(F8):
            return
        base, w, parts = F8[c], 8, 2
    else:
        if c >= len(F6):
            return
        base, w, parts = F6[c], 6, 1
    for part in range(parts):
        for i in range(w):
            b = base[i + part * 8]
            for r in range(8):
                if (b >> r) & 1:
                    for dy in range(scale):
                        for dx in range(scale):
                            sp(x + i * scale + dx,
                               y + (part * 8 + r) * scale + dy, draw_on)


def text(x, y, s, size, scale=1):
    gw = (8 if size == 16 else 6) * scale
    for ch in s:
        glyph(x, y, ch, size, scale)
        x += gw


def center(y, s, size, scale=1):
    gw = (8 if size == 16 else 6) * scale
    w = len(s) * gw
    x = (W - w) // 2 if w < W else 0
    text(x, y, s, size, scale)


def clear():
    global fb
    fb = [[0] * W for _ in range(H)]


# ------------------------------------------------------------------ screens
def screen_idle():
    clear()
    rect(0, 0, 127, 63)
    hline(0, 127, 11)
    fillrect(4, 4, 7, 7)
    text(11, 2, 'STANDBY', 8)
    center(20, 'BOMB READY', 16)
    hline(24, 103, 42)
    center(48, 'PRESS KEY1', 8)


def screen_countdown(remain_ms):
    global draw_on
    clear()
    rect(0, 0, 127, 63)
    hline(0, 127, 11)
    text(4, 2, 'ARMED', 8)
    if (remain_ms // 500) & 1 == 0:
        fillrect(118, 4, 121, 7)
    else:
        rect(118, 4, 121, 7)

    sec = (remain_ms + 999) // 1000
    for t, digit in enumerate('%02d' % sec):
        glyph(48 + t * 16, 15, digit, 16, 2)

    rect(8, 50, 119, 58)
    bar = remain_ms * 108 // 10000
    bar = min(bar, 108)
    if bar > 0:
        fillrect(10, 52, 10 + bar - 1, 56)


def screen_defused():
    clear()
    rect(0, 0, 127, 63)
    circle(64, 14, 9)
    fillrect(59, 9, 60, 10)
    fillrect(68, 9, 69, 10)
    hline(61, 67, 19)
    sp(60, 18)
    sp(68, 18)
    sp(59, 17)
    sp(69, 17)
    center(27, 'DEFUSED', 16)
    hline(32, 95, 47)
    center(51, 'SAFE', 8)


def screen_failed():
    clear()
    rect(0, 0, 127, 63)
    center(14, 'FAILED', 16)
    hline(40, 87, 44)
    center(50, 'BAD LUCK', 8)


def screen_boom(invert):
    global draw_on
    clear()
    if invert:
        fillrect(0, 0, 127, 63)
        draw_on = 0
        rect(2, 2, 125, 61)
        center(16, 'BOOM', 16, 2)
        draw_on = 1
    else:
        rect(0, 0, 127, 63)
        center(16, 'BOOM', 16, 2)


# ------------------------------------------------------------------ output
SCALE = 5
PAD = 10
panels = []

for fn in (screen_idle, lambda: screen_countdown(10000),
           lambda: screen_countdown(6000), lambda: screen_countdown(2000),
           screen_defused, screen_failed,
           lambda: screen_boom(0), lambda: screen_boom(1)):
    fn()
    img = Image.new('RGB', (W, H), (4, 8, 14))
    px = img.load()
    for y in range(H):
        for x in range(W):
            if fb[y][x]:
                px[x, y] = (130, 215, 255)
    panels.append(img.resize((W * SCALE, H * SCALE), Image.NEAREST))

cols, rows = 4, 2
sheet = Image.new('RGB', (cols * (W * SCALE + PAD) + PAD,
                          rows * (H * SCALE + PAD) + PAD), (30, 34, 40))
for k, p in enumerate(panels):
    cx = PAD + (k % cols) * (W * SCALE + PAD)
    cy = PAD + (k // cols) * (H * SCALE + PAD)
    sheet.paste(p, (cx, cy))

import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
sheet.save(OUT)
print('written', OUT, sheet.size)

# text-width sanity checks (what used to overflow and wrap)
for s, size, scale in (('DEFUSED', 16, 1), ('BOMB READY', 16, 1),
                       ('PRESS KEY1', 8, 1), ('BOOM', 16, 2)):
    gw = (8 if size == 16 else 6) * scale
    w = len(s) * gw
    print('%-12s w=%3d  x=%2d  right edge=%3d  %s'
          % (s, w, (128 - w) // 2, (128 - w) // 2 + w - 1,
             'OK' if w <= 128 else 'OVERFLOW'))
