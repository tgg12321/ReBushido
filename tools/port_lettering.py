#!/usr/bin/env python3
"""The port's lettering for the tools that draw the mods it ships
(docs/PORT.md, "The port's lettering"): one typeface, Shippori Mincho, in
the three weights the program draws its own text in (src/port/window/font.c),
so a mod's picture and the settings menu are one family.

    from port_lettering import letter, font, line_mask

    letter('Online Play', face='title', size=96, color=(255, 255, 255),
           outline=(0, 8, 24), outline_px=5, shadow=(16, 41, 131), shadow_px=9)

gives an RGBA picture of the text, as tall as the face is at that size and
as wide as the text with its edge and shadow; `top` and `bottom` instead of
`color` run the letters' colour from one to the other down them, as the
game's own lettering does. `line_mask` fits a text to a rectangle the
game's lettering fills (the sharp menus, tools/port_ui.py); `font` is the
Pillow font of a face at a size.

    python tools/port_lettering.py OUT.png TEXT [--face F] [--size N]

draws one, to look at. Needs Pillow.
"""
import argparse
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS_DIR = os.path.join(ROOT, 'src', 'port', 'fonts')
# The faces, as font.c's PORT_FACE_: text (Regular) for lines of reading,
# bold for a menu's lines and names, title (ExtraBold) for titles, rows of
# the top menu and digits.
FACES = {
    'text': 'ShipporiMincho-Regular.ttf',
    'bold': 'ShipporiMincho-Bold.ttf',
    'title': 'ShipporiMincho-ExtraBold.ttf',
}
# how much wider than the face has it a text may be drawn to fill the
# rectangle the game's lettering fills before the rest is put between its
# letters (line_mask)
STRETCH_MAX = 1.1
# how much narrower: past this the letters are drawn smaller instead
SQUEEZE_MIN = 0.8
# the most put between two letters, of the size, where it need not fill
TRACK_MAX = 0.08

_fonts = {}


def font(face, size):
    """The Pillow font of a face at `size` pixels (Pillow's size: the em)."""
    key = (face, size)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(os.path.join(FONTS_DIR, FACES[face]), size)
    return _fonts[key]


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


def paint(mask, top, bottom=None, outline=None, outline_px=0, shadow=None, shadow_px=0, y0=0, y1=None):
    """A mask of letters coloured: a shadow of them (and their edge) down
    and to the right, a dark edge round both, the letters `top` running to
    `bottom` from row y0 to row y1."""
    bottom = top if bottom is None else bottom
    y1 = mask.height if y1 is None else y1
    out = Image.new('RGBA', mask.size, (0, 0, 0, 0))
    shadowed = shift(mask, shadow_px, shadow_px) if shadow is not None and shadow_px else None
    if outline is not None and outline_px:
        under = ImageChops.lighter(mask, shadowed) if shadowed is not None else mask
        out.paste(Image.new('RGBA', mask.size, tuple(outline) + (255,)), (0, 0), grow(under, outline_px))
    if shadowed is not None:
        out.paste(Image.new('RGBA', mask.size, tuple(shadow) + (255,)), (0, 0), shadowed)
    out.paste(gradient(mask.size, top, bottom, y0, y1).convert('RGBA'), (0, 0), mask)
    return out


def letter(text, face='bold', size=48, color=(255, 255, 255), top=None, bottom=None,
           outline=None, outline_px=0, shadow=None, shadow_px=0):
    """The text as an RGBA picture: `size` pixels from the top of the
    tallest letter to the bottom of the deepest (as font.c's size), its
    baseline at the face's ascent from the picture's top plus the edge."""
    f = font(face, size_for(face, size))
    ascent, descent = f.getmetrics()
    pad = outline_px
    w = int(round(f.getlength(text))) + 2 * pad + (shadow_px if shadow is not None else 0) + 2
    h = ascent + descent + 2 * pad + (shadow_px if shadow is not None else 0)
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).text((pad + 1, pad), text, font=f, fill=255)
    return paint(mask, top or color, bottom or top or color, outline, outline_px, shadow, shadow_px,
                 pad, pad + ascent + descent)


def size_for(face, pixels):
    """Pillow's size (the em) for a face `pixels` from its highest point to
    its lowest, as stb_truetype's stbtt_ScaleForPixelHeight takes it."""
    f = font(face, 1000)
    ascent, descent = f.getmetrics()
    return max(1, int(round(pixels * 1000.0 / (ascent + descent))))


def fit_height(face, words, box_h):
    """The face at the size whose drawing of `words`, from its highest
    point to its lowest, is `box_h` pixels high."""
    size = box_h * 3
    while size > 4:
        f = font(face, size)
        _, top, _, bottom = f.getbbox(words)
        if bottom - top <= box_h:
            return f
        size -= 1
    return font(face, 4)


def reference(words):
    """What a line's height is fitted to: its words with a capital, and a
    descender where it has small letters, so that the lines of a menu --
    "Sub" and "Return to Top Menu" -- come out one size on one baseline,
    and a name in capitals fills its rectangle with them."""
    return words + 'H' + ('g' if any(c.islower() for c in words) else '')


def fitted_size(words, face, box_w, box_h, aspect=1.0):
    """The size line_mask() draws the words at, alone."""
    f = fit_height(face, reference(words), box_h)
    left, _, right, _ = f.getbbox(words)
    while (right - left) * aspect * SQUEEZE_MIN > box_w and f.size > 4:
        f = font(face, f.size - 1)
        left, _, right, _ = f.getbbox(words)
    return f.size


def common_size(sizes, long_part=0.75):
    """The one size of texts of a kind, each fitted alone to `sizes`: the
    smallest of them, leaving out a long one that fits only at less than
    `long_part` of the largest, which keeps its own."""
    return min(size for size in sizes if size >= long_part * max(sizes))


def line_mask(words, face, box_w, box_h, report=None, aspect=1.0, size_max=None, fill=True):
    """The words as a mask `box_w` by `box_h`, filling it: as tall as the
    box (reference()), and as wide -- narrowed where the face is wider, but
    only to SQUEEZE_MIN, past that its letters smaller on the baseline they
    would have had; widened up to STRETCH_MAX where it is narrower, and past
    that the rest put between the letters, so a serif keeps its shape where
    the game's lettering is wide. `aspect` is how many of the mask's pixels
    across are one down on the screen: 2 for a texture of the console's 640
    by 240 mode drawn as many pixels a texel each way, whose texel is twice
    as tall as it is wide there. `size_max` caps the face's size (Pillow's,
    fitted_size()), so that texts of one kind are drawn one size. Unless
    `fill`, the space put between letters is at most TRACK_MAX of the size
    and the text set in the middle of what it leaves: `fill` is for a text
    the game cuts into pieces at fixed places, which must reach its ends.
    `report(stretch)` is told how much it was narrowed."""
    ref = reference(words)
    f = fit_height(face, ref, box_h)
    baseline = f.getmetrics()[0] - f.getbbox(ref)[1]
    if size_max is not None and f.size > size_max:
        f = font(face, size_max)
    left, _, right, _ = f.getbbox(words)
    natural = (right - left) * aspect
    margin = 4 + box_h // 2
    if natural >= box_w / STRETCH_MAX or len(words) < 2:
        while natural * SQUEEZE_MIN > box_w and f.size > 4:
            f = font(face, f.size - 1)
            left, _, right, _ = f.getbbox(words)
            natural = (right - left) * aspect
        y = baseline - f.getmetrics()[0]
        stretch = box_w / float(natural)
        wide = Image.new('L', (right - left + 2 * margin, box_h + 2 * margin), 0)
        ImageDraw.Draw(wide).text((margin - left, margin + y), words, font=f, fill=255)
        if report is not None and stretch < 1.0:
            report(stretch)
        across = stretch * aspect
        wide = wide.resize((max(1, int(round(wide.width * across))), wide.height), Image.LANCZOS)
        mask = Image.new('L', (box_w, box_h), 0)
        mask.paste(wide, (int(round(-margin * across)), -margin))
        return mask
    # letters drawn one by one, each widened STRETCH_MAX, the rest of the
    # width between them
    y = baseline - f.getmetrics()[0]
    across = STRETCH_MAX * aspect
    gap = (box_w - natural * STRETCH_MAX) / float(len(words) - 1)
    # spaced only so far, the rest either side, unless it must fill
    start = 0.0
    if not fill and gap > TRACK_MAX * f.size * aspect:
        gap = TRACK_MAX * f.size * aspect
        start = (box_w - natural * STRETCH_MAX - gap * (len(words) - 1)) / 2.0
    mask = Image.new('L', (box_w, box_h), 0)
    pen = 0.0
    for i, char in enumerate(words):
        advance = f.getlength(char)
        if char != ' ':
            cl, _, cr, _ = f.getbbox(char)
            piece = Image.new('L', (max(1, cr - cl) + 2 * margin, box_h + 2 * margin), 0)
            ImageDraw.Draw(piece).text((margin - cl, margin + y), char, font=f, fill=255)
            piece = piece.resize((max(1, int(round(piece.width * across))), piece.height), Image.LANCZOS)
            x = start + (pen + (cl - left)) * across + gap * i
            layer = Image.new('L', (box_w, box_h), 0)
            layer.paste(piece, (int(round(x - margin * across)), -margin))
            mask = ImageChops.lighter(mask, layer)
        pen += advance
    return mask


def main():
    ap = argparse.ArgumentParser(description='draws a text in the port\'s typeface')
    ap.add_argument('out')
    ap.add_argument('text')
    ap.add_argument('--face', default='bold', choices=sorted(FACES))
    ap.add_argument('--size', type=int, default=96)
    args = ap.parse_args()
    letter(args.text, args.face, args.size, outline=(0, 8, 24), outline_px=max(1, args.size // 20),
           shadow=(16, 41, 131), shadow_px=max(1, args.size // 12)).save(args.out)


if __name__ == '__main__':
    main()
