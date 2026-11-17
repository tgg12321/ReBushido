#!/usr/bin/env python3
"""The built-in "english" mod of the native port (docs/MODS.md, "The
English menu"): the top menu's Japanese lines drawn again in English.

    python tools/port_english.py                 write src/port/mods/english/
    python tools/port_english.py --check         exit 1 if what is there is not
                                                 what this would write
    python tools/port_english.py --measure DIR   hold the table below to the
                                                 game's own textures, dumped
                                                 into DIR (--dump-textures)

The pictures are this project's own: text in two fonts of the SIL Open Font
Licence (src/port/fonts/), drawn at six times the console's size across and twelve down. Not a
pixel of the game's is in them or is read to make them, so they are kept in
the repository and this script makes the same bytes again. What was taken
from the game is in the table: each texture's key (its file's name), its
size, and where the game's own lettering sits in it, measured once with
--measure from a dump of the top menu.

The top menu (MOD.BIN, docs/formats/STAGE_BIN.md) draws each of its rows as
two sprites of 64 by 16 texels: the two characters of the mode's own name
(決戦, "decisive battle") and then の間, "the hall of", which is one texture
that every row shares. So a row's English is in two pieces as well: the
mode's name on the left, and "Mode", which every row shares, on the right,
which gives the names the US disc gives the modes itself (its captions, the
English line under the rows). 支度 ("preparations") is the options' row and
is "Setup" here, since "Mode" follows whatever is put before it.

The title over the rows, 試練項目選択 ("the choice of trials") with "TOP
MENU" spaced out under it, is four sprites 26 texels high, 210 wide in all,
drawn as one picture and cut up.

The lettering is white with a dark edge and a blue shadow down and to the
right, as the game's is; the game colours a row itself (a lit row gold, the
cursor's yellow), so white is what it is given. Needs Pillow.
"""
import argparse
import io
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'port', 'mods', 'english')
BRUSH = os.path.join(ROOT, 'src', 'port', 'fonts', 'KaushanScript-Regular.ttf')
ROMAN = os.path.join(ROOT, 'src', 'port', 'fonts', 'Marcellus-Regular.ttf')
# How many of the picture's pixels a texel of the game's is, across and
# down. The top menu is in the console's mode of 640 by 240, whose texel is
# twice as tall as it is wide on the screen; a pixel of these pictures is
# square there, so the letters are drawn as they are to be seen.
SCALE_X = 6
SCALE_Y = 12

MANIFEST = '''# ReBushido's English top menu: built in, made by tools/port_english.py
# (docs/MODS.md, "The English menu"). `english = off` in rebushido.cfg,
# or the settings menu, leaves it out.
name = English top menu
version = 1.0
order = -100      # before every other mod: any of yours of the same key wins
author = ReBushido
'''

# The rows: the key of the game's texture, and its English. The last is
# the piece every row shares.
ROWS = [
    ('64x16-4-b27f44ebfb2dde70', 'Story'),         # 決戦の間
    ('64x16-4-347418456a33427e', 'Vs. CPU'),       # 勝抜の間
    ('64x16-4-08aa7bc35b8174ad', 'Training'),      # 精進の間
    ('64x16-4-dc3d93f5f25a83e8', 'Vs.'),           # 対刃の間
    ('64x16-4-3304798ac9b9773e', 'Group Battle'),  # 対抗の間
    ('64x16-4-c490316a2f2167e4', 'Link'),          # 連結の間
    ('64x16-4-89cbb335c9cdaf89', 'Slash'),         # 百斬の間, until it is won
    ('64x16-4-1c096af18bd24ca7', 'Setup'),         # 支度の間
]
SHARED = ('64x16-4-3fb8c1aea8867615', 'Mode')      # の間
ROW_W, ROW_H = 64, 16
# where the game's characters are in a row's pieces, in texels: rows 1 to
# 14, from 3 to 61 across (--measure). The English capitals take rows 1 to
# 10, leaving the rest to the tails of g, p and y.
ROW_TOP, ROW_BOTTOM = 1, 10
ROW_LEFT, ROW_RIGHT = 2, 61

# The title's four pieces, left to right: key, width.
TITLE = [
    ('64x26-4-5c9315d0c585fdfb', 64),
    ('64x26-4-edfa2037098e8483', 64),
    ('64x26-4-069c956500dd78f2', 64),
    ('18x26-4-9a3735aff71297c0', 18),
]
TITLE_W, TITLE_H = 210, 26
TITLE_TEXT = 'Select a Trial'
# the big line in rows 1 to 16, from 5 to 205 across; the small letters
# of "TOP MENU" in rows 18 to 24, each about these middles
TITLE_TOP, TITLE_BOTTOM = 2, 15
TITLE_LEFT, TITLE_RIGHT = 8, 202
SMALL_TOP, SMALL_BOTTOM = 18, 24
SMALL_LETTERS = [('T', 14), ('O', 41), ('P', 68), ('M', 117), ('E', 144), ('N', 172), ('U', 198)]

# The colours, as the game's own lettering has them.
WHITE = (255, 255, 255)
PALE = (189, 197, 222)      # the bottom of a row's letters
EDGE = (0, 8, 24)           # the dark edge round them
SHADOW = (16, 41, 131)      # the blue shadow down and to the right
TITLE_WHITE = (230, 246, 255)
TITLE_PALE = (164, 180, 197)
TITLE_EDGE = (0, 8, 8)
TITLE_SHADOW = (41, 49, 57)
SMALL_INK = (41, 123, 148)  # the teal of "TOP MENU"
SMALL_EDGE = (8, 24, 32)
# the shadow's step down and to the right and the dark edge's width, in
# the pictures' pixels; how much narrower a long name may be drawn
# before it is drawn smaller
SHADOW_STEP = 9
EDGE_WIDTH = 5
SQUEEZE_MIN = 0.8


def font_fitting(path, text, box_w, box_h):
    """The font at the size whose capitals fill `box_h` pixels, and how
    much narrower the text is drawn to fit `box_w`: down to SQUEEZE_MIN,
    and smaller beyond that."""
    size = box_h * 2
    while size > 4:
        font = ImageFont.truetype(path, size)
        top, bottom = font.getbbox('H')[1], font.getbbox('H')[3]
        left, _, right, _ = font.getbbox(text)
        if bottom - top <= box_h and (right - left) * SQUEEZE_MIN <= box_w:
            break
        size -= 1
    return font, min(1.0, box_w / float(right - left))


def text_mask(path, text, box, align, canvas):
    """The letters of `text` as a mask of `canvas` (w, h), their capitals
    filling the rows of `box` (x0, y0, x1, y1, in pixels) and the text set
    to its left, right or middle, narrowed where it would not fit."""
    x0, y0, x1, y1 = box
    font, squeeze = font_fitting(path, text, x1 - x0, y1 - y0)
    left, _, right, bottom = font.getbbox(text)
    cap_top = font.getbbox('H')[1]
    margin = 4 * SCALE_X
    wide = Image.new('L', (right - left + 2 * margin, bottom + margin), 0)
    ImageDraw.Draw(wide).text((margin - left, 0), text, font=font, fill=255)
    if squeeze < 1.0:
        wide = wide.resize((max(1, int(wide.width * squeeze)), wide.height), Image.LANCZOS)
    width = (right - left) * squeeze
    if align == 'left':
        at_x = x0
    elif align == 'right':
        at_x = x1 - width
    else:
        at_x = (x0 + x1 - width) / 2.0
    mask = Image.new('L', canvas, 0)
    mask.paste(wide, (int(round(at_x - margin * squeeze)), int(y0 - cap_top)))
    return mask


def grow(mask, pixels):
    """The mask made thicker by `pixels` all round."""
    return mask.filter(ImageFilter.MaxFilter(2 * pixels + 1)) if pixels > 0 else mask


def shift(mask, dx, dy):
    """The mask moved by (dx, dy) pixels, what leaves its edge lost."""
    out = Image.new('L', mask.size, 0)
    out.paste(mask, (dx, dy))
    return out


def gradient(size, top, bottom, y0, y1):
    """Colour running from `top` at row y0 to `bottom` at row y1."""
    w, h = size
    out = Image.new('RGB', size)
    draw = ImageDraw.Draw(out)
    for y in range(h):
        t = min(1.0, max(0.0, (y - y0) / float(max(1, y1 - y0))))
        draw.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return out


def lettering(mask, colors, y0, y1):
    """The game's look over a mask: a dark edge round the letters and
    their shadow, the shadow, then the letters, white fading to pale."""
    white, pale, edge, shadowed = colors
    shadow = shift(mask, SHADOW_STEP, SHADOW_STEP)
    outline = grow(ImageChops.lighter(mask, shadow), EDGE_WIDTH)
    out = Image.new('RGBA', mask.size, (0, 0, 0, 0))
    out.paste(Image.new('RGBA', mask.size, edge + (255,)), (0, 0), outline)
    out.paste(Image.new('RGBA', mask.size, shadowed + (255,)), (0, 0), shadow)
    out.paste(gradient(mask.size, white, pale, y0, y1).convert('RGBA'), (0, 0), mask)
    return out


def row_piece(text, align):
    w, h = ROW_W * SCALE_X, ROW_H * SCALE_Y
    left = ROW_LEFT + (1 if align == 'left' else 0)
    box = (left * SCALE_X, ROW_TOP * SCALE_Y, (ROW_RIGHT - 1) * SCALE_X, ROW_BOTTOM * SCALE_Y)
    mask = text_mask(BRUSH, text, box, align, (w, h))
    return lettering(mask, (WHITE, PALE, EDGE, SHADOW), ROW_TOP * SCALE_Y, (ROW_BOTTOM + 1) * SCALE_Y)


def title_picture():
    w, h = TITLE_W * SCALE_X, TITLE_H * SCALE_Y
    box = (TITLE_LEFT * SCALE_X, TITLE_TOP * SCALE_Y, TITLE_RIGHT * SCALE_X, TITLE_BOTTOM * SCALE_Y)
    mask = text_mask(BRUSH, TITLE_TEXT, box, 'middle', (w, h))
    out = lettering(mask, (TITLE_WHITE, TITLE_PALE, TITLE_EDGE, TITLE_SHADOW),
                    TITLE_TOP * SCALE_Y, (TITLE_BOTTOM + 1) * SCALE_Y)
    small = Image.new('L', (w, h), 0)
    for letter, middle in SMALL_LETTERS:
        box = ((middle - 6) * SCALE_X, SMALL_TOP * SCALE_Y, (middle + 7) * SCALE_X, (SMALL_BOTTOM + 1) * SCALE_Y)
        small = ImageChops.lighter(small, text_mask(ROMAN, letter, box, 'middle', (w, h)))
    small = grow(small, 2)
    out.paste(Image.new('RGBA', (w, h), SMALL_EDGE + (255,)), (0, 0), grow(small, EDGE_WIDTH))
    out.paste(Image.new('RGBA', (w, h), SMALL_INK + (255,)), (0, 0), small)
    return out


def png_bytes(image):
    """A PNG whose bytes depend on the pixels alone."""
    out = io.BytesIO()
    image.save(out, format='PNG', optimize=False, compress_level=9)
    return out.getvalue()


def pictures():
    """name: PNG bytes, for every file of the mod's textures/."""
    out = {}
    for key, text in ROWS:
        out[key] = png_bytes(row_piece(text, 'right'))
    out[SHARED[0]] = png_bytes(row_piece(SHARED[1], 'left'))
    title = title_picture()
    x = 0
    for key, width in TITLE:
        out[key] = png_bytes(title.crop((x * SCALE_X, 0, (x + width) * SCALE_X, TITLE_H * SCALE_Y)))
        x += width
    return out


def wanted_files():
    files = {'mod.txt': MANIFEST.encode('ascii')}
    for key, data in pictures().items():
        files['textures/%s.png' % key] = data
    return files


def write(check):
    files = wanted_files()
    differ = []
    for name, data in sorted(files.items()):
        path = os.path.join(OUT, name)
        old = open(path, 'rb').read() if os.path.exists(path) else None
        if old == data:
            continue
        differ.append(name)
        if not check:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as f:
                f.write(data)
    there = os.path.join(OUT, 'textures')
    for name in sorted(os.listdir(there)) if os.path.isdir(there) else []:
        if 'textures/' + name not in files:
            differ.append('textures/' + name + ' (not this script\'s)')
    if check:
        for name in differ:
            print('port_english: %s is not what this script writes' % name)
        return 1 if differ else 0
    print('port_english: %d files in %s, %d written' % (len(files), os.path.relpath(OUT, ROOT), len(differ)))
    return 0


def ink(path):
    """The rectangle of a dumped texture's drawn texels."""
    image = Image.open(path).convert('RGBA')
    return image.size, image.getchannel('A').getbbox()


def measure(folder):
    """Each texture of the table in a dump, its size and where its
    lettering is: what the table's numbers came from."""
    missing = 0
    keys = [key for key, _ in ROWS] + [SHARED[0]] + [key for key, _ in TITLE]
    for key in keys:
        path = os.path.join(folder, key + '.png')
        if not os.path.exists(path):
            print('%s: not in %s' % (key, folder))
            missing += 1
            continue
        size, box = ink(path)
        print('%s: %dx%d, lettering from (%d, %d) to (%d, %d)' % ((key,) + size + box))
    return 1 if missing else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--measure', metavar='DIR')
    args = ap.parse_args()
    if args.measure:
        return measure(args.measure)
    return write(args.check)


if __name__ == '__main__':
    sys.exit(main())
