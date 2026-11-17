#!/usr/bin/env python3
"""The built-in "ui" mod of the native port (docs/MODS.md, "The sharp
menus"): the lettering of the game's menus drawn again in a font, sharp at
any size -- the pause menu, the menu after a fight, the top menu's captions,
the select screens' titles, the group battle's page.

    python tools/port_ui.py                  write src/port/mods/ui/
    python tools/port_ui.py --check          exit 1 if what is there is not
                                             what this would write
    python tools/port_ui.py --measure DIR... hold the table below to the
                                             game's own textures, dumped
                                             into DIR (--dump-textures)

The game draws a line of its menus as sprites up to 64 texels wide cut from
one line of lettering in a texture page, and a word of one line is sometimes
another line's (the pause menu's "Return to Sub Menu" is the "Return to " of
"Return to Top Menu", then "Sub", then "Menu", each from its own place). So a
line here is the pieces of one line of the page, each at its place along it, and a piece of
another line that holds the same letters at the same place is cut from it as
well:
the line's text is drawn once, in Arimo (src/port/fonts/, SIL Open Font
Licence), over the rectangle the game's own lettering fills, and each piece
is cut from it. Where the pieces' places along their lines came from: the
log of a run with --dump-textures, which says where each texture was
(docs/MODS.md, "Making one").

The pictures are this project's own: text in a free font, coloured with the
colours of the game's own lettering. Not a pixel of the game's is in them or
is read to make them, so they are kept in the repository, and this script
makes the same bytes again. What was taken from the game is in the table:
each piece's key, where it sits along its line, the rectangle the lettering
fills, and its two colours, measured with --measure from dumps of the menus.

Needs Pillow.
"""
import argparse
import io
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'port', 'mods', 'ui')
FONT = os.path.join(ROOT, 'src', 'port', 'fonts', 'Arimo-Variable.ttf')
# How many of a picture's pixels a texel of the game's is, each way. The
# letters are fitted to the rectangle the game's own fill, so whatever the
# screen does to a texel's shape it does to these as well.
SCALE = 8
# the font's weight (400 to 700)
WEIGHT = 600
# the shadow's step down and to the right, in texels, and how far the
# letters are drawn narrower before they are drawn smaller
SHADOW_STEP = 1
# the edge round the select screens' letters, in pixels
EDGE_PIXELS = 5
SQUEEZE_MIN = 0.7

MANIFEST = '''# ReBushido's sharp menus: built in, made by tools/port_ui.py
# (docs/MODS.md, "The sharp menus"). `ui = off` in rebushido.cfg, or the
# settings menu, leaves it out.
name = Sharp menus
version = 1.0
order = -100      # over the art mod (-200), under every mod of yours
author = ReBushido
'''

# The looks of the game's lettering: the letters' colour at their top and
# at their bottom, and the dark colour round them -- a shadow down and to
# the right (the pause menu's), an edge all round (the select screens'), or
# none (the top menu's captions, whose letters only darken downward).
PAUSE = dict(look='shadow', top=(255, 255, 255), bottom=(214, 214, 214), dark=(24, 24, 24))
PAUSE_TITLE = dict(look='shadow', top=(255, 255, 0), bottom=(214, 214, 0), dark=(32, 32, 0))
CAPTION = dict(look='plain', top=(255, 255, 255), bottom=(197, 197, 197), dark=(0, 0, 0))
TITLE = dict(look='edge', top=(255, 255, 255), bottom=(165, 165, 173), dark=(8, 8, 8))
GROUP = dict(look='edge', top=(255, 255, 0), bottom=(197, 197, 8), dark=(0, 0, 0))
GROUP_TITLE = dict(look='edge', top=(255, 255, 255), bottom=(197, 197, 197), dark=(16, 16, 16))


def line(text, size, ink, look, pieces):
    return dict(text=text, size=size, ink=ink, pieces=pieces, **look)


# The lines: the text; the line's size in texels; the rectangle the game's
# lettering fills (left, top, right, bottom: texels, the right and bottom
# past it); its look; the pieces, each its key and where it begins along
# the line.
LINES = [
    # the pause menu (fight_RunPauseMenu) and the menu after a fight
    line('Pause Menu', (148, 12), (2, 0, 147, 11), PAUSE_TITLE,
         [('64x12-4-8426f78b501ddbc2', 0), ('64x12-4-a8eb34d598d914bb', 64), ('20x12-4-33ae64c78e915e15', 128)]),
    line('Continue', (109, 12), (1, 0, 109, 12), PAUSE,
         [('64x12-4-666060fc090097b3', 0), ('45x12-4-75a740f50adefc01', 64)]),
    line('Return to Top Menu', (234, 12), (1, 0, 234, 12), PAUSE,
         [('64x12-4-e139f08f2ee12590', 0), ('64x12-4-a5de196600f9673b', 64), ('64x12-4-809705bbcb9bed3a', 128),
          ('42x12-4-f45e201cdf58730d', 192),
          # the "n to " of "Return to Sub Menu", cut from the same place
          ('53x12-4-34c8134d9decb37b', 64)]),
    line('Sub', (57, 12), (1, 0, 48, 12), PAUSE, [('57x12-4-89dd5e99d1a8aaac', 0)]),
    line('Menu', (65, 12), (1, 0, 65, 12), PAUSE, [('65x12-4-6e75f76360d4014c', 0)]),
    line('Rematch', (107, 12), (1, 0, 107, 12), PAUSE,
         [('64x12-4-92eba2127b8d29c4', 0), ('43x12-4-e38f2ca779f7cbf7', 64)]),
    # the top menu's captions (MOD.BIN)
    line('Story Mode', (132, 14), (0, 1, 131, 14), CAPTION,
         [('64x14-4-3b6bac3ac7326f24', 0), ('64x14-4-737a86493fc88d77', 64), ('4x14-4-53bf650f89476644', 128)]),
    line('Vs. CPU Mode', (171, 14), (0, 1, 171, 11), CAPTION,
         [('64x14-4-610b7910c1196050', 0), ('64x14-4-33f12b67738701c2', 64), ('43x14-4-59090a5aaa62b2d8', 128)]),
    line('Training Mode', (165, 14), (0, 1, 165, 14), CAPTION,
         [('64x14-4-b16f56e399cadeda', 0), ('64x14-4-b90ba50c347c9b3f', 64), ('37x14-4-ed4554225edeb20a', 128)]),
    line('Vs. Mode', (110, 14), (0, 1, 110, 11), CAPTION,
         [('64x14-4-b0e31edbb0912b0b', 0), ('46x14-4-4ccbc0e84bcac438', 64)]),
    line('Group Battle Mode', (217, 14), (0, 1, 217, 14), CAPTION,
         [('64x14-4-6250a05693143ef7', 0), ('64x14-4-b569ca87833851bc', 64), ('64x14-4-4719ec1ce942313e', 128),
          ('25x14-4-21d16e484037b4b7', 192)]),
    line('Link Mode', (118, 14), (0, 1, 118, 11), CAPTION,
         [('64x14-4-12a322c3cae1b3ea', 0), ('54x14-4-b4f46370655c6de3', 64)]),
    line('Option Menu', (147, 14), (0, 1, 147, 14), CAPTION,
         [('64x14-4-e98a7279e31fa5d0', 0), ('64x14-4-239ba02c4dc709fd', 64), ('19x14-4-6627bf02eef48edd', 128)]),
    # the select screens' titles: "Select", then "Character" or "Weapon"
    line('Select', (91, 16), (0, 1, 91, 14), TITLE,
         [('64x16-4-5fb9a70b547a8650', 0), ('27x16-4-5fc5181ae677b109', 64)]),
    line('Character', (152, 16), (6, 1, 152, 14), TITLE,
         [('64x16-4-8e7b22c1b4b49d3a', 0), ('64x16-4-d031c43e8b6d182e', 64), ('24x16-4-9eeba8ac2432d453', 128)]),
    line('Weapon', (140, 16), (6, 0, 140, 16), TITLE,
         [('64x16-4-3c0184e19dac46ad', 0), ('64x16-4-3ceafe251e7f1584', 64), ('12x16-4-2ff66a2727f219ab', 128)]),
    # the group battle's page
    line('Number of Entries', (210, 16), (0, 1, 210, 12), GROUP,
         [('64x16-4-00364172ef334029', 0), ('64x16-4-0dcafdd80f2fe31a', 64), ('64x16-4-fa28c04911b1d6df', 128),
          ('18x16-4-516bc7f30862eb14', 192)]),
    line('Match', (69, 16), (0, 1, 69, 12), GROUP,
         [('64x16-4-d778bdae0266e28f', 0), ('5x16-4-3875fdf3cc17abaa', 64)]),
    line('Time Limit', (121, 16), (0, 1, 121, 12), GROUP,
         [('64x16-4-628dc7576ef2647d', 0), ('57x16-4-35bcfd1f74a74980', 64)]),
    line('Start', (56, 16), (0, 1, 56, 12), GROUP, [('56x16-4-ce54f80e106c1846', 0)]),
    line('Return to Top Menu', (227, 16), (0, 1, 227, 15), GROUP,
         [('64x16-4-3d5ebfe7201ca66b', 0), ('64x16-4-53d4ee8e1a5749de', 64), ('64x16-4-6df663410298211f', 128),
          ('35x16-4-ac43e14405e81bd7', 192)]),
    line('Group Battle Mode', (234, 16), (0, 0, 234, 16), GROUP_TITLE,
         [('64x16-4-5d757edaf105a901', 0), ('64x16-4-3de1f4e048d62ce8', 64), ('64x16-4-b1100fe8f980744e', 128),
          ('42x16-4-18004d59faf27df7', 192)]),
]


def font_fitting(text, box_w, box_h):
    """The font at the size whose letters, from the capitals' top to the
    tails' bottom, fill `box_h` pixels, and how much narrower or wider the
    text is drawn to fill `box_w`."""
    size = box_h * 2
    while size > 4:
        font = ImageFont.truetype(FONT, size)
        font.set_variation_by_axes([WEIGHT])
        # from the capitals' top to the bottom of a p's tail, whether the
        # text has one or not: so every line of one height has letters of
        # one size, as the game's have
        top = font.getbbox('H')[1]
        bottom = font.getbbox('Hp')[3]
        if bottom - top <= box_h:
            break
        size -= 1
    left, _, right, _ = font.getbbox(text)
    return font, box_w / float(right - left)


def line_picture(line):
    """The whole line, letters and shadow, SCALE pixels a texel."""
    w, h = line['size']
    x0, y0, x1, y1 = line['ink']
    W, H = w * SCALE, h * SCALE
    step = SHADOW_STEP * SCALE
    # the letters' capitals fill the rectangle less the dark round them
    room = step if line['look'] == 'shadow' else EDGE_PIXELS if line['look'] == 'edge' else 0
    box_w, box_h = (x1 - x0) * SCALE - room, (y1 - y0) * SCALE - room
    x0 += room / 2.0 / SCALE if line['look'] == 'edge' else 0
    font, stretch = font_fitting(line['text'], box_w, box_h)
    left, _, right, bottom = font.getbbox(line['text'])
    cap_top = font.getbbox('H')[1]
    margin = 4 * SCALE
    wide = Image.new('L', (right - left + 2 * margin, bottom + margin), 0)
    ImageDraw.Draw(wide).text((margin - left, 0), line['text'], font=font, fill=255)
    if stretch < SQUEEZE_MIN:
        print('port_ui: "%s" is drawn %.2f as wide as the font has it' % (line['text'], stretch))
    wide = wide.resize((max(1, int(round(wide.width * stretch))), wide.height), Image.LANCZOS)
    mask = Image.new('L', (W, H), 0)
    mask.paste(wide, (int(round(x0 * SCALE - margin * stretch)), int(y0 * SCALE + (room if line['look'] == 'edge' else 0) / 2 - cap_top)))
    dark = Image.new('L', (W, H), 0)
    if line['look'] == 'shadow':
        dark.paste(mask, (step, step))
    elif line['look'] == 'edge':
        dark = mask.filter(ImageFilter.MaxFilter(EDGE_PIXELS * 2 + 1))
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    out.paste(Image.new('RGBA', (W, H), tuple(line['dark']) + (255,)), (0, 0), dark)
    letters = gradient((W, H), line['top'], line['bottom'], y0 * SCALE, y1 * SCALE)
    out.paste(letters.convert('RGBA'), (0, 0), mask)
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


def png_bytes(image):
    """A PNG whose bytes depend on the pixels alone."""
    out = io.BytesIO()
    image.save(out, format='PNG', optimize=False, compress_level=9)
    return out.getvalue()


def pictures():
    """name: PNG bytes, for every file of the mod's textures/."""
    out = {}
    for line in LINES:
        whole = line_picture(line)
        h = line['size'][1]
        for key, at in line['pieces']:
            w = int(key.split('x')[0])
            out[key] = png_bytes(whole.crop((at * SCALE, 0, (at + w) * SCALE, h * SCALE)))
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
            print('port_ui: %s is not what this script writes' % name)
        return 1 if differ else 0
    print('port_ui: %d files in %s, %d written' % (len(files), os.path.relpath(OUT, ROOT), len(differ)))
    return 0


def measure(folders):
    """Each line assembled from a dump: where its lettering is, and its
    two most common colours, the brighter the letters' and the darker the
    shadow's -- what the table's numbers came from."""
    missing = 0
    for line in LINES:
        w, h = line['size']
        whole = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        found = 0
        for key, at in line['pieces']:
            for folder in folders:
                path = os.path.join(folder, key + '.png')
                if os.path.exists(path):
                    whole.alpha_composite(Image.open(path).convert('RGBA'), (at, 0))
                    found += 1
                    break
        if found < len(line['pieces']):
            print('%s: %d of its %d pieces not in the dumps' % (line['text'], len(line['pieces']) - found,
                                                                len(line['pieces'])))
            missing += 1
            if not found:
                continue
        box = whole.getchannel('A').getbbox()
        colors = sorted(((c, rgb[:3]) for c, rgb in whole.getcolors(w * h) if rgb[3]), reverse=True)[:2]
        colors = sorted((rgb for _, rgb in colors), key=sum, reverse=True)
        print('%-20s ink=%s fill=%s shadow=%s' % (line['text'], box, colors[0], colors[-1]))
    return 1 if missing else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--measure', metavar='DIR', nargs='+')
    args = ap.parse_args()
    if args.measure:
        return measure(args.measure)
    return write(args.check)


if __name__ == '__main__':
    sys.exit(main())
