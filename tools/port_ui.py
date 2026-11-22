#!/usr/bin/env python3
"""The built-in "ui" mod of the native port (docs/MODS.md, "The sharp
menus"): the lettering of the game's 2D screens drawn again in a font,
sharp at any size -- the pause menu, the menu after a fight, the top menu's
captions, the select screens, the group battle's page, the training mode's
page, the memory card's messages, the story's narration, the stage cards,
the results, and the built-in font of ReBushido's options page.

    python tools/port_ui.py                  write src/port/mods/ui/
    python tools/port_ui.py --check          exit 1 if what is there is not
                                             what this would write
    python tools/port_ui.py --measure DIR... hold the pause menu's and the
                                             captions' table to the game's
                                             own textures, dumped into DIR
                                             (--dump-textures)

The game draws its lettering as sprites up to 64 texels wide cut from
lettering laid out in a texture page: a line, several lines one under
another, or a long text wrapped from one row of the page to the next, now
and then in the middle of a letter. A word of one line is sometimes another
line's (the pause menu's "Return to Sub Menu" is the "Return to " of
"Return to Top Menu", then "Sub", then "Menu", each from its own place). So
the table here is of blocks: a block is a rectangle of a page as the game
laid its lettering out, the texts in it, each with the rectangle or
rectangles its letters fill and its look, and the pieces the game cuts from
it, each its key and where it lies in the block. Each text is drawn once,
in the port's typeface, Shippori Mincho (src/port/fonts/, the SIL Open Font
Licence; tools/port_lettering.py), fitted to its rectangle at eight pixels
a texel -- as tall as the game's lettering, and as wide: a little wider than
the face has it and then spaced, where the game's is wider, so the serifs
keep their shape -- a text wrapped over several rectangles drawn as one
line across them laid end to end and cut at their widths -- and each piece
is cut from the block. Where the pieces' places came from:
the log of a run with --dump-textures, which says where each texture was
(docs/MODS.md, "Making one").

The built-in font's characters (font.c), which ReBushido's options page
draws one sprite a character, are each a texture of their own: GLYPHS has
the key of each, as eight by eight and as the seven by seven that the
page's larger text reads.

The pictures are this project's own: text in a free font, coloured with the
colours of the game's own lettering. Not a pixel of the game's is in them or
is read to make them, so they are kept in the repository, and this script
makes the same bytes again. What was taken from the game is in the tables:
each piece's key, where it lies, the rectangles the lettering fills and its
colours, measured from dumps of the screens.

Needs Pillow.
"""
import argparse
import io
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from port_lettering import common_size, fitted_size, font, gradient, line_mask  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'port', 'mods', 'ui')
# The kinds of the game's lettering and the face of the port's typeface
# each is drawn in (port_lettering.FACES): its menus' lines, the thinner
# lettering of its names and captions, its heavy digits, the built-in
# font's characters, and its titles and names of places.
FONTS = {
    'sans': 'bold',
    'thin': 'bold',
    'heavy': 'title',
    'glyph': 'bold',
    'serif': 'text',
}
# How many of a picture's pixels a texel of the game's is, each way. The
# letters are fitted to the rectangle the game's own fill, so whatever the
# screen does to a texel's shape it does to these as well.
SCALE = 8
# the shadow's step down and to the right, in texels
SHADOW_STEP = 1
# the edge round the select screens' letters, in pixels
EDGE_PIXELS = 5
SQUEEZE_MIN = 0.7
# The screens these are of are in the console's mode of 640 by 240, whose
# texel is twice as tall as it is wide: the letters are drawn twice as wide
# in a texel as the face has them, to be seen as it has them
ASPECT = 2.0
# The kinds whose strokes are made this many pixels thicker at each side:
# the names, small on the screen, whose mincho hairlines (the bar of an H)
# a picture of eight pixels a texel shown at four or five loses to the
# half-covered texel the console does not draw
THICKENED = ('thin',)
THICKEN_PIXELS = 2
# the built-in font's characters: seven texels high but for the tails of
# g j p q y, which are in the eighth row
GLYPH_HEIGHT = 7

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
# the right (the pause menu's), an edge all round (the select screens'), a
# panel under them (the top menu's "Side View" and "POV"), or none.
PAUSE = dict(look='shadow', top=(255, 255, 255), bottom=(214, 214, 214), dark=(24, 24, 24))
PAUSE_TITLE = dict(look='shadow', top=(255, 255, 0), bottom=(214, 214, 0), dark=(32, 32, 0))
CAPTION = dict(look='plain', top=(255, 255, 255), bottom=(197, 197, 197), dark=(0, 0, 0))
TITLE = dict(look='edge', top=(255, 255, 255), bottom=(165, 165, 173), dark=(8, 8, 8))
GROUP = dict(look='edge', top=(255, 255, 0), bottom=(197, 197, 8), dark=(0, 0, 0))
GROUP_TITLE = dict(look='edge', top=(255, 255, 255), bottom=(197, 197, 197), dark=(16, 16, 16))
# the group battle card's names: white, a little greyer at the foot, as the
# game's own are
CARD_NAME_TOP = (255, 255, 255)
CARD_NAME_BOTTOM = (214, 214, 214)


def text(words, boxes, font, look, top, bottom, dark, panel=None, aspect=ASPECT, screen=None, slant=0.0):
    """A text of a block: its words, the rectangles its letters fill
    (left, top, right, bottom: texels, the right and bottom past it; more
    than one where it is wrapped), its font and its look; for a panel, the
    panel's rectangle; the texel's width on the screen for its height
    there, if its screen is not one of 640 by 240 (ASPECT); the screen
    it is of, where its kind (kind()) is to be that screen's alone; and,
    for the game's italic (the staff roll's), how far its letters lean:
    the face has no italic of its own, so its upright letters are leant,
    `slant` texels across for each one up."""
    return dict(words=words, boxes=boxes, font=font, look=look, top=top, bottom=bottom, dark=dark, panel=panel,
                aspect=aspect, screen=screen, slant=slant)


def rule(box, colour):
    """A rule drawn under a title: its rectangle and colour."""
    return dict(rule=box, colour=colour)


def block(size, texts, pieces):
    return dict(size=size, texts=texts, pieces=pieces)


def line(words, size, ink, look, pieces, font='sans'):
    """A block of one line: `pieces` are each a key and where it begins
    along the line."""
    return block(size, [text(words, [ink], font, **look)], [(key, at, 0) for key, at in pieces])


# The pause menu's, the captions' and the select screens' lines: the
# text; the line's size in texels; the rectangle the game's lettering
# fills (left, top, right, bottom: texels, the right and bottom past it);
# its look; the pieces, each its key and where it begins along the line.
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
         [('64x14-4-3b6bac3ac7326f24', 0), ('64x14-4-737a86493fc88d77', 64), ('4x14-4-53bf650f89476644', 128)], font='thin'),
    line('Vs. CPU Mode', (171, 14), (0, 1, 171, 11), CAPTION,
         [('64x14-4-610b7910c1196050', 0), ('64x14-4-33f12b67738701c2', 64), ('43x14-4-59090a5aaa62b2d8', 128)], font='thin'),
    line('Training Mode', (165, 14), (0, 1, 165, 14), CAPTION,
         [('64x14-4-b16f56e399cadeda', 0), ('64x14-4-b90ba50c347c9b3f', 64), ('37x14-4-ed4554225edeb20a', 128)], font='thin'),
    line('Vs. Mode', (110, 14), (0, 1, 110, 11), CAPTION,
         [('64x14-4-b0e31edbb0912b0b', 0), ('46x14-4-4ccbc0e84bcac438', 64)], font='thin'),
    line('Group Battle Mode', (217, 14), (0, 1, 217, 14), CAPTION,
         [('64x14-4-6250a05693143ef7', 0), ('64x14-4-b569ca87833851bc', 64), ('64x14-4-4719ec1ce942313e', 128),
          ('25x14-4-21d16e484037b4b7', 192)], font='thin'),
    line('Link Mode', (118, 14), (0, 1, 118, 11), CAPTION,
         [('64x14-4-12a322c3cae1b3ea', 0), ('54x14-4-b4f46370655c6de3', 64)], font='thin'),
    line('Option Menu', (147, 14), (0, 1, 147, 14), CAPTION,
         [('64x14-4-e98a7279e31fa5d0', 0), ('64x14-4-239ba02c4dc709fd', 64), ('19x14-4-6627bf02eef48edd', 128)], font='thin'),
    # the select screens' titles: "Select", then "Character" or "Weapon"
    # (Weapon's rectangle is Select's rows, so that the two are one size)
    line('Select', (91, 16), (0, 1, 91, 14), TITLE,
         [('64x16-4-5fb9a70b547a8650', 0), ('27x16-4-5fc5181ae677b109', 64)], font='serif'),
    line('Character', (152, 16), (6, 1, 152, 14), TITLE,
         [('64x16-4-8e7b22c1b4b49d3a', 0), ('64x16-4-d031c43e8b6d182e', 64), ('24x16-4-9eeba8ac2432d453', 128)], font='serif'),
    line('Weapon', (140, 16), (6, 1, 140, 14), TITLE,
         [('64x16-4-3c0184e19dac46ad', 0), ('64x16-4-3ceafe251e7f1584', 64), ('12x16-4-2ff66a2727f219ab', 128)], font='serif'),
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

# The rest of the screens' lettering, as blocks (text() and block(),
# above), measured from dumps of the screens: each text's rectangles,
# font, look and colours, and each piece's key and place.
BLOCKS = [
    # GENGORO
    block((73, 11), [
        text('GENGORO', [(0, 1, 71, 10)], 'thin', 'plain', (16, 16, 16), (16, 16, 16), (0, 0, 0)),
    ], [
        ('64x11-4-cd7c62f44c50f525', 0, 0), ('9x11-4-ea50304f8076b2d5', 64, 0),
    ]),
    # Select School / KANNUKI / KAUN / MIKADO / TATSUMI
    block((156, 45), [
        text('Select School', [(15, 0, 141, 9)], 'serif', 'edge', (180, 172, 164), (155, 150, 139), (41, 41, 41)),
        text('KANNUKI', [(1, 13, 63, 22)], 'thin', 'plain', (16, 16, 16), (16, 16, 16), (0, 0, 0)),
        text('KAUN', [(65, 13, 103, 22)], 'thin', 'plain', (16, 16, 16), (16, 16, 16), (0, 0, 0)),
        text('MIKADO', [(1, 24, 58, 33)], 'thin', 'plain', (16, 16, 16), (16, 16, 16), (0, 0, 0)),
        text('TATSUMI', [(1, 35, 61, 44)], 'thin', 'plain', (16, 16, 16), (16, 16, 16), (0, 0, 0)),
        rule((0, 11, 156, 12), (90, 90, 82)),
    ], [
        ('64x12-4-191a8d9630f6d987', 0, 0), ('64x12-4-43b84b0537bea56b', 64, 0), ('28x12-4-4b338d20587cc9b3', 128, 0),
        ('64x11-4-336bb0a3e9e91487', 0, 12), ('40x11-4-95598b545b4de448', 64, 12), ('59x11-4-f80e7abdda78e08f', 0, 23),
        ('62x11-4-b99612c8c3517e23', 0, 34),
    ]),
    # 1ST / 2ND / 3RD
    block((227, 37), [
        text('1ST', [(119, 2, 163, 10)], 'serif', 'shadow', (156, 150, 139), (175, 167, 159), (65, 65, 57)),
        text('2ND', [(116, 14, 167, 22)], 'serif', 'edge', (159, 152, 142), (163, 156, 147), (41, 41, 41)),
        text('3RD', [(116, 26, 167, 35)], 'serif', 'edge', (161, 154, 144), (167, 161, 151), (41, 41, 41)),
        rule((205, 0, 224, 1), (127, 123, 110)),
    ], [
        ('227x1-4-ff0fb24f97a4166a', 0, 0), ('58x12-4-b3fbb05a3118b85b', 112, 1), ('58x12-4-a5d3a283e900d120', 112, 13),
        ('58x12-4-4ef1318e94f6ab9e', 112, 25),
    ]),
    # JO
    block((22, 11), [
        text('JO', [(1, 1, 21, 10)], 'thin', 'plain', (16, 16, 16), (16, 16, 16), (0, 0, 0)),
    ], [
        ('22x11-4-574a672f5a20b0fb', 0, 0),
    ]),
    # Select Character
    block((156, 12), [
        text('Select Character', [(3, 0, 153, 9)], 'serif', 'edge', (180, 172, 164), (154, 148, 137), (41, 41, 41)),
        rule((0, 11, 156, 12), (90, 90, 82)),
    ], [
        ('64x12-4-e8fb431554929bf7', 0, 0), ('64x12-4-ca20d91e3d4bb7e9', 64, 0), ('28x12-4-fac21be424883ee2', 128, 0),
    ]),
    # Narukagami / Shainto
    block((102, 16), [
        text('Narukagami', [(0, 0, 102, 8)], 'serif', 'edge', (171, 163, 155), (165, 157, 148), (41, 41, 41)),
        text('Shainto', [(0, 8, 64, 14)], 'serif', 'edge', (144, 136, 127), (145, 137, 129), (41, 41, 41)),
    ], [
        ('64x8-4-af54f43f5e55fad6', 0, 0), ('38x8-4-c98ca94380bfa6dc', 64, 0), ('64x8-4-eebec35fad772af2', 0, 8),
    ]),
    # 1ST / 2ND
    block((58, 24), [
        text('1ST', [(7, 1, 51, 9)], 'serif', 'shadow', (162, 147, 180), (183, 167, 180), (82, 65, 180)),
        text('2ND', [(4, 13, 55, 21)], 'serif', 'edge', (165, 150, 180), (169, 155, 180), (57, 41, 180)),
    ], [
        ('58x12-4-7a79c7c99fbdc8b1', 0, 0), ('58x12-4-a70e97a5bf7fab66', 0, 12),
    ]),
    # KANNUKI
    block((64, 11), [
        text('KANNUKI', [(1, 1, 63, 10)], 'thin', 'edge', (148, 139, 180), (167, 158, 182), (32, 16, 180)),
    ], [
        ('64x11-4-465aa68206def51d', 0, 0),
    ]),
    # 1ST / 2ND
    block((58, 24), [
        text('1ST', [(7, 1, 51, 9)], 'serif', 'shadow', (202, 159, 148), (205, 172, 164), (176, 70, 53)),
        text('2ND', [(4, 13, 55, 21)], 'serif', 'shadow', (202, 158, 147), (203, 162, 151), (164, 41, 24)),
    ], [
        ('58x12-4-84558dbdec8c416d', 0, 0), ('58x12-4-11bbeb492752f44a', 0, 12),
    ]),
    # GENGORO
    block((73, 11), [
        text('GENGORO', [(0, 1, 71, 10)], 'thin', 'plain', (164, 16, 0), (164, 16, 0), (0, 0, 0)),
    ], [
        ('64x11-4-91adf2b4ed4a7a2f', 0, 0), ('9x11-4-696eb1907a9b107f', 64, 0),
    ]),
    # Checking Memory Card / Please wait / Do not remove Memory Card
    block((253, 42), [
        text('Checking Memory Card', [(0, 1, 205, 13)], 'sans', 'shadow', (199, 199, 199), (174, 174, 174), (16, 16, 16)),
        text('Please wait', [(0, 15, 103, 25)], 'sans', 'shadow', (211, 211, 211), (208, 208, 208), (16, 16, 16)),
        text('Do not remove Memory Card', [(0, 29, 253, 41)], 'sans', 'shadow', (180, 180, 180), (172, 172, 172), (16, 16, 16)),
    ], [
        ('64x42-4-c7146a38e679d227', 0, 0), ('64x42-4-b19bc0ac061642ed', 64, 0), ('64x42-4-701d12696cdffe24', 128, 0),
        ('61x42-4-48ac46b27a530302', 192, 0),
    ]),
    # KANNUKI / MIKADO / TATSUMI
    block((64, 33), [
        text('KANNUKI', [(1, 1, 63, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
        text('MIKADO', [(1, 12, 58, 21)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
        text('TATSUMI', [(1, 23, 61, 32)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('64x11-4-f98dbb018443823f', 0, 0), ('59x11-4-a401498c8edfd452', 0, 11), ('62x11-4-442d64c1a0094889', 0, 22),
    ]),
    # GENGORO
    block((73, 11), [
        text('GENGORO', [(1, 1, 72, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('64x11-4-f0b34e6a46fe141f', 0, 0), ('9x11-4-23ebd565fc672c11', 64, 0),
    ]),
    # KAUN
    block((40, 11), [
        text('KAUN', [(1, 1, 39, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('40x11-4-b9f17f4fd5d33673', 0, 0),
    ]),
    # JO
    block((22, 11), [
        text('JO', [(1, 1, 21, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('22x11-4-2aeb168919949332', 0, 0),
    ]),
    # Katana
    block((65, 8), [
        text('Katana', [(1, 0, 64, 7)], 'serif', 'edge', (199, 199, 199), (246, 246, 246), (90, 90, 90)),
    ], [
        ('64x8-4-ac71dc3823862eb2', 0, 0), ('1x8-4-6639f5570a0faecf', 64, 0),
    ]),
    # Off the sea of Setonaikai, lies / the ancient island of Kounoshima. / Eight hundred years 
    block((256, 180), [
        text('Off the sea of Setonaikai, lies', [(0, 1, 252, 9)], 'sans', 'edge', (237, 237, 237), (224, 224, 224), (98, 98, 98)),
        text('the ancient island of Kounoshima.', [(2, 11, 256, 19), (0, 21, 43, 29)], 'sans', 'edge', (237, 237, 237), (225, 225, 225), (98, 98, 98)),
        text('Eight hundred years ago,', [(0, 30, 218, 40)], 'sans', 'edge', (221, 221, 221), (206, 206, 206), (98, 98, 98)),
        text('the two samurai families occupying the island', [(2, 40, 256, 50), (0, 50, 144, 60)], 'sans', 'edge', (224, 224, 224), (222, 222, 222), (98, 98, 98)),
        text('engaged in a devastating war that would split the country in two.', [(0, 60, 256, 70), (0, 70, 256, 80), (0, 80, 50, 90)], 'sans', 'edge', (222, 222, 222), (213, 213, 213), (98, 98, 98)),
        text('The Kagami family survived the war by siding with the predominant', [(0, 90, 256, 100), (1, 100, 256, 110), (0, 110, 78, 120)], 'sans', 'edge', (223, 223, 223), (210, 210, 210), (98, 98, 98)),
        text('power, while the Sue family fell, as a result of this betrayal.', [(0, 120, 256, 130), (2, 130, 246, 140)], 'sans', 'edge', (225, 225, 225), (204, 204, 204), (98, 98, 98)),
        text('The bitter  feud persisted between the Kagami descendents', [(0, 140, 256, 150), (0, 150, 256, 160), (0, 160, 12, 170)], 'sans', 'edge', (224, 224, 224), (215, 215, 215), (98, 98, 98)),
        text('and the remnants of the Sue.', [(1, 170, 255, 180)], 'sans', 'edge', (225, 225, 225), (226, 226, 226), (98, 98, 98)),
    ], [
        ('64x10-4-fa92ac3c0471965a', 0, 0), ('64x10-4-1f93a8d1bb41acdf', 64, 0), ('64x10-4-805216d25504844b', 128, 0),
        ('30x10-4-b281f458d97c726d', 192, 0), ('34x10-4-74535f0b6c4df983', 222, 0), ('64x10-4-1efea0ebbfa7705d', 0, 10),
        ('64x10-4-3ba2c93e1fdaf257', 64, 10), ('64x10-4-6f39a6704b8aa6c3', 128, 10), ('64x10-4-4efb1fb0e907969e', 192, 10),
        ('43x10-4-e14836535613243b', 0, 20), ('64x10-4-c0b3e1c082d21e85', 0, 30), ('64x10-4-8eacec77075559e5', 64, 30),
        ('64x10-4-54f8d36f2bcdb408', 128, 30), ('38x10-4-18a2adda7fb5168a', 192, 30), ('64x10-4-a2ba0b60008fbc72', 0, 40),
        ('64x10-4-d52d53641f22674d', 64, 40), ('64x10-4-fc781bfa8bc987e1', 128, 40), ('64x10-4-75faad8bcfb69e3a', 192, 40),
        ('64x10-4-d56217ce204bafb9', 0, 50), ('64x10-4-1ef614943ee741b0', 64, 50), ('16x10-4-d9d1aa4ab678d13a', 128, 50),
        ('64x10-4-e2b5722642262d34', 0, 60), ('64x10-4-12889d2d3c0b7e5d', 64, 60), ('64x10-4-2f634a4416f00d64', 128, 60),
        ('64x10-4-ff8d825b71c821c6', 192, 60), ('64x10-4-0e5e8f85c3ac6a14', 0, 70), ('64x10-4-d250ffdb19daa386', 64, 70),
        ('64x10-4-98286aea88f2fbf8', 128, 70), ('64x10-4-a6136f98ec9b27d3', 192, 70), ('50x10-4-ed99d76fd3179a9b', 0, 80),
        ('64x10-4-731d907c479b0574', 0, 90), ('64x10-4-d2013a69f92362cc', 64, 90), ('64x10-4-c4e7242d3ec45d82', 128, 90),
        ('64x10-4-e052565a174a61e8', 192, 90), ('59x10-4-a87e07ddeca7e4cf', 0, 100), ('62x10-4-01f61c7e157854df', 66, 100),
        ('64x10-4-63e483ee17e9a872', 128, 100), ('64x10-4-a73fa625484eb6b1', 192, 100), ('64x10-4-bbbad41115ab25f0', 0, 110),
        ('14x10-4-9227beffd6c25e9b', 64, 110), ('56x10-4-97bb471c4e16beb3', 0, 120), ('8x10-4-5730a91ee91e5d39', 56, 120),
        ('64x10-4-6ac23a7d658a64c5', 64, 120), ('64x10-4-bf791a11cde25677', 128, 120), ('64x10-4-23127ce9955a5d93', 192, 120),
        ('14x10-4-64c78730f4ac102f', 0, 130), ('50x10-4-59b51aeb92eac814', 14, 130), ('64x10-4-97aa5cbbf5a698c2', 64, 130),
        ('64x10-4-fc5094e0072fa5fa', 128, 130), ('54x10-4-cd41bbee2f312b47', 192, 130), ('64x10-4-d70d982ebbe5daae', 0, 140),
        ('64x10-4-5a493bccac75cb58', 64, 140), ('64x10-4-70ecadad25cd005a', 128, 140), ('64x10-4-299168a9339b7945', 192, 140),
        ('64x10-4-9586f29fc9670517', 0, 150), ('64x10-4-ab2df3aad991e874', 64, 150), ('64x10-4-c71524b13895c223', 128, 150),
        ('64x10-4-ffa7d4f032caea38', 192, 150), ('12x10-4-a22b3e6366890dee', 0, 160), ('64x10-4-18f4a9c55e29659b', 0, 170),
        ('64x10-4-3c06ea0ef2a2ab30', 64, 170), ('64x10-4-c1f4eb69301933c7', 128, 170), ('64x10-4-49d058bb5d460feb', 192, 170),
    ]),
    # 1 point / 3 point / 3 / 4 / 5 / 60 / 120 / sec
    block((196, 28), [
        text('1 point', [(0, 1, 76, 14)], 'sans', 'edge', (214, 214, 214), (240, 240, 240), (90, 90, 90)),
        text('3 point', [(76, 1, 153, 14)], 'sans', 'edge', (230, 230, 230), (230, 230, 230), (90, 90, 90)),
        text('3', [(155, 1, 168, 12)], 'sans', 'shadow', (246, 246, 246), (237, 237, 237), (82, 82, 82)),
        text('4', [(168, 2, 182, 12)], 'sans', 'edge', (238, 238, 238), (238, 238, 238), (82, 82, 82)),
        text('5', [(182, 2, 195, 12)], 'sans', 'edge', (241, 241, 241), (251, 251, 251), (82, 82, 82)),
        text('60', [(28, 15, 56, 26)], 'sans', 'edge', (234, 234, 234), (242, 242, 242), (82, 82, 82)),
        text('120', [(56, 15, 96, 26)], 'sans', 'shadow', (250, 250, 250), (237, 237, 237), (65, 65, 65)),
        text('sec', [(100, 18, 140, 26)], 'sans', 'edge', (232, 232, 232), (236, 236, 236), (82, 82, 82)),
    ], [
        ('64x14-4-1698a2679cbf2c2f', 0, 0), ('12x14-4-c7355b366723c091', 64, 0), ('64x14-4-2823ab254988e8e3', 76, 0),
        ('14x14-4-b077d35dbb1ec62a', 140, 0), ('14x14-4-51d0056330cdf0f5', 154, 0), ('14x14-4-7912eb0eee1efca3', 168, 0),
        ('14x14-4-452b30112b0b30cb', 182, 0), ('28x14-4-2408dc0c1ebea8b6', 28, 14), ('40x12-4-79003fb62e6206dd', 56, 14),
        ('44x12-4-98ac06832d4a34b8', 96, 14), ('44x14-4-745c7e1961ec7a2a', 96, 14),
    ]),
    # Building Rooftop / Building Parking Lot / Future Dump Site / Purple Cavern / Abandoned Sta
    block((177, 72), [
        text('Building Rooftop', [(0, 1, 148, 12)], 'serif', 'edge', (165, 161, 146), (172, 166, 152), (32, 32, 24)),
        text('Building Parking Lot', [(0, 13, 176, 24)], 'serif', 'edge', (163, 159, 144), (171, 164, 151), (32, 32, 24)),
        text('Future Dump Site', [(0, 25, 152, 36)], 'serif', 'edge', (172, 165, 152), (127, 120, 110), (49, 49, 41)),
        text('Purple Cavern', [(0, 37, 123, 47)], 'serif', 'edge', (133, 127, 116), (164, 160, 145), (49, 49, 41)),
        text('Abandoned Station', [(1, 49, 169, 57)], 'serif', 'edge', (160, 157, 142), (134, 127, 116), (49, 49, 41)),
        text('Bamboo Thicket', [(1, 61, 143, 69)], 'serif', 'edge', (166, 161, 146), (164, 160, 145), (49, 49, 41)),
    ], [
        ('64x12-4-9fea38abc548404c', 0, 0), ('64x12-4-ea1459b8e5c78f87', 64, 0), ('20x12-4-e2850bb8c2cc6575', 128, 0),
        ('64x12-4-a16e4f45d9d5b08a', 0, 12), ('64x12-4-127406f038101c40', 64, 12), ('49x12-4-0c9c2c9ed4e840d1', 128, 12),
        ('64x12-4-5e933de0a393a0f0', 0, 24), ('64x12-4-56a2b9b07c971c0d', 64, 24), ('25x12-4-7160829b92b2db12', 128, 24),
        ('64x12-4-582c16a41adb11c8', 0, 36), ('59x12-4-1ff176ce26d20a99', 64, 36), ('64x12-4-889bff96a7826aa6', 0, 48),
        ('64x12-4-c9a11b597bcece83', 64, 48), ('42x12-4-d92236264443805f', 128, 48), ('64x12-4-0aa207e4c639d4ab', 0, 60),
        ('64x12-4-8fb30e3c813f8643', 64, 60), ('16x12-4-297b79f908be19ce', 128, 60),
    ]),
    # ( Map ? ), the game's title of the middle stage page, which the
    # owner (2026-11-20) has read "( Map 2 )", in the colours of the first
    # page's ( Map 1 ) (below), so the three read alike
    block((74, 12), [
        text('( Map 2 )', [(1, 0, 73, 11)], 'serif', 'edge', (143, 136, 125), (140, 134, 123), (37, 36, 32)),
    ], [
        ('64x12-4-f0074d3374741ed3', 0, 0), ('10x12-4-7a2c1d11f241da6e', 64, 0),
    ]),
    # Other Options
    block((256, 32), [
        text('Other Options', [(63, 11, 192, 21)], 'sans', 'edge', (244, 244, 244), (250, 250, 250), (82, 82, 82)),
    ], [
        ('64x32-4-1711ee732298df19', 0, 0), ('64x32-4-9e7f2f141d2b3fdd', 64, 0), ('64x32-4-135732ccffaa5465', 128, 0),
    ]),
    # Battle of the Two Schools / Trust Your Skills / Master Your Weapon / Select Battle Stage /
    block((256, 192), [
        text('Battle of the Two Schools', [(10, 12, 245, 20)], 'sans', 'edge', (244, 244, 244), (250, 250, 250), (65, 65, 65)),
        text('Trust Your Skills', [(52, 44, 203, 52)], 'sans', 'edge', (215, 215, 215), (244, 244, 244), (65, 65, 65)),
        text('Master Your Weapon', [(30, 75, 225, 85)], 'sans', 'edge', (253, 253, 253), (213, 213, 213), (65, 65, 65)),
        text('Select Battle Stage', [(40, 106, 216, 117)], 'sans', 'edge', (241, 241, 241), (210, 210, 210), (65, 65, 65)),
        text('May the Best School Win', [(13, 139, 243, 149)], 'sans', 'edge', (253, 253, 253), (239, 239, 239), (65, 65, 65)),
        text('2 Player Link Battle', [(38, 171, 217, 181)], 'sans', 'edge', (242, 242, 242), (244, 244, 244), (65, 65, 65)),
    ], [
        ('64x32-4-e1f8c263eea30924', 0, 0), ('64x32-4-5021d98675f1f9dc', 64, 0), ('64x32-4-09ed5c8922826ddd', 128, 0),
        ('64x32-4-3bc2e8489e964a3d', 192, 0), ('64x32-4-a619936afca637dd', 0, 32), ('64x32-4-84cae1f6a27529ed', 64, 32),
        ('64x32-4-46d9a225ef6284bf', 128, 32), ('64x32-4-7f3554e1d0e761ad', 192, 32), ('64x32-4-e32bb78531632bd6', 0, 64),
        ('64x32-4-cc2602d3cc7bb8ac', 64, 64), ('64x32-4-4db6ae97a1b168e5', 128, 64), ('64x32-4-6b0497c225662add', 192, 64),
        ('64x32-4-40998e3709384e2a', 0, 96), ('64x32-4-e0064f0c4af72039', 64, 96), ('64x32-4-90a105d6b934fe7e', 128, 96),
        ('64x32-4-3f55ba9dc196bef8', 192, 96), ('64x32-4-81ab34a7745e7f04', 0, 128), ('64x32-4-33190490b275a08c', 64, 128),
        ('64x32-4-ef284b816a472474', 128, 128), ('64x32-4-19d4f6bd8bceeedf', 192, 128), ('64x32-4-ba521090399a33ee', 0, 160),
        ('64x32-4-c4d22321a56c177a', 64, 160), ('64x32-4-0dabd87a99bf261e', 128, 160), ('64x32-4-d9e240cbcb80041a', 192, 160),
    ]),
    # Train with the Ninja. / Adjust the Ninja's fighting pattern / by pressing the Start button
    block((256, 176), [
        text('Train with the Ninja.', [(0, 3, 227, 16)], 'sans', 'edge', (240, 240, 240), (211, 211, 211), (90, 90, 90)),
        text("Adjust the Ninja's fighting pattern", [(0, 19, 256, 32), (0, 35, 123, 48)], 'sans', 'edge', (234, 234, 234), (225, 225, 225), (74, 74, 74)),
        text('by pressing the Start button.', [(0, 51, 256, 64), (0, 67, 67, 80)], 'sans', 'edge', (227, 227, 227), (230, 230, 230), (74, 74, 74)),
        text('Practice with the actual weapon.', [(0, 83, 256, 96), (0, 99, 113, 112)], 'sans', 'edge', (230, 230, 230), (247, 247, 247), (74, 74, 74)),
        text('Select your opponent and weapon.', [(0, 115, 256, 128), (0, 131, 144, 144)], 'sans', 'edge', (219, 219, 219), (238, 238, 238), (90, 90, 90)),
        text('Overcome your weakness.', [(0, 147, 256, 160), (0, 163, 49, 176)], 'sans', 'edge', (228, 228, 228), (242, 242, 242), (90, 90, 90)),
    ], [
        ('64x16-4-0ed88932cf51331a', 0, 0), ('64x16-4-e910f8ec4beb0687', 64, 0), ('64x16-4-c6bc1d5f38866791', 128, 0),
        ('35x16-4-bf0a19da7e44594c', 192, 0), ('64x16-4-01824009568b7c5a', 0, 16), ('64x16-4-9a632f917c8d665e', 64, 16),
        ('64x16-4-7df043e50cda957c', 128, 16), ('64x16-4-9c99221bc39ef9ce', 192, 16), ('64x16-4-2e36393e3e637ca3', 0, 32),
        ('59x16-4-9860e99d58bd4162', 64, 32), ('64x16-4-9c6a694d12671d85', 0, 48), ('64x16-4-51a6ad3dbf59c4a6', 64, 48),
        ('64x16-4-4d7aaa31745f8351', 128, 48), ('64x16-4-aa6069d93434e665', 192, 48), ('64x16-4-318c90f44d8542cb', 0, 64),
        ('3x16-4-f9830dd1237a743c', 64, 64), ('64x16-4-8be9631df32b35d5', 0, 80), ('64x16-4-a0fba9773c06ec3c', 64, 80),
        ('64x16-4-59e9a19d450551b7', 128, 80), ('64x16-4-102305e1419f6161', 192, 80), ('64x16-4-1bae894c1f7ff3ee', 0, 96),
        ('49x16-4-20a60643553bc378', 64, 96), ('64x16-4-30175bce477fdb68', 0, 112), ('64x16-4-b739e8012c89a84e', 64, 112),
        ('64x16-4-54fa5a354a12b237', 128, 112), ('64x16-4-437469367d0ccffd', 192, 112), ('64x16-4-e5a881f0c0c430dc', 0, 128),
        ('64x16-4-568031659c70ea35', 64, 128), ('16x16-4-0b2b810fffb5a19f', 128, 128), ('64x16-4-f11bd6cc02121f7b', 0, 144),
        ('64x16-4-8fbcecf04f9891b6', 64, 144), ('64x16-4-0714291315033a0f', 128, 144), ('64x16-4-fcdf6ebc1ad5f761', 192, 144),
        ('49x16-4-354b805b7daada6f', 0, 160),
    ]),
    # Training Mode
    block((178, 18), [
        text('Training Mode', [(0, 1, 178, 17)], 'sans', 'shadow', (237, 237, 237), (218, 218, 218), (16, 16, 16)),
    ], [
        ('64x18-4-b7570ce0e94c12b6', 0, 0), ('64x18-4-13297f5ec4722a93', 64, 0), ('50x18-4-4de518ac8d07559d', 128, 0),
    ]),
    # Return to Top Menu / Practice Round
    block((228, 32), [
        text('Return to Top Menu', [(0, 1, 228, 15)], 'sans', 'shadow', (237, 237, 36), (216, 216, 33), (0, 0, 0)),
        text('Practice Round', [(0, 17, 177, 28)], 'sans', 'shadow', (221, 221, 33), (221, 221, 33), (0, 0, 0)),
    ], [
        ('64x16-4-75528d17be5dc67c', 0, 0), ('64x16-4-2dd71904c1c7c560', 64, 0), ('64x16-4-d1beaae96499a004', 128, 0),
        ('36x16-4-a6a3b3587b44af45', 192, 0), ('64x16-4-33c66328fab42792', 0, 16), ('29x16-4-5de599d3f113cc36', 64, 16),
        ('64x16-4-846bf5a358d3ff5b', 64, 16), ('49x16-4-2b64ac91e15ccdb4', 128, 16),
    ]),
    # Next Battle Stage
    block((76, 48), [
        text('Next Battle Stage', [(1, 2, 76, 14), (0, 18, 76, 30), (0, 34, 63, 46)], 'serif', 'edge', (224, 224, 224), (240, 240, 240), (65, 65, 65)),
    ], [
        ('64x16-4-1713dc2c43310dbd', 0, 0), ('12x16-4-e84b5ca31735692e', 64, 0), ('64x16-4-d40bd32abd298375', 0, 16),
        ('12x16-4-4a59e2d8c2c8fc15', 64, 16), ('64x16-4-22665dc84cd32f4b', 0, 32),
    ]),
    # Battle Stage: the story's card, and the stage select's title after
    # "Select" (LINES), whose look it takes so that the title is one
    block((75, 48), [
        text('Battle Stage', [(6, 1, 75, 14), (0, 17, 75, 30), (0, 33, 30, 46)], 'serif', **TITLE),
    ], [
        ('64x16-4-cd5052c53e91f3ce', 0, 0), ('11x16-4-13724f0250e6fe98', 64, 0), ('64x16-4-587b0da0ee5d3dd9', 0, 16),
        ('11x16-4-fb419fef2351d79e', 64, 16), ('30x16-4-75bbfa41b00c82f0', 0, 32),
    ]),
    # Outside the Meikyokan
    block((256, 22), [
        text('Outside the Meikyokan', [(0, 6, 256, 18)], 'serif', 'edge', (244, 244, 244), (238, 238, 238), (90, 90, 90)),
        rule((0, 21, 256, 22), (255, 0, 0)),
    ], [
        ('64x22-4-91734ea23e615773', 0, 0), ('64x22-4-58bcac6b5a2f488f', 64, 0), ('64x22-4-adadf503d6f017cf', 128, 0),
        ('64x22-4-1d09407211c3107c', 192, 0),
    ]),
    # Side View / POV
    block((200, 12), [
        text('Side View', [(7, 2, 94, 10)], 'sans', 'plain', (219, 219, 219), (190, 190, 190), (0, 0, 16), panel=(0, 0, 100, 12)),
        text('POV', [(129, 2, 170, 10)], 'sans', 'plain', (195, 195, 195), (166, 166, 166), (0, 0, 16), panel=(100, 0, 200, 12)),
    ], [
        ('64x12-4-c1d528d38350599f', 0, 0), ('36x12-4-341c7a30a7147b9e', 64, 0), ('64x12-4-5578bb692bac091a', 100, 0),
        ('36x12-4-c1c0a62ae7ce4cfd', 164, 0),
    ]),
    # 01234
    block((91, 14), [
        text('01234', [(0, 0, 91, 14)], 'heavy', 'edge', (255, 255, 255), (255, 255, 255), (0, 0, 0)),
    ], [
        ('19x14-4-6697e1b3e95a3fbb', 0, 0), ('15x14-4-7317fd7d4dc01d68', 19, 0), ('19x14-4-629f5097e5fe6b5a', 34, 0),
        ('19x14-4-e8660681ba94bb0d', 53, 0), ('19x14-4-62e07d0d46a1e431', 72, 0),
    ]),
    # on / off
    block((58, 12), [
        text('on', [(0, 3, 27, 11)], 'sans', 'shadow', (227, 227, 227), (227, 227, 227), (16, 16, 16)),
        text('off', [(28, 2, 57, 11)], 'sans', 'shadow', (211, 211, 211), (231, 231, 231), (16, 16, 16)),
    ], [
        ('28x12-4-29467cc447d29210', 0, 0), ('30x12-4-bd81c3c9814cfbc3', 28, 0),
    ]),
    # The Select button changes / Attack / Dash / the stance of the Ninja.
    block((256, 52), [
        text('The Select button changes', [(0, 0, 256, 13), (0, 13, 66, 26)], 'sans', 'shadow', (236, 236, 236), (243, 243, 243), (65, 65, 65)),
        text('Attack', [(78, 13, 154, 25)], 'sans', 'shadow', (251, 251, 251), (253, 253, 253), (65, 65, 65)),
        text('Dash', [(156, 13, 218, 25)], 'sans', 'shadow', (218, 218, 218), (255, 255, 255), (65, 65, 65)),
        text('the stance of the Ninja.', [(0, 26, 256, 39), (0, 39, 19, 52)], 'sans', 'shadow', (249, 249, 249), (240, 240, 240), (65, 65, 65)),
    ], [
        ('192x13-4-09c159f3e1eadb2f', 0, 0), ('64x13-4-4012fafabd9d6e1c', 192, 0), ('66x13-4-a04bd9c80a8ddeb0', 0, 13),
        ('50x12-4-fb5af695a05d0591', 78, 13), ('26x12-4-067b340d00f82e8a', 128, 13), ('12x13-4-e5caa3e08a014835', 156, 13),
        ('12x13-4-e0d3572a4192c2da', 168, 13), ('12x13-4-591cdcdee1d82418', 180, 13), ('29x13-4-0fafe3c7b3db805c', 192, 13),
        ('192x13-4-dfa0d3f3f2202fa3', 0, 26), ('64x13-4-4eff0169f82e6b25', 192, 26), ('19x13-4-7f0c87a6e504918c', 0, 39),
    ]),
    # Frontal
    block((88, 12), [
        text('Frontal', [(0, 0, 82, 12)], 'sans', 'shadow', (230, 230, 230), (252, 252, 252), (65, 65, 65)),
    ], [
        ('64x12-4-affd7868ae5b2c89', 0, 0), ('24x12-4-e4ebce38b8886b18', 64, 0),
    ]),
    # Reverse
    block((104, 12), [
        text('Reverse', [(0, 0, 99, 12)], 'sans', 'shadow', (238, 238, 238), (243, 243, 243), (65, 65, 65)),
    ], [
        ('64x12-4-60a25f7e8a113da6', 0, 0), ('40x12-4-239e52d959d22bc8', 64, 0),
    ]),
    # WINS / KILLS
    block((60, 28), [
        text('WINS', [(0, 1, 60, 13)], 'sans', 'edge', (255, 255, 255), (255, 255, 255), (8, 8, 8)),
        text('KILLS', [(1, 15, 60, 26)], 'sans', 'shadow', (255, 255, 255), (210, 210, 210), (8, 8, 8)),
    ], [
        ('60x14-4-095f806dd1552234', 0, 0), ('45x14-4-e81f4e0890f28474', 0, 14), ('60x14-4-057606db84d2594f', 0, 14),
    ]),
    # The select screens' lettering the first table had not: the fighters
    # every unlock brings (the six of an empty save are above), the
    # weapons but the katana, and the stage pages' first and third pages
    # (Map 1, Map 3; the middle page's is above). Measured from dumps of versus mode
    # and versus the CPU with --unlock-all, both cursors walked over the
    # grid, the weapons and the pages.
    # UTSUSEMI
    block((75, 11), [
        text('UTSUSEMI', [(1, 1, 74, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('75x11-4-7645bbdcfad25c2b', 0, 0),
    ]),
    # CHIHIRO
    block((60, 11), [
        text('CHIHIRO', [(1, 1, 59, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('60x11-4-cd9afb0ba1888c95', 0, 0),
    ]),
    # RED SHADOW
    block((90, 11), [
        text('RED SHADOW', [(1, 1, 89, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('90x11-4-a82c3d2e41b0b08f', 0, 0),
    ]),
    # SUMINAGASHI
    block((90, 11), [
        text('SUMINAGASHI', [(1, 1, 89, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('90x11-4-f7895f6f594790a8', 0, 0),
    ]),
    # MATSUMUSHI
    block((88, 11), [
        text('MATSUMUSHI', [(1, 1, 87, 11)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('88x11-4-bcbeed06cda56469', 0, 0),
    ]),
    # HONGOU
    block((63, 11), [
        text('HONGOU', [(1, 1, 62, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('63x11-4-3e9bd9b1ac99ce0b', 0, 0),
    ]),
    # NIGHTSTALKER
    block((94, 11), [
        text('NIGHTSTALKER', [(1, 1, 93, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('94x11-4-19d2cff7f050729d', 0, 0),
    ]),
    # HIGHWAYMAN
    block((93, 11), [
        text('HIGHWAYMAN', [(1, 1, 92, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('64x11-4-50f6f7face2e67a4', 0, 0), ('29x11-4-09b76b9dbabba143', 64, 0),
    ]),
    # TONY UMEDA
    block((95, 11), [
        text('TONY UMEDA', [(1, 1, 94, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('64x11-4-a23dfccaed6f5c49', 0, 0), ('31x11-4-2c7a8151bc2840a9', 64, 0),
    ]),
    # KATZE
    block((46, 11), [
        text('KATZE', [(1, 1, 45, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('46x11-4-599c8a3e07171098', 0, 0),
    ]),
    # SAZANKA
    block((71, 11), [
        text('SAZANKA', [(1, 1, 70, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('71x11-4-5f9e96432cb9abdb', 0, 0),
    ]),
    # UTAMARU
    block((71, 11), [
        text('UTAMARU', [(1, 1, 70, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('71x11-4-8cbf279ede07bc29', 0, 0),
    ]),
    # TSUBAME
    block((71, 11), [
        text('TSUBAME', [(1, 1, 70, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('71x11-4-634d829391177d29', 0, 0),
    ]),
    # ISOHACHI
    block((70, 11), [
        text('ISOHACHI', [(1, 1, 69, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('64x11-4-67c99996add1a73b', 0, 0), ('6x11-4-f23e1d2d252a5430', 64, 0),
    ]),
    # Naginata
    block((85, 8), [
        text('Naginata', [(1, 0, 84, 8)], 'serif', 'edge', (194, 194, 194), (183, 183, 183), (84, 84, 84)),
    ], [
        ('64x8-4-3b908eeaa21d44c1', 0, 0), ('21x8-4-8ebf0b3d5f3911e5', 64, 0),
    ]),
    # Broadsword
    block((117, 8), [
        text('Broadsword', [(1, 0, 116, 7)], 'serif', 'edge', (187, 187, 187), (180, 180, 180), (82, 82, 82)),
    ], [
        ('64x8-4-a8934af1ecdede21', 0, 0), ('53x8-4-cb627189f247660d', 64, 0),
    ]),
    # Nodachi
    block((80, 8), [
        text('Nodachi', [(1, 0, 79, 7)], 'serif', 'edge', (195, 195, 195), (183, 183, 183), (84, 84, 84)),
    ], [
        ('64x8-4-e273cceb2d51f13b', 0, 0), ('16x8-4-2de9528a008b21e4', 64, 0),
    ]),
    # Long Sword
    block((117, 8), [
        text('Long Sword', [(1, 0, 116, 8)], 'serif', 'edge', (181, 181, 181), (161, 161, 161), (76, 76, 76)),
    ], [
        ('64x8-4-89f524be3b546e12', 0, 0), ('53x8-4-facff04831683e6f', 64, 0),
    ]),
    # Yari
    block((38, 8), [
        text('Yari', [(0, 0, 38, 7)], 'serif', 'edge', (203, 203, 203), (210, 210, 210), (81, 81, 81)),
    ], [
        ('19x8-4-f8f2dbd709249ded', 0, 0), ('19x8-4-5a5593f83269731f', 19, 0),
    ]),
    # Shainto Gate / Shainto Cemetery / Ferry Dock / Kimigaura Coast / Snake Cliff / Lumberyard
    block((155, 72), [
        text('Shainto Gate', [(1, 1, 113, 9)], 'serif', 'edge', (147, 141, 129), (135, 130, 118), (38, 37, 33)),
        text('Shainto Cemetery', [(1, 13, 155, 24)], 'serif', 'edge', (144, 138, 125), (136, 131, 119), (41, 40, 36)),
        text('Ferry Dock', [(1, 25, 95, 36)], 'serif', 'edge', (137, 132, 120), (141, 134, 122), (41, 41, 36)),
        text('Kimigaura Coast', [(0, 37, 141, 48)], 'serif', 'edge', (150, 143, 131), (149, 144, 130), (41, 40, 35)),
        text('Snake Cliff', [(1, 49, 93, 57)], 'serif', 'edge', (144, 138, 125), (145, 139, 127), (41, 40, 36)),
        text('Lumberyard', [(1, 61, 107, 72)], 'serif', 'edge', (141, 136, 124), (132, 125, 115), (40, 39, 35)),
    ], [
        ('64x12-4-a10a8d8d92758374', 0, 0), ('50x12-4-cb387ab2236e1b44', 64, 0), ('64x12-4-8b4cfc341ff4970f', 0, 12),
        ('64x12-4-08193d466387f55c', 64, 12), ('27x12-4-38d20465e1abe62d', 128, 12),
        ('64x12-4-7dae4866bb0dec28', 0, 24), ('31x12-4-08232aa62153fe2a', 64, 24), ('64x12-4-2cdb344b65414467', 0, 36),
        ('64x12-4-5625423fd38f0ea4', 64, 36), ('14x12-4-184ca58e21384f52', 128, 36),
        ('64x12-4-ef0d2bcf1010ba75', 0, 48), ('30x12-4-260adb9ff2543f01', 64, 48), ('64x12-4-f2f14d988c330458', 0, 60),
        ('44x12-4-cfb29923d84e4e50', 64, 60),
    ]),
    # Abandoned Tunnel / Terraced Fields / Railway / Meikyokan Dojo / Castle Rooftop / Under Railway
    block((171, 72), [
        text('Abandoned Tunnel', [(0, 1, 171, 10)], 'serif', 'edge', (143, 136, 125), (141, 135, 123), (36, 35, 31)),
        text('Terraced Fields', [(0, 13, 131, 21)], 'serif', 'edge', (148, 140, 128), (143, 136, 125), (43, 43, 37)),
        text('Railway', [(0, 25, 68, 35)], 'serif', 'edge', (148, 141, 129), (128, 121, 111), (40, 39, 35)),
        text('Meikyokan Dojo', [(0, 37, 144, 47)], 'serif', 'edge', (143, 137, 125), (132, 125, 114), (37, 37, 33)),
        text('Castle Rooftop', [(0, 49, 124, 59)], 'serif', 'edge', (141, 136, 123), (128, 121, 111), (43, 42, 37)),
        text('Under Railway', [(0, 61, 128, 71)], 'serif', 'edge', (143, 138, 126), (130, 123, 113), (41, 40, 36)),
    ], [
        ('64x12-4-24eadfbdaaf5e5dc', 0, 0), ('64x12-4-e24db762fcd9e5d2', 64, 0), ('43x12-4-85650a2073c342ab', 128, 0),
        ('64x12-4-2fb448dbd28951d4', 0, 12), ('64x12-4-d27a96c17f8fbd5f', 64, 12), ('4x12-4-d4d2fda53bc11cb0', 128, 12),
        ('64x12-4-0184934a6f6693ae', 0, 24), ('6x1-4-00a37096aefc8fc5', 26, 24), ('5x12-4-d2b38d5b51f9c1f4', 64, 24),
        ('64x12-4-f35e6d3468f77c89', 0, 36), ('64x12-4-e2d15234f77ed0da', 64, 36),
        ('16x12-4-f004015a2d27dec3', 128, 36), ('64x12-4-99738657e3793415', 0, 48),
        ('61x12-4-f9b6f6194629ef75', 64, 48), ('64x12-4-3788102451f3e309', 0, 60), ('64x12-4-3c06ba10c0c2dc72', 64, 60),
    ]),
    # ( Map 1 )
    block((74, 12), [
        text('( Map 1 )', [(1, 0, 73, 11)], 'serif', 'edge', (143, 136, 125), (140, 134, 123), (37, 36, 32)),
    ], [
        ('64x12-4-e1cddb52dc2a54b1', 0, 0),
    ]),
    # ( Map 3 )
    block((74, 12), [
        text('( Map 3 )', [(1, 0, 73, 11)], 'serif', 'edge', (145, 139, 127), (139, 133, 122), (40, 39, 34)),
    ], [
        ('64x12-4-a4127d28ab9d8b2d', 0, 0),
    ]),
    # The group battle's pages: the fighters every unlock brings, and the
    # weapons, in the black of the list and the blue and red of the two
    # sides; its own small Select Weapon; and the card before the battle
    # (the sides' names, 1ST 2ND 3RD; its fighters' names are at the end of
    # this list). Measured from a dump of a group battle with --unlock-all.
    # NIGHTSTALKER
    block((94, 11), [
        text('NIGHTSTALKER', [(0, 1, 92, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('64x11-4-80d4b58c6de0530a', 0, 0), ('30x11-4-b4405cdc56bc8786', 64, 0),
    ]),
    # ISOHACHI
    block((70, 11), [
        text('ISOHACHI', [(0, 1, 68, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('64x11-4-8f6e5f339b91f4d9', 0, 0), ('6x11-4-09b11af9a123714f', 64, 0),
    ]),
    # HIGHWAYMAN
    block((93, 11), [
        text('HIGHWAYMAN', [(0, 1, 91, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('64x11-4-4f679d9a1683ded4', 0, 0), ('29x11-4-694772c54a4bcdb9', 64, 0),
    ]),
    # TONY UMEDA
    block((95, 11), [
        text('TONY UMEDA', [(0, 1, 93, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('64x11-4-05507950ab71641b', 0, 0), ('31x11-4-98aadd25491259f3', 64, 0),
    ]),
    # UTSUSEMI
    block((75, 11), [
        text('UTSUSEMI', [(1, 1, 74, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('75x11-4-29c0f3d6dd3bcfee', 0, 0),
    ]),
    # KATZE
    block((46, 11), [
        text('KATZE', [(0, 1, 44, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('46x11-4-c86f426e1c3c0d76', 0, 0),
    ]),
    # SAZANKA
    block((71, 11), [
        text('SAZANKA', [(1, 1, 70, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('71x11-4-999bddffdf322c5a', 0, 0),
    ]),
    # RED SHADOW
    block((90, 11), [
        text('RED SHADOW', [(1, 1, 89, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('90x11-4-21aabce47315865c', 0, 0),
    ]),
    # UTAMARU
    block((71, 11), [
        text('UTAMARU', [(0, 1, 69, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('71x11-4-bfea1d01c01886ba', 0, 0),
    ]),
    # SUMINAGASHI
    block((90, 11), [
        text('SUMINAGASHI', [(1, 1, 89, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('90x11-4-29d0f6842bf170d0', 0, 0),
    ]),
    # TSUBAME
    block((71, 11), [
        text('TSUBAME', [(0, 1, 69, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('71x11-4-49d780ad03c5b489', 0, 0),
    ]),
    # MATSUMUSHI
    block((88, 11), [
        text('MATSUMUSHI', [(1, 1, 87, 11)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('88x11-4-20346bfce21d5bf2', 0, 0),
    ]),
    # HONGOU
    block((63, 11), [
        text('HONGOU', [(1, 1, 62, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('63x11-4-a5e79c8e9afe327c', 0, 0),
    ]),
    # CHIHIRO
    block((60, 11), [
        text('CHIHIRO', [(0, 1, 58, 10)], 'thin', 'plain', (0, 0, 0), (0, 0, 0), (0, 0, 0)),
    ], [
        ('60x11-4-d8d1f9eba3362c06', 0, 0),
    ]),
    # KAUN
    block((40, 11), [
        text('KAUN', [(1, 1, 39, 10)], 'thin', 'plain', (69, 56, 180), (76, 62, 180), (0, 0, 0)),
    ], [
        ('40x11-4-0510e57ec839111e', 0, 0),
    ]),
    # HIGHWAYMAN
    block((93, 11), [
        text('HIGHWAYMAN', [(0, 1, 91, 10)], 'thin', 'plain', (77, 64, 180), (77, 65, 181), (0, 0, 0)),
    ], [
        ('64x11-4-b4ebfb4ed9863dea', 0, 0), ('29x11-4-0c680a7f427e7c1f', 64, 0),
    ]),
    # TONY UMEDA
    block((95, 11), [
        text('TONY UMEDA', [(0, 1, 93, 10)], 'thin', 'plain', (80, 67, 181), (79, 66, 180), (0, 0, 0)),
    ], [
        ('64x11-4-31c3ba7c79553c71', 0, 0), ('31x11-4-37301476b7ea02f9', 64, 0),
    ]),
    # KATZE
    block((46, 11), [
        text('KATZE', [(0, 1, 44, 10)], 'thin', 'plain', (87, 74, 182), (76, 63, 181), (0, 0, 0)),
    ], [
        ('46x11-4-aea9fc3bb20087f4', 0, 0),
    ]),
    # JO
    block((22, 11), [
        text('JO', [(1, 1, 21, 10)], 'thin', 'plain', (77, 64, 180), (81, 68, 180), (0, 0, 0)),
    ], [
        ('22x11-4-8097dc7110b38cd1', 0, 0),
    ]),
    # GENGORO
    block((73, 11), [
        text('GENGORO', [(0, 1, 71, 10)], 'thin', 'plain', (86, 72, 180), (84, 69, 180), (0, 0, 0)),
    ], [
        ('64x11-4-668b61baa8fb9b2b', 0, 0), ('9x11-4-1eef05f883294abb', 64, 0),
    ]),
    # KANNUKI
    block((64, 11), [
        text('KANNUKI', [(1, 1, 63, 10)], 'thin', 'plain', (173, 53, 37), (174, 59, 43), (0, 0, 0)),
    ], [
        ('64x11-4-6e35ad5299a16861', 0, 0),
    ]),
    # NIGHTSTALKER
    block((94, 11), [
        text('NIGHTSTALKER', [(0, 1, 92, 10)], 'thin', 'plain', (175, 63, 47), (173, 56, 40), (0, 0, 0)),
    ], [
        ('64x11-4-bb284171c53167dc', 0, 0), ('30x11-4-ae5a6ed6a386cf80', 64, 0),
    ]),
    # MIKADO
    block((59, 11), [
        text('MIKADO', [(1, 1, 58, 10)], 'thin', 'plain', (176, 63, 47), (175, 60, 44), (0, 0, 0)),
    ], [
        ('59x11-4-8a2867f4448b6ea9', 0, 0),
    ]),
    # TATSUMI
    block((62, 11), [
        text('TATSUMI', [(1, 1, 61, 10)], 'thin', 'plain', (175, 59, 43), (173, 54, 38), (0, 0, 0)),
    ], [
        ('62x11-4-0a3dade6f2abc515', 0, 0),
    ]),
    # SAZANKA
    block((71, 11), [
        text('SAZANKA', [(1, 1, 70, 10)], 'thin', 'plain', (177, 70, 54), (174, 62, 45), (0, 0, 0)),
    ], [
        ('71x11-4-dc97c0f29925450c', 0, 0),
    ]),
    # TSUBAME
    block((71, 11), [
        text('TSUBAME', [(0, 1, 69, 10)], 'thin', 'plain', (176, 65, 49), (174, 60, 44), (0, 0, 0)),
    ], [
        ('71x11-4-64b92f8a0609c13b', 0, 0),
    ]),
    # Katana
    block((63, 10), [
        text('Katana', [(0, 1, 63, 8)], 'serif', 'edge', (140, 133, 124), (132, 128, 117), (49, 47, 44)),
    ], [
        ('63x10-4-4f0f7dd1642d8015', 0, 0),
    ]),
    # Long Sword
    block((114, 10), [
        text('Long Sword', [(0, 1, 114, 10)], 'serif', 'edge', (146, 141, 131), (126, 123, 114), (45, 43, 40)),
    ], [
        ('64x10-4-fd77dc5439c0d6ef', 0, 0), ('50x10-4-debe5d646ec502b0', 64, 0),
    ]),
    # Katana
    block((63, 10), [
        text('Katana', [(0, 1, 63, 8)], 'serif', 'edge', (143, 133, 180), (137, 125, 180), (64, 49, 180)),
    ], [
        ('63x10-4-9c47212524813ffb', 0, 0),
    ]),
    # Nodachi
    block((79, 10), [
        text('Nodachi', [(0, 1, 79, 8)], 'serif', 'edge', (160, 146, 180), (134, 123, 180), (59, 44, 180)),
    ], [
        ('64x10-4-ea51651216149650', 0, 0), ('15x10-4-8cca9fd874e68ee0', 64, 0),
    ]),
    # Long Sword
    block((114, 10), [
        text('Long Sword', [(0, 1, 114, 10)], 'serif', 'edge', (152, 139, 180), (132, 122, 180), (59, 44, 180)),
    ], [
        ('64x10-4-1ab15dae91bf2795', 0, 0), ('50x10-4-e9000fe074e59f16', 64, 0),
    ]),
    # Katana
    block((63, 10), [
        text('Katana', [(0, 1, 63, 8)], 'serif', 'edge', (194, 132, 117), (190, 124, 108), (169, 47, 31)),
    ], [
        ('63x10-4-a07f3d22935cc0bf', 0, 0),
    ]),
    # Nodachi
    block((79, 10), [
        text('Nodachi', [(0, 1, 79, 8)], 'serif', 'edge', (197, 146, 133), (188, 120, 104), (168, 43, 27)),
    ], [
        ('64x10-4-5c3c79c6d8013e14', 0, 0), ('15x10-4-9f9fd0bcfedb17e4', 64, 0),
    ]),
    # Long Sword
    block((114, 10), [
        text('Long Sword', [(0, 1, 114, 10)], 'serif', 'edge', (195, 138, 124), (188, 118, 103), (168, 43, 26)),
    ], [
        ('64x10-4-7175322fa0ea4c09', 0, 0), ('50x10-4-c29458fd051e6e3a', 64, 0),
    ]),
    # 3RD
    block((58, 12), [
        text('3RD', [(4, 1, 55, 10)], 'serif', 'edge', (146, 133, 180), (147, 134, 180), (67, 52, 180)),
    ], [
        ('58x12-4-f636caf2835b186c', 0, 0),
    ]),
    # 3RD
    block((58, 12), [
        text('3RD', [(4, 1, 55, 10)], 'serif', 'edge', (192, 131, 117), (192, 131, 118), (170, 50, 34)),
    ], [
        ('58x12-4-7ca1063cdb4b0078', 0, 0),
    ]),
    # Select Weapon
    block((156, 12), [
        text('Select Weapon', [(8, 0, 149, 10)], 'serif', 'edge', (161, 154, 146), (138, 133, 123), (48, 46, 43)),
        rule((0, 11, 156, 12), (90, 90, 82)),
    ], [
        ('64x12-4-fb671b6ef8314c87', 0, 0), ('64x12-4-4d741c003a9fe4b8', 64, 0), ('28x12-4-48ce5cdc9609b55e', 128, 0),
    ]),
    # 1ST
    block((46, 12), [
        text('1ST', [(4, 2, 41, 10)], 'serif', 'edge', (212, 212, 212), (199, 199, 199), (70, 70, 70)),
    ], [
        ('46x12-4-4e109de55213c02a', 0, 0),
    ]),
    # 2ND
    block((46, 12), [
        text('2ND', [(0, 2, 45, 10)], 'serif', 'edge', (209, 209, 209), (204, 204, 204), (83, 83, 83)),
    ], [
        ('46x12-4-27424166deac5df9', 0, 0),
    ]),
    # 3RD
    block((46, 12), [
        text('3RD', [(1, 2, 44, 10)], 'serif', 'edge', (207, 207, 207), (215, 215, 215), (79, 79, 79)),
    ], [
        ('46x12-4-1257ccc2b24c5ee8', 0, 0),
    ]),
    # Narukagami
    block((117, 10), [
        text('Narukagami', [(0, 0, 117, 10)], 'serif', 'edge', (166, 166, 166), (162, 162, 162), (68, 68, 68)),
    ], [
        ('64x10-4-b7931d10e5507471', 0, 0), ('53x10-4-a64057e517d7648b', 64, 0),
    ]),
    # Shainto
    block((73, 10), [
        text('Shainto', [(0, 0, 73, 8)], 'serif', 'edge', (176, 176, 176), (176, 176, 176), (70, 70, 70)),
    ], [
        ('64x10-4-140e8cf74936a33d', 0, 0), ('9x10-4-ab8baf5fd6ed9add', 64, 0),
    ]),
    # The card before a group battle's fight and after each (hud.c,
    # hud_DrawTeamScores): each fighter's name from the game's sheet of the
    # twenty (g_hud_name_cells; NIGHTSTALKER's tail, g_hud_name_8_tail_cells,
    # is its last three pieces), white; the loser's is the same texture dimmed
    # by the sprite's colour. Measured from the video memory of a group
    # battle's --dump-textures (the stage's meshes/stage_32.bin).
    # KANNUKI
    block((75, 12), [
        text('KANNUKI', [(0, 0, 75, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-4c722f3b00911e3f', 0, 0), ('32x12-4-6280b85549615ce9', 32, 0),
        ('11x12-4-0be04b7a4ddb7fdf', 64, 0),
    ]),
    # TATSUMI
    block((75, 12), [
        text('TATSUMI', [(0, 0, 75, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-615e188852b435c9', 0, 0), ('32x12-4-930d3010d0cff96d', 32, 0),
        ('11x12-4-914aac30e3c5eecf', 64, 0),
    ]),
    # MIKADO
    block((66, 12), [
        text('MIKADO', [(0, 0, 66, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-6b02fe1df7faca5f', 0, 0), ('32x12-4-d38f151af99b5e12', 32, 0),
        ('2x12-4-de01bf080559e37c', 64, 0),
    ]),
    # UTSUSEMI
    block((86, 12), [
        text('UTSUSEMI', [(1, 0, 86, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-7d180ce3068b8c78', 0, 0), ('32x12-4-e3cd32d9268340bc', 32, 0),
        ('22x12-4-20cc1ef907023e2a', 64, 0),
    ]),
    # RED SHADOW
    block((117, 12), [
        text('RED SHADOW', [(1, 0, 117, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-38250a2e56d886f4', 0, 0), ('32x12-4-7e6088f4d028ffc4', 32, 0),
        ('53x12-4-c3a273d3ac065eaa', 64, 0),
    ]),
    # TSUBAME
    block((82, 12), [
        text('TSUBAME', [(0, 0, 82, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-fafe512f39e6d1cf', 0, 0), ('32x12-4-903c925f3804c9ad', 32, 0),
        ('18x12-4-061f77964a932169', 64, 0),
    ]),
    # SAZANKA
    block((80, 12), [
        text('SAZANKA', [(0, 0, 80, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-11fdc349b8de0f08', 0, 0), ('32x12-4-d9e931967c9d2c30', 32, 0),
        ('16x12-4-cf77a07e1ac3cdc5', 64, 0),
    ]),
    # SUMINAGASHI
    block((119, 12), [
        text('SUMINAGASHI', [(0, 0, 119, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-c503b2e45aa0cbf0', 0, 0), ('32x12-4-b84968e4bae5fa81', 32, 0),
        ('55x12-4-480de9eb96df47aa', 64, 0),
    ]),
    # NIGHTSTALKER
    block((128, 12), [
        text('NIGHTSTALKER', [(0, 0, 128, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('16x12-4-a8bd3871e1424272', 0, 0), ('16x12-4-30a483e064d8f655', 16, 0),
        ('16x12-4-29c004cc871dc3ec', 32, 0), ('16x12-4-3b83613d7c431d08', 48, 0),
        ('16x12-4-7bbf89a2f6ed2f23', 64, 0), ('48x12-4-f004a1d94cbd72ed', 80, 0),
    ]),
    # MATSUMUSHI
    block((115, 12), [
        text('MATSUMUSHI', [(1, 0, 115, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-72cecb6e3069f392', 0, 0), ('32x12-4-ce3ceae67cdc8993', 32, 0),
        ('51x12-4-7f160821b690e224', 64, 0),
    ]),
    # GENGORO
    block((88, 12), [
        text('GENGORO', [(1, 0, 88, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-c897463cbeccb949', 0, 0), ('32x12-4-b7097f9ec3410c3d', 32, 0),
        ('24x12-4-cf8fc891dc359ca9', 64, 0),
    ]),
    # KAUN
    block((48, 12), [
        text('KAUN', [(1, 0, 48, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('16x12-4-848ce7f03fbbb0a1', 0, 0), ('16x12-4-769e375a19162fc4', 16, 0),
        ('16x12-4-9d30cc4e32650020', 32, 0),
    ]),
    # JO
    block((23, 12), [
        text('JO', [(0, 0, 23, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('7x12-4-9a7b126cb571fb10', 0, 0), ('7x12-4-ee006a9676771ff4', 7, 0), ('9x12-4-e25863b5a98f547e', 14, 0),
    ]),
    # HONGOU
    block((78, 12), [
        text('HONGOU', [(1, 0, 78, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-8203a59a5dc01508', 0, 0), ('32x12-4-4ff4e5bf6ecf5054', 32, 0),
        ('14x12-4-eae5258c59b5acf6', 64, 0),
    ]),
    # KATZE
    block((53, 12), [
        text('KATZE', [(1, 0, 53, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('16x12-4-e1cb96c14c0f5dd1', 0, 0), ('16x12-4-d5b4aa737fff2503', 16, 0),
        ('21x12-4-f5db6f33ae8224e3', 32, 0),
    ]),
    # HIGHWAYMAN
    block((122, 12), [
        text('HIGHWAYMAN', [(1, 0, 122, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-92f7072af2f8aeed', 0, 0), ('32x12-4-2972e89215775fc5', 32, 0),
        ('58x12-4-8aa579e522c1512e', 64, 0),
    ]),
    # CHIHIRO
    block((72, 12), [
        text('CHIHIRO', [(1, 0, 72, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-ff9e177be2627210', 0, 0), ('32x12-4-c9c96e43c9f903e2', 32, 0),
        ('8x12-4-b6e9a5d631c77d39', 64, 0),
    ]),
    # UTAMARU
    block((86, 12), [
        text('UTAMARU', [(1, 0, 85, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-155b1a8aa62dcd2b', 0, 0), ('32x12-4-29fce332cf0d4070', 32, 0),
        ('22x12-4-42f02630b0611b46', 64, 0),
    ]),
    # TONY UMEDA
    block((118, 12), [
        text('TONY UMEDA', [(0, 0, 116, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-bbc8e7b1b5377dab', 0, 0), ('32x12-4-1fab298c51a628b8', 32, 0),
        ('54x12-4-0dcecf0f10980263', 64, 0),
    ]),
    # ISOHACHI
    block((82, 12), [
        text('ISOHACHI', [(1, 0, 82, 12)], 'thin', 'plain', CARD_NAME_TOP, CARD_NAME_BOTTOM, (0, 0, 0)),
    ], [
        ('32x12-4-22954d4c95990b88', 0, 0), ('32x12-4-a1ee6b0eea567260', 32, 0),
        ('18x12-4-be8c58b84ba0710e', 64, 0),
    ]),
    # -- the options' pages (setup) --
    # Frontal Attack / Reverse / Change Stance / Default / Custom (the key configuration's rows)
    block((109, 43), [
        text('Frontal', [(0, 0, 52, 7)], 'sans', 'edge', (220, 213, 229), (217, 210, 228), (106, 90, 148), screen='setup'),
        text('Attack', [(56, 0, 103, 7)], 'sans', 'edge', (230, 224, 236), (211, 203, 223), (106, 90, 148), screen='setup'),
        text('Reverse', [(0, 9, 58, 16)], 'sans', 'edge', (208, 200, 218), (212, 205, 224), (106, 90, 148), screen='setup'),
        text('Change', [(0, 18, 53, 27)], 'sans', 'edge', (210, 202, 224), (211, 203, 223), (106, 90, 148), screen='setup'),
        text('Stance', [(58, 18, 109, 27)], 'sans', 'edge', (198, 192, 211), (209, 201, 220), (106, 90, 148), screen='setup'),
        text('Default', [(0, 27, 57, 34)], 'sans', 'edge', (215, 208, 225), (212, 205, 224), (106, 90, 148), screen='setup'),
        text('Custom', [(0, 36, 55, 43)], 'sans', 'edge', (212, 205, 223), (209, 201, 220), (106, 90, 148), screen='setup'),
    ], [
        ('52x9-4-6378171017f2c657', 0, 0), ('47x9-4-b5017fa0624ef1c2', 56, 0), ('58x9-4-63bf55d40d817a1d', 0, 9),
        ('53x9-4-902fdd545e5ecd09', 0, 18), ('51x9-4-bd2f1e0100633535', 58, 18), ('57x7-4-55ccbbf8a92a8ee0', 0, 27),
        ('55x7-4-71cda711f2428507', 0, 36),
    ]),
    # 1P / 2P
    block((184, 24), [
        text('1P', [(75, 0, 112, 11)], 'heavy', 'edge', (230, 230, 230), (203, 203, 203), (49, 49, 49), screen='setup'),
        rule((1, 6, 68, 7), (255, 255, 255)),
        rule((119, 6, 183, 7), (255, 255, 255)),
        text('2P', [(72, 12, 113, 23)], 'heavy', 'edge', (235, 235, 235), (204, 204, 204), (57, 57, 57), screen='setup'),
        rule((64, 18, 68, 19), (255, 255, 255)),
        rule((116, 18, 128, 19), (255, 255, 255)),
    ], [
        ('64x12-4-0465c91a49cd099f', 0, 0), ('64x12-4-d3202adaf3d08d92', 64, 0), ('56x12-4-ccf87c2d008343ff', 128, 0),
        ('64x12-4-b992ffcd289d80de', 64, 12),
    ]),
    # EXIT
    block((180, 14), [
        text('EXIT', [(60, 1, 120, 12)], 'sans', 'plain', (100, 100, 0), (71, 71, 0), (255, 255, 0), panel=(0, 0, 180, 14), screen='setup'),
    ], [
        ('64x14-4-0b766cd86f508913', 0, 0), ('64x14-4-56f8c5503802da18', 64, 0), ('52x14-4-0f2ff7f703cde6fc', 128, 0),
    ]),
    # OK
    block((180, 14), [
        text('OK', [(69, 2, 109, 13)], 'sans', 'plain', (57, 60, 85), (58, 63, 88), (165, 165, 255), panel=(0, 0, 180, 14), screen='setup'),
    ], [
        ('64x14-4-48f51f02cfa15b75', 0, 0), ('64x14-4-f0864d51a5984ec7', 64, 0), ('52x14-4-03c50b7a96728b71', 128, 0),
    ]),
    # N/A
    block((180, 14), [
        text('N/A', [(65, 2, 113, 13)], 'sans', 'plain', (65, 34, 11), (109, 58, 17), (255, 140, 57), panel=(0, 0, 180, 14), screen='setup'),
    ], [
        ('64x14-4-20cd2df4e6ed5d95', 0, 0), ('64x14-4-a67ca72bb3d95b12', 64, 0), ('52x14-4-50647ee38fd9ceb9', 128, 0),
    ]),
    # Memory Card / Key Configuration (the titles of the options' pages)
    block((224, 36), [
        text('Memory Card', [(0, 1, 168, 17)], 'sans', 'edge', (214, 214, 214), (217, 217, 217), (16, 16, 16), screen='setup'),
        text('Key Configuration', [(0, 19, 221, 35)], 'sans', 'edge', (212, 212, 212), (204, 204, 204), (16, 16, 16), screen='setup'),
    ], [
        ('64x18-4-ff878969d06f89b0', 0, 0), ('64x18-4-52d90ef865968069', 64, 0), ('47x18-4-03ee51327fdc7e0c', 128, 0),
        ('64x18-4-bcb71400bbab2454', 0, 18), ('64x18-4-f229fdd0f0cdc29e', 64, 18), ('64x18-4-ec0411ed33b2ee0c', 128, 18),
        ('32x18-4-90e181f1c672190b', 192, 18),
    ]),
    # Option Menu
    block((159, 18), [
        text('Option Menu', [(0, 1, 159, 17)], 'sans', 'edge', (215, 215, 215), (205, 205, 205), (16, 16, 16), screen='setup'),
    ], [
        ('64x18-4-ac788bcdcc1dd241', 0, 0), ('64x18-4-002a50732d61a9fe', 64, 0), ('31x18-4-58f5806fa2349184', 128, 0),
    ]),
    # Color / B&W / On / Off / Load / Data
    block((125, 48), [
        text('Color', [(0, 1, 52, 12)], 'sans', 'edge', (205, 205, 205), (211, 211, 211), (90, 90, 90), screen='setup'),
        text('B&W', [(54, 1, 118, 12)], 'sans', 'edge', (219, 219, 219), (209, 209, 209), (90, 90, 90), screen='setup'),
        text('On', [(0, 17, 32, 29)], 'sans', 'edge', (215, 215, 215), (211, 211, 211), (90, 90, 90), screen='setup'),
        text('Off', [(32, 17, 67, 29)], 'sans', 'edge', (211, 211, 211), (211, 211, 211), (74, 74, 74), screen='setup'),
        text('Load', [(67, 17, 125, 29)], 'sans', 'edge', (211, 211, 211), (221, 221, 221), (24, 24, 24), screen='setup'),
        text('Data', [(8, 33, 63, 44)], 'sans', 'edge', (193, 193, 193), (220, 220, 220), (41, 41, 41), screen='setup'),
    ], [
        ('62x16-4-a820ec12e48c77f2', 0, 0), ('63x16-4-e8e4bb9037c46983', 62, 0), ('33x16-4-cf67eedd75bfc77e', 0, 16),
        ('34x16-4-cc2a0f58b5024f96', 34, 16), ('57x16-4-07609dd79f5369ef', 68, 16), ('63x16-4-6156a41631858050', 0, 32),
    ]),
    # Hard / Easy / Save
    block((110, 32), [
        text('Hard', [(0, 2, 53, 15)], 'sans', 'edge', (203, 203, 203), (205, 205, 205), (90, 90, 90), screen='setup'),
        text('Easy', [(54, 2, 110, 15)], 'sans', 'edge', (212, 212, 212), (207, 207, 207), (90, 90, 90), screen='setup'),
        text('Save', [(0, 17, 60, 28)], 'sans', 'edge', (211, 211, 211), (219, 219, 219), (24, 24, 24), screen='setup'),
    ], [
        ('53x16-4-287a7afc7edc240f', 0, 0), ('56x16-4-9d5137446aff9eea', 54, 0), ('60x16-4-bc9f394b3f14f0d5', 0, 16),
    ]),
    # Monitor / Difficulty / Music / Memory Card / Return / Key Configuration / Return to Top Menu (the options' rows) (tinted, drawn from these: 15x16-4-4c2ab6d4a30ece9f, 23x16-4-4652ac5bbd75b640, 29x16-4-edd32694a9e11b86, 34x16-4-7c53e12029f3f402, 64x16-4-312e029678822e76, 64x16-4-3b0f25d6359c0665, 64x16-4-4431ebfe224b040d, 64x16-4-44df40a645e99cf5, 64x16-4-4503054e887c37da, 64x16-4-5b57743b2bd0b352, 64x16-4-8b95d7e2821f4840, 64x16-4-bfde8f0f5842d18b)
    block((250, 64), [
        text('Monitor', [(0, 1, 85, 15)], 'sans', 'edge', (194, 194, 194), (193, 193, 193), (0, 0, 0), screen='setup'),
        text('Difficulty', [(85, 1, 185, 15)], 'sans', 'edge', (195, 195, 195), (190, 190, 190), (0, 0, 0), screen='setup'),
        text('Music', [(185, 1, 250, 15)], 'sans', 'edge', (197, 197, 197), (201, 201, 201), (0, 0, 0), screen='setup'),
        text('Memory Card', [(0, 17, 155, 31)], 'sans', 'edge', (200, 200, 200), (183, 183, 183), (0, 0, 0), screen='setup'),
        text('Return', [(155, 17, 235, 31)], 'sans', 'edge', (200, 200, 32), (210, 210, 34), (0, 0, 0), screen='setup'),
        text('Key Configuration', [(0, 33, 207, 47)], 'sans', 'edge', (196, 196, 196), (186, 186, 186), (0, 0, 0), screen='setup'),
        text('Return to Top Menu', [(0, 49, 228, 63)], 'sans', 'edge', (197, 197, 197), (190, 190, 190), (0, 0, 0), screen='setup'),
    ], [
        ('64x16-4-a86a418be9d2732c', 0, 0), ('23x16-4-041c0f6f42415309', 64, 0), ('64x16-4-100e99727fb11ec4', 88, 0),
        ('34x16-4-2dad72e8201f3e9f', 152, 0), ('64x16-4-0bff80788e999ca3', 186, 0), ('64x16-4-495f3af606eae509', 0, 16),
        ('64x16-4-3271d24f04acac66', 64, 16), ('29x16-4-1246602f5ff27773', 128, 16), ('64x16-4-99ebcf67e646125c', 158, 16),
        ('13x16-4-c30efc501d9eafa9', 222, 16), ('64x16-4-18dd56da4ed184af', 0, 32), ('64x16-4-719e41ebd3b6fde7', 64, 32),
        ('64x16-4-7c103b1e93b7e4bc', 128, 32), ('15x16-4-b44ec55feaa338fa', 192, 32), ('64x16-4-cc6fa51a3d8822b5', 0, 48),
        ('64x16-4-5048e2490a8eab69', 64, 48), ('64x16-4-5f969d6bd8a922ed', 128, 48), ('36x16-4-fcadda4d73f9d14c', 192, 48),
    ]),
    # -- end of the options' pages (setup) --
    # -- the slash mode (slash) --
    # YES / NO (the memory card's question)
    block((81, 14), [
        text('YES', [(0, 1, 40, 11)], 'sans', 'edge', (190, 190, 190), (193, 193, 193), (16, 16, 16), screen='slash'),
        text('/', [(41, 1, 51, 11)], 'sans', 'edge', (222, 222, 222), (194, 194, 194), (16, 16, 16), screen='slash'),
        text('NO', [(51, 1, 81, 11)], 'sans', 'edge', (207, 207, 207), (201, 201, 201), (16, 16, 16), screen='slash'),
    ], [
        ('40x14-4-09f1d315ab624090', 0, 0), ('10x14-4-504fadd3a436ea84', 40, 0), ('31x14-4-fd3f270ba61046bb', 50, 0),
    ]),
    # 100 Kills Time Trial Records (the slash mode's page and its best times)
    block((256, 24), [
        text('100 Kills Time Trial Records', [(0, 0, 256, 11), (0, 12, 93, 23)], 'serif', 'edge', (47, 192, 198),
             (49, 197, 208), (24, 49, 49), screen='slash'),
    ], [
        ('64x12-4-7475da21699fbee3', 0, 0), ('64x12-4-e29b175b88bb00a9', 64, 0), ('64x12-4-9fdf3c160213bddf', 128, 0),
        ('64x12-4-40cb6b2697874e26', 192, 0), ('64x12-4-441e19c78fd79c78', 0, 12), ('30x12-4-e28398c1c9f63437', 64, 12),
    ]),
    # Memory Card has been removed
    block((204, 28), [
        text('Memory Card has been', [(0, 1, 204, 13)], 'sans', 'edge', (185, 185, 185), (197, 197, 197), (16, 16, 16), screen='slash'),
        text('removed', [(5, 15, 83, 25)], 'sans', 'edge', (201, 201, 201), (197, 197, 197), (16, 16, 16), screen='slash'),
    ], [
        ('64x14-4-8b3fc5cdc55f0af3', 0, 0), ('64x14-4-6837eacefa09a296', 64, 0), ('64x14-4-e24b401d4c379792', 128, 0),
        ('12x14-4-1543c65b874eaeec', 192, 0), ('64x14-4-b04ca7e9138032bf', 0, 14), ('19x14-4-45cbe01ec5cccb8b', 64, 14),
    ]),
    # Save current data / on Memory Card?
    block((157, 28), [
        text('Save current data', [(0, 3, 157, 13)], 'sans', 'edge', (197, 197, 197), (197, 197, 197), (16, 16, 16), screen='slash'),
        text('on Memory Card?', [(0, 15, 156, 27)], 'sans', 'edge', (187, 187, 187), (171, 171, 171), (16, 16, 16), screen='slash'),
    ], [
        ('64x28-4-3caed2584a2bf914', 0, 0), ('64x28-4-151dad2d8efc7559', 64, 0), ('29x28-4-a6235fb322ea65ad', 128, 0),
    ]),
    # 100 Kills Time Trial (the slash mode's banner)
    block((256, 32), [
        text('100 Kills Time Trial', [(40, 12, 215, 20)], 'sans', 'edge', (229, 229, 229), (214, 214, 214), (82, 82, 82), screen='slash'),
    ], [
        ('64x32-4-8f9d6157aab5cce8', 0, 0), ('64x32-4-bc5055557d06439a', 64, 0), ('64x32-4-75ec75120d71cb69', 128, 0),
        ('64x32-4-8ebb0b91991880d2', 192, 0),
    ]),
    # Slash Mode (the top menu's caption)
    block((136, 14), [
        text('Slash Mode', [(0, 1, 136, 11)], 'thin', **CAPTION),
    ], [
        ('64x14-4-7a696e02dc59d94c', 0, 0), ('64x14-4-4606258b6b108b3c', 64, 0), ('8x14-4-603f58428c63ed23', 128, 0),
    ]),
    # Slash Mode (the slash mode's page)
    block((148, 18), [
        text('Slash Mode', [(0, 1, 148, 14)], 'sans', 'edge', (221, 221, 221), (222, 222, 222), (16, 16, 16), screen='slash'),
    ], [
        ('64x18-4-a1e228ce29098581', 0, 0), ('64x18-4-21f541176cc93486', 64, 0), ('20x18-4-d3cac6b3a70f7edc', 128, 0),
    ]),
    # 56789: the fight's digits after 01234 (above), its clock's in the
    # slash mode
    block((95, 14), [
        text('56789', [(0, 0, 95, 14)], 'heavy', 'edge', (255, 255, 255), (255, 255, 255), (0, 0, 0)),
    ], [
        ('19x14-4-0cd31b7435567903', 0, 0), ('19x14-4-8499a3e9243f1834', 19, 0), ('19x14-4-e2eb4e5d8e909083', 38, 0),
        ('19x14-4-3a751eb7f2eceb19', 57, 0), ('19x14-4-5b8551da4c9b334a', 76, 0),
    ]),
    # -- end of the slash mode (slash) --
    # -- the staff roll (ending-0) --
    # Executive Producer ... Programmer
    block((252, 176), [
        text('Executive', [(0, 2, 128, 13)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Producer', [(10, 18, 130, 29)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Tetsuo', [(0, 34, 81, 45)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Mizuno', [(92, 34, 179, 45)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Tomoyuki', [(0, 50, 116, 64)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Takechi', [(125, 50, 217, 61)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Hironobu', [(0, 66, 110, 77)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Sakaguchi', [(120, 66, 248, 80)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Hisashi', [(0, 82, 88, 93)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Suzuki', [(99, 82, 180, 93)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Tetsuhisa', [(0, 98, 117, 109)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Tsuruzono', [(125, 98, 252, 109)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Producer', [(0, 114, 120, 125)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Director', [(121, 114, 228, 125)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('KUNIHIKO', [(0, 131, 135, 142)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('NAKATA', [(16, 147, 121, 158)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('Programmer', [(0, 162, 167, 176)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
    ], [
        ('128x16-4-e0516368bdeaa88e', 0, 0), ('130x16-4-93cc0603a2cb5228', 0, 16), ('81x16-4-77d4b7dcb5b66ec1', 0, 32),
        ('87x16-4-41c742224018bf22', 92, 32), ('116x16-4-d4b890c389c3b25e', 0, 48), ('93x16-4-2d50538c92cb03ca', 124, 48),
        ('110x16-4-6862afabca4c2fd5', 0, 64), ('128x16-4-421a6a2c11cce2f4', 120, 64), ('88x16-4-8278b596f06c1f4d', 0, 80),
        ('82x16-4-313896d455b77deb', 98, 80), ('118x16-4-79ded1846432ec9c', 0, 96), ('128x16-4-b62818237fbe6ae2', 124, 96),
        ('120x16-4-943bbb627ca6378c', 0, 112), ('109x16-4-dd01831fcfafe432', 120, 112), ('136x16-4-7e9a992615b51a79', 0, 128),
        ('105x16-4-6af23f2edd4d588b', 16, 144), ('167x16-4-a5381dedd84c011f', 0, 160),
    ]),
    # NAOTAKA / MASAKI
    block((129, 32), [
        text('NAOTAKA', [(0, 2, 129, 13)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('MASAKI', [(0, 18, 107, 29)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
    ], [
        ('129x16-4-417bf02c89b3e838', 0, 0), ('107x16-4-572d08beaa91bb29', 0, 16),
    ]),
    # UEDA
    block((74, 16), [
        text('UEDA', [(0, 2, 73, 13)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
    ], [
        ('74x16-4-9c9d25e4cf1fd5ce', 0, 0),
    ]),
    # Kazuhiro Hasegawa ... and all staffs of (the logo under it is the art's)
    block((248, 64), [
        text('Kazuhiro', [(0, 1, 107, 12)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Hasegawa', [(118, 1, 247, 15)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Takeshi', [(0, 18, 93, 29)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Fusano', [(106, 18, 195, 29)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Kiyoko', [(0, 33, 82, 47)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Maeda', [(92, 33, 175, 44)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('and all staffs of', [(0, 50, 186, 62)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
    ], [
        ('107x16-4-f64a8060d5d4c94d', 0, 0), ('130x16-4-669a4c85a762c454', 118, 0), ('93x16-4-f6656cf9d1877053', 0, 16),
        ('91x16-4-6c83a808be6e6ac0', 104, 16), ('82x16-4-3e02b76be410b041', 0, 32), ('83x16-4-4d0a24cc3da2a53d', 92, 32),
        ('186x16-4-4efe64f7ba9e2cea', 0, 48),
    ]),
    # BUSHIDO BLADE 2 STAFF
    block((255, 32), [
        text('BUSHIDO BLADE 2', [(0, 2, 255, 13)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('STAFF', [(9, 18, 96, 29)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
    ], [
        ('255x16-4-cddd647a4231b525', 0, 0), ('96x16-4-18dbfc1312fdd491', 0, 16),
    ]),
    # Junya Kuroda
    block((176, 16), [
        text('Junya', [(0, 1, 79, 15)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Kuroda', [(81, 1, 175, 13)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
    ], [
        ('80x16-4-a3cbb861506ed5fa', 0, 0), ('96x16-4-13d9c48f4b0163b6', 80, 0),
    ]),
    # YAMASHITA ... Motion Capturing
    block((255, 256), [
        text('YAMASHITA', [(10, 2, 163, 13)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('Stage Design', [(0, 18, 183, 32)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('HIROAKI', [(0, 34, 115, 45)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('NAKAGAWA', [(16, 50, 174, 61)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('TAKAFUMI', [(0, 66, 138, 77)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('KAGAYA', [(146, 66, 255, 77)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('ETSUKO', [(0, 82, 114, 93)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('YAMADA', [(133, 82, 244, 93)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('Character Design', [(0, 98, 230, 112)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('NAHO', [(0, 114, 80, 125)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('KODAMA', [(96, 114, 215, 125)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('2D Design', [(1, 130, 137, 144)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('MASATAKA', [(1, 146, 146, 157)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('AOCHI', [(161, 146, 251, 157)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('Motion Creating', [(0, 162, 212, 176)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('MASAKAZU', [(0, 178, 154, 189)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('TAGAWA', [(10, 194, 123, 205)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('Motion Control', [(0, 210, 206, 221)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('KYOKO', [(0, 226, 99, 237)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('TANGE', [(117, 226, 210, 237)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('Motion Capturing', [(1, 242, 239, 256)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
    ], [
        ('153x16-4-0a105587dce4225a', 10, 0), ('183x16-4-7612a20293bb71d5', 0, 16), ('115x16-4-58d3a915d268a3d9', 0, 32),
        ('158x16-4-82cfd502f37a79b3', 16, 48), ('138x16-4-ce34020188bfc4a5', 0, 64), ('109x16-4-1eff8ca3b61f10d0', 146, 64),
        ('114x16-4-ccd3d5b97b0cf519', 0, 80), ('112x16-4-d297f27b7c85cb0f', 132, 80), ('230x16-4-676b4840c4839b67', 0, 96),
        ('81x16-4-212beaadabe8755f', 0, 112), ('119x16-4-fdf2060dc1a41378', 96, 112), ('137x16-4-5c9e1db8a4511507', 0, 128),
        ('147x16-4-aff13d7a267c0a0d', 0, 144), ('92x16-4-4d92157f78073618', 160, 144), ('212x16-4-213daf8aeb14ef7d', 0, 160),
        ('154x16-4-02a88465d71699a8', 0, 176), ('116x16-4-2265f2713fe495a3', 8, 192), ('207x16-4-cc030e5e4fdfaab5', 0, 208),
        ('100x16-4-5f11f6130cb39c64', 0, 224), ('95x16-4-44d044f63299368a', 116, 224), ('239x16-4-f4995fde0a85fbbe', 0, 240),
    ]),
    # TETSUYA OKAMOTO ... Teruaki
    block((168, 128), [
        text('TETSUYA', [(1, 2, 124, 13)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('OKAMOTO', [(17, 18, 158, 29)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('Data Control', [(0, 34, 168, 45)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('TOMOHIRO', [(0, 50, 151, 61)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('MARUYAMA', [(8, 66, 166, 77)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), slant=0.3, screen='staff'),
        text('Music', [(0, 82, 77, 93)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Ryuji', [(80, 82, 139, 96)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Sound', [(0, 98, 84, 109)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Teruaki', [(0, 114, 89, 125)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
    ], [
        ('124x16-4-6149e3a312a45821', 0, 0), ('142x16-4-dfb135950ab6dfde', 16, 16), ('168x16-4-a1cdb1a785a57bdf', 0, 32),
        ('151x16-4-3fe785b9e3cdddda', 0, 48), ('158x16-4-3be2f4b98d00a4d9', 8, 64), ('77x16-4-c920b795849028fe', 0, 80),
        ('59x16-4-6f5a935db480767f', 80, 82), ('84x16-4-edc596616607d5ea', 0, 96), ('89x16-4-df661326a3ceb8b0', 0, 112),
    ]),
    # Sasai
    block((68, 16), [
        text('Sasai', [(1, 0, 68, 11)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
    ], [
        ('68x16-4-62f7ee36d54ee5d3', 0, 0),
    ]),
    # Sugawara
    block((125, 16), [
        text('Sugawara', [(1, 2, 125, 16)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
    ], [
        ('125x16-4-57ed9680e76d8721', 0, 0),
    ]),
    # Opening Movie ... Mari Nishikawa (on black)
    block((255, 256), [
        text('Opening Movie', [(0, 1, 200, 15)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 0, 200, 16), screen='staff'),
        text('Digital Media Lab.', [(0, 17, 236, 31)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 16, 236, 32), screen='staff'),
        text('Localization Directo', [(0, 33, 255, 44)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 32, 255, 48), screen='staff'),
        text('r', [(0, 52, 19, 60)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 48, 19, 64), screen='staff'),
        text('.)', [(20, 48, 38, 62)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(20, 48, 38, 64), screen='staff'),
        text('INC.', [(45, 48, 109, 62)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(38, 48, 109, 64), screen='staff'),
        text('ement', [(110, 49, 191, 60)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(110, 48, 191, 64), screen='staff'),
        text('Ichiro Nonaka (S.C.L', [(0, 64, 255, 78)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 64, 255, 80), screen='staff'),
        text('SQUARE SOFT,', [(0, 80, 249, 96)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 80, 255, 96), screen='staff'),
        text('Localization Manag', [(0, 97, 254, 110)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 96, 255, 112), screen='staff'),
        text('Masahiro Nakajima', [(0, 113, 236, 126)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 112, 236, 128), screen='staff'),
        text('Randall Fujimoto', [(0, 129, 206, 142)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 128, 206, 144), screen='staff'),
        text('Localization Projec', [(0, 145, 255, 158)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 144, 255, 160), screen='staff'),
        text('t Leader', [(0, 161, 106, 172)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 160, 106, 176), screen='staff'),
        text('Yutaka Sano', [(0, 177, 157, 188)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 176, 157, 192), screen='staff'),
        text('lists', [(160, 177, 214, 188)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(158, 176, 214, 192), screen='staff'),
        text('Localization Specia', [(0, 193, 255, 207)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 192, 255, 208), screen='staff'),
        text('Naomi Cooper', [(0, 208, 176, 222)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 208, 176, 224), screen='staff'),
        text('Sammy Matsushima', [(0, 225, 252, 239)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 224, 252, 240), screen='staff'),
        text('Mari Nishikawa', [(0, 241, 186, 252)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 240, 186, 256), screen='staff'),
    ], [
        ('200x16-4-1c362e6d6759365f', 0, 0), ('236x16-4-a521391922bdde70', 0, 16), ('255x16-4-433c98e7d333680a', 0, 32),
        ('19x16-4-34c3768b47434c86', 0, 48), ('18x16-4-42e4c799d4d353c2', 20, 48), ('71x16-4-e47361b18e92d102', 38, 48),
        ('81x16-4-88695c90c0b5f9d4', 110, 48), ('255x16-4-ea8cc2def5458582', 0, 64), ('255x16-4-bd62e4af0f6ee9e0', 0, 80),
        ('255x16-4-4c7fd4c694a7595a', 0, 96), ('236x16-4-0a0574d4f6cd15ea', 0, 112), ('206x16-4-b37479f2610ca273', 0, 128),
        ('255x16-4-ec4f27f6e7e83982', 0, 144), ('106x16-4-018cda0506e6ac9b', 0, 160), ('157x16-4-dd3a00eaeee20d7e', 0, 176),
        ('56x16-4-f24c7c724e4f1e4d', 158, 176), ('255x16-4-35b37a8524b3c088', 0, 192), ('176x16-4-32e175022d11d5ac', 0, 208),
        ('252x16-4-503c8f51d59a9e2f', 0, 224), ('186x16-4-b24a8c89990c0c57', 0, 240),
    ]),
    # Associate Producer ... Jonathan Williams (on black)
    block((255, 64), [
        text('Associate Produce', [(0, 1, 255, 12)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 0, 255, 16), screen='staff'),
        text('r', [(0, 19, 7, 27)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 16, 7, 32), screen='staff'),
        text('Akihito Kozu', [(8, 17, 162, 28)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(8, 16, 162, 32), screen='staff'),
        text('QA Manager', [(0, 33, 162, 47)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 32, 162, 48), screen='staff'),
        text('Jonathan Williams', [(0, 49, 226, 61)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), panel=(0, 48, 226, 64), screen='staff'),
    ], [
        ('255x16-4-a6ea6a1c9916b8ea', 0, 0), ('7x16-4-5b8f2865ca1a812b', 0, 16), ('154x16-4-9a6ac2946994b5ea', 8, 16),
        ('162x16-4-92978efaea8bd020', 0, 32), ('226x16-4-432d334f5f7d4762', 0, 48),
    ]),
    # Special Thanks to ...
    block((249, 112), [
        text('Special Thanks to', [(1, 2, 236, 16)], 'sans', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Nobuyasu', [(0, 18, 123, 32)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Asano', [(131, 18, 209, 29)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Takashi', [(0, 34, 93, 45)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Hashizume', [(112, 34, 249, 45)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Tsutomu', [(0, 50, 104, 61)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Mawatari', [(123, 50, 233, 61)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Shingo', [(0, 66, 85, 80)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Akiyama', [(100, 66, 207, 80)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Takeshi', [(1, 82, 94, 93)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Takenaka', [(110, 82, 229, 93)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Toshiyuki', [(0, 98, 113, 112)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
        text('Saotome', [(131, 98, 242, 109)], 'serif', 'plain', (255, 255, 255), (255, 255, 255), (0, 0, 0), screen='staff'),
    ], [
        ('236x16-4-dbbc188eacbcb2db', 0, 0), ('123x16-4-498553d44ea3f939', 0, 16), ('79x16-4-edd088ffea0da0cf', 130, 16),
        ('93x16-4-8ce1946da337af04', 0, 32), ('137x16-4-82309a5a5c733287', 112, 32), ('104x16-4-7c162bd4aa49cb36', 0, 48),
        ('111x16-4-244a98db8ade1134', 122, 48), ('85x16-4-d1e83e988ffa8da2', 0, 64), ('107x16-4-d72da315c36a0073', 100, 64),
        ('94x16-4-50097f24258d3604', 0, 80), ('119x16-4-7b49f1d95992e8de', 110, 80), ('113x16-4-8bc1c5db0ab4be29', 0, 96),
        ('112x16-4-509fa7bdea75227e', 130, 96),
    ]),
    # -- end of the staff roll (ending-0) --
    # -- a fight begun again (story) --
    # REMATCH (under the story's 再戦, a fight begun again) (tinted, drawn from these: 26x10-4-eb04d7182efe1d56, 64x10-4-bf5f976697e035d8)
    block((90, 10), [
        text('REMATCH', [(0, 0, 90, 9)], 'sans', 'edge', (212, 212, 212), (215, 215, 215), (0, 0, 0), screen='story'),
    ], [
        ('64x10-4-5261391d6cb0457f', 0, 0), ('26x10-4-24e3b89cd88ce67d', 64, 0),
    ]),
    # -- end of a fight begun again (story) --
]

# The built-in font's characters: each key, eight by eight and seven by
# seven, and the character.
GLYPHS = [
    ('8x8-4-24854331d991b347', '!'), ('7x7-4-a12f31181ba0cd4f', '!'), ('8x8-4-60d013174d51e469', '"'),
    ('7x7-4-a40a07628c71b64b', '"'), ('8x8-4-f240001670d9b56d', '#'), ('7x7-4-5fa3413a92c41c47', '#'),
    ('8x8-4-3ed8b74d5c879a63', '$'), ('7x7-4-d76684894ef56125', '$'), ('8x8-4-ee2258813da951ce', '%'),
    ('7x7-4-923d8d7610246e00', '%'), ('8x8-4-e1803ffc499083bc', '&'), ('7x7-4-271fcaed94f18652', '&'),
    ('8x8-4-f44fbd628eaeca13', "'"), ('7x7-4-db3ff64543b6f38f', "'"), ('8x8-4-800c83bcbf9639df', '('),
    ('7x7-4-5e5bf9498fb455a5', '('), ('8x8-4-7528d067b2565787', ')'), ('7x7-4-76ccbe3cd6601b8f', ')'),
    ('8x8-4-b9bc6d2807d200a9', '*'), ('7x7-4-50d6d1960311d48b', '*'), ('8x8-4-952a4e6e0a0a67ad', '+'),
    ('7x7-4-b046ce706b88c087', '+'), ('8x8-4-31f0be06f37f7aa3', ','), ('7x7-4-9b5a706574cc0765', ','),
    ('8x8-4-bad281eadacd140e', '-'), ('7x7-4-a0e59e4b6c34c040', '-'), ('8x8-4-29ed18a82beaabfc', '.'),
    ('7x7-4-2a734f205f327a92', '.'), ('8x8-4-9198018fa27f4253', '/'), ('7x7-4-892ff58f1e4af9cf', '/'),
    ('8x8-4-5f666a9c8dadae5f', '0'), ('7x7-4-cc0a6f43d1dc8825', '0'), ('8x8-4-0351673865af3007', '1'),
    ('7x7-4-3cd4ca7244bed60f', '1'), ('8x8-4-51dd4294828b2529', '2'), ('7x7-4-0116fc4ea6f9a70b', '2'),
    ('8x8-4-b2c48b807a1e5a2d', '3'), ('7x7-4-3e6f65411ee19907', '3'), ('8x8-4-89d0595017033923', '4'),
    ('7x7-4-cc999b1ef74285e5', '4'), ('8x8-4-5d3bd0cb73b1288e', '5'), ('7x7-4-0b7ee8215ff09cc0', '5'),
    ('8x8-4-426b53d5a5b6167c', '6'), ('7x7-4-0124fd2d178fed12', '6'), ('8x8-4-80d71975bd4dcad3', '7'),
    ('7x7-4-038c3693a78b084f', '7'), ('8x8-4-3275940cbf25f55f', '8'), ('7x7-4-c33e4610ad4c4b25', '8'),
    ('8x8-4-86a8a1a88d5ff707', '9'), ('7x7-4-629292e0f2d3c90f', '9'), ('8x8-4-863035a7e606a029', ':'),
    ('7x7-4-ecb2e90a78e9540b', ':'), ('8x8-4-cd62c9bb62cb4d2d', ';'), ('7x7-4-c1c69fb146926007', ';'),
    ('8x8-4-2d4f16a3b0395023', '<'), ('7x7-4-369cff401ac52ae5', '<'), ('8x8-4-d8b5686c305bc98e', '='),
    ('7x7-4-871f523b2073fdc0', '='), ('8x8-4-eec8e1a87f26d57c', '>'), ('7x7-4-ec2b826d58e14e12', '>'),
    ('8x8-4-0bbe23a37a18d9d3', '?'), ('7x7-4-a252e8637df1634f', '?'), ('8x8-4-2dd983001957435e', '@'),
    ('7x7-4-0bc6c551c21c1bd4', '@'), ('8x8-4-8cf3b6cb554629bf', 'A'), ('7x7-4-1a3c7caa08dd2865', 'A'),
    ('8x8-4-da9019365a7aa0dd', 'B'), ('7x7-4-5e1e29f1e811d4d3', 'B'), ('8x8-4-de15b616083f9577', 'C'),
    ('7x7-4-a4a8221a066b47b9', 'C'), ('8x8-4-99c7fdfb475fc205', 'D'), ('7x7-4-f58793a656356e47', 'D'),
    ('8x8-4-de9041dc7ace6e41', 'E'), ('7x7-4-c65f8be0f9519f7b', 'E'), ('8x8-4-6a76f62ae5c9b994', 'F'),
    ('7x7-4-0d9178d28efce13c', 'F'), ('8x8-4-7411903e008e8d5a', 'G'), ('7x7-4-f43c43f4ac796bc2', 'G'),
    ('8x8-4-9ac3a2e5bce5079e', 'H'), ('7x7-4-f25b08c41fe66c14', 'H'), ('8x8-4-692e343c221265ff', 'I'),
    ('7x7-4-d7a1ad8ec433eca5', 'I'), ('8x8-4-fda05c82b327a91d', 'J'), ('7x7-4-5314f454c0452713', 'J'),
    ('8x8-4-1d6aadf4a14d55b7', 'K'), ('7x7-4-d0b95b382fec55f9', 'K'), ('8x8-4-6f1d7bfc95c18e45', 'L'),
    ('7x7-4-462b20dc2efa1287', 'L'), ('8x8-4-9370580af6f3f881', 'M'), ('7x7-4-05082c3b35c2efbb', 'M'),
    ('8x8-4-682855d368c8cbd4', 'N'), ('7x7-4-617bf3ab5a80f17c', 'N'), ('8x8-4-93357df7954a199a', 'O'),
    ('7x7-4-0d92f2c42d45f802', 'O'), ('8x8-4-5715ba9e4f0f681e', 'P'), ('7x7-4-3cfd6f7328d77e94', 'P'),
    ('8x8-4-2a1ab3c2c6ce927f', 'Q'), ('7x7-4-45502389065c1f25', 'Q'), ('8x8-4-82f0e8e5ec113b9d', 'R'),
    ('7x7-4-b03fba22289cd393', 'R'), ('8x8-4-2d922bae55f14637', 'S'), ('7x7-4-9341d3f0f36ef079', 'S'),
    ('8x8-4-f35dc33ce77284c5', 'T'), ('7x7-4-d453b7ace252eb07', 'T'), ('8x8-4-b8c67fe741b94101', 'U'),
    ('7x7-4-f6d8c36bc3c6d23b', 'U'), ('8x8-4-361c1371025aa454', 'V'), ('7x7-4-2b75d3483331c1fc', 'V'),
    ('8x8-4-29ecd264304de01a', 'W'), ('7x7-4-7631001245e35e82', 'W'), ('8x8-4-2ec2f359d802ad1e', 'X'),
    ('7x7-4-950d080f5a5b7d94', 'X'), ('8x8-4-4d0091736149a97f', 'Y'), ('7x7-4-3c83fa55e1cbe225', 'Y'),
    ('8x8-4-69552a9ae7ceae9d', 'Z'), ('7x7-4-7c1c93c79a0c4c93', 'Z'), ('8x8-4-8bd9c811611e3737', '['),
    ('7x7-4-0474354a05530579', '['), ('8x8-4-4abb9a9645dce1c5', '\\'), ('7x7-4-57aaf21d0a03b207', '\\'),
    ('8x8-4-ba30d2cb601a9201', ']'), ('7x7-4-61920134937b393b', ']'), ('8x8-4-8637645d8e5c8954', '^'),
    ('7x7-4-382b5417b55504fc', '^'), ('8x8-4-c64c547383f0571a', '_'), ('7x7-4-1519ce0801695182', '_'),
    ('8x8-4-ef49f35bc12ec2c1', '`'), ('7x7-4-cfc5a2d8e5645dcd', '`'), ('8x8-4-5e900168e776a278', 'a'),
    ('7x7-4-de214283f15cef54', 'a'), ('8x8-4-af4dfaa5f316d511', 'b'), ('7x7-4-408e1262b724647f', 'b'),
    ('8x8-4-774aa14e73166cb9', 'c'), ('7x7-4-bd9ae479cd72b9d3', 'c'), ('8x8-4-714ac1d21d5fa931', 'd'),
    ('7x7-4-9bb5bd593e7bf7c7', 'd'), ('8x8-4-fdcaa42b3bf037ea', 'e'), ('7x7-4-6561fab5d571ece2', 'e'),
    ('8x8-4-f3ff3fbac5e84ae8', 'f'), ('7x7-4-e45e0842b9971ed4', 'f'), ('8x8-4-3a896ad3f36a5f02', 'g'),
    ('7x7-4-d3de66e783e088ef', 'g'), ('8x8-4-f4fc5d6713ccdd01', 'h'), ('7x7-4-4710468badb4340d', 'h'),
    ('8x8-4-2b69eb8963ee26b8', 'i'), ('7x7-4-77009b44231bbb94', 'i'), ('8x8-4-9edd7b606a7acd51', 'j'),
    ('7x7-4-b03f000dcc02f6bf', 'j'), ('8x8-4-a35bda6c9c977af9', 'k'), ('7x7-4-b291aedca5a60c13', 'k'),
    ('8x8-4-32a031cefd20b971', 'l'), ('7x7-4-e8201973bd8fde07', 'l'), ('8x8-4-7aaf318f109e7c2a', 'm'),
    ('7x7-4-f51f79304bcb0f22', 'm'), ('8x8-4-a72982a648e93b28', 'n'), ('7x7-4-caf24bb517616f14', 'n'),
    ('8x8-4-415b111e23efa942', 'o'), ('7x7-4-d724bb94c8c11f2f', 'o'), ('8x8-4-561eb11cfb60d181', 'p'),
    ('7x7-4-6631c0370915768d', 'p'), ('8x8-4-cc33b860e7050b38', 'q'), ('7x7-4-2b1e8343582c7814', 'q'),
    ('8x8-4-c383a7e5375acfd1', 'r'), ('7x7-4-9331cd63fd25933f', 'r'), ('8x8-4-65e45325601a1579', 's'),
    ('7x7-4-0fbc74aa0dfdb893', 's'), ('8x8-4-12fc3c9fe61d1bf1', 't'), ('7x7-4-a17819d814ef3a87', 't'),
    ('8x8-4-1af2a595b11888aa', 'u'), ('7x7-4-d3f5ebb6bd4c47a2', 'u'), ('8x8-4-052105139a54c5a8', 'v'),
    ('7x7-4-1594b26420528194', 'v'), ('8x8-4-ad3ff717ef42a9c2', 'w'), ('7x7-4-02d747a441bfadaf', 'w'),
    ('8x8-4-85f044d032461081', 'x'), ('7x7-4-cbb69be41e90038d', 'x'), ('8x8-4-3255137f8f466c38', 'y'),
    ('7x7-4-9b5cd37f1f11fb14', 'y'), ('8x8-4-27ec589fc45ae6d1', 'z'), ('7x7-4-5e545224fbaf263f', 'z'),
    ('8x8-4-d716b47e71fe2a79', '{'), ('7x7-4-db994e4f7f6d3193', '{'), ('8x8-4-297a380a8808e6f1', '|'),
    ('7x7-4-a723385f041c7f87', '|'), ('8x8-4-825dce17e49e0daa', '}'), ('7x7-4-16493b2c685d52a2', '}'),
    ('8x8-4-d63d054850f566a8', '~'), ('7x7-4-6da44b0051d68094', '~'),
]


def font_of(name, size):
    return font(FONTS[name], size)


def strip_size(item):
    """A text's strip, its rectangles laid end to end, and the room its
    letters fill, the dark round them left out: (W, H, box_w, box_h, room)."""
    boxes = item['boxes']
    W = sum(x1 - x0 for x0, _, x1, _ in boxes) * SCALE
    H = max(y1 - y0 for _, y0, _, y1 in boxes) * SCALE
    look = item['look']
    room = SHADOW_STEP * SCALE if look == 'shadow' else EDGE_PIXELS if look == 'edge' else 0
    return W, H, W - room, H - room, room


def kind(item):
    """Texts of one kind are drawn one size: one face, one height, and
    capitals alone or not (the fighters' names, the menus' lines), of one
    screen where a text says its screen."""
    return item['font'], strip_size(item)[3], item['words'].isupper(), item['aspect'], item['screen'], item['slant']


def kind_sizes():
    """{kind: its one size} (port_lettering.common_size)."""
    sizes = {}
    for entry in LINES + BLOCKS:
        for item in entry['texts']:
            if 'rule' in item:
                continue
            _, _, box_w, box_h, _ = strip_size(item)
            sizes.setdefault(kind(item), []).append(
                fitted_size(item['words'], FONTS[item['font']], box_w, box_h, item['aspect']))
    return {k: common_size(v) for k, v in sizes.items()}


KIND_SIZES = {}


def text_strip(item):
    """The text drawn as one line across its rectangles laid end to end:
    letters, and the dark round them, SCALE pixels a texel."""
    W, H, box_w, box_h, room = strip_size(item)
    step = SHADOW_STEP * SCALE
    look = item['look']

    def narrowed(stretch):
        if stretch < SQUEEZE_MIN:
            print('port_ui: "%s" is drawn %.2f as wide as the face has it' % (item['words'], stretch))

    # a text wrapped over rectangles, or a line of the menus the game cuts
    # into pieces at fixed places (LINES), reaches the ends of its strip
    fill = len(item['boxes']) > 1 or any(item is t for entry in LINES for t in entry['texts'])
    # a leaning text is fitted to the width its lean leaves, then leant
    lean = int(round(item['slant'] * box_h))
    letters = line_mask(item['words'], FONTS[item['font']], box_w - lean, box_h, narrowed, item['aspect'],
                        KIND_SIZES.get(kind(item)), fill)
    if lean:
        upright = Image.new('L', (box_w, box_h), 0)
        upright.paste(letters, (0, 0))
        letters = upright.transform((box_w, box_h), Image.AFFINE,
                                    (1, lean / float(box_h), -lean, 0, 1, 0), resample=Image.BICUBIC)
    if item['font'] in THICKENED:
        letters = letters.filter(ImageFilter.MaxFilter(THICKEN_PIXELS * 2 + 1))
    inset = room // 2 if look == 'edge' else 0
    mask = Image.new('L', (W, H), 0)
    mask.paste(letters, (inset, inset))
    dark = Image.new('L', (W, H), 0)
    if look == 'shadow':
        dark.paste(mask, (step, step))
    elif look == 'edge':
        dark = mask.filter(ImageFilter.MaxFilter(EDGE_PIXELS * 2 + 1))
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    out.paste(Image.new('RGBA', (W, H), tuple(item['dark']) + (255,)), (0, 0), dark)
    letters = gradient((W, H), item['top'], item['bottom'], 0, H)
    out.paste(letters.convert('RGBA'), (0, 0), mask)
    return out


def block_picture(entry):
    """The whole block, every text in its rectangles, SCALE pixels a texel."""
    w, h = entry['size']
    out = Image.new('RGBA', (w * SCALE, h * SCALE), (0, 0, 0, 0))
    for item in entry['texts']:
        if 'rule' in item:
            x0, y0, x1, y1 = item['rule']
            out.paste(tuple(item['colour']) + (255,), (x0 * SCALE, y0 * SCALE, x1 * SCALE, y1 * SCALE))
            continue
        if item['panel']:
            x0, y0, x1, y1 = item['panel']
            out.paste(tuple(item['dark']) + (255,), (x0 * SCALE, y0 * SCALE, x1 * SCALE, y1 * SCALE))
        strip = text_strip(item)
        at = 0
        for x0, y0, x1, y1 in item['boxes']:
            piece = strip.crop((at, 0, at + (x1 - x0) * SCALE, (y1 - y0) * SCALE))
            out.alpha_composite(piece, (x0 * SCALE, y0 * SCALE))
            at += (x1 - x0) * SCALE
    return out


def glyph_picture(char, side):
    """A character of the built-in font: white, its capitals GLYPH_HEIGHT
    texels high from the top of its square, in the middle across."""
    S = side * SCALE
    out = Image.new('L', (S, S), 0)
    cap = GLYPH_HEIGHT * SCALE
    size = cap * 2
    while size > 4:
        f = font_of('glyph', size)
        _, top, _, bottom = f.getbbox('H')
        if bottom - top <= cap - SCALE // 2:
            break
        size -= 1
    _, cap_top, _, base = f.getbbox('H')
    left, _, right, _ = f.getbbox(char)
    width = right - left
    room = GLYPH_HEIGHT * SCALE - SCALE // 2
    margin = 2 * SCALE
    drawn = Image.new('L', (width + 2 * margin, S + 2 * margin), 0)
    ImageDraw.Draw(drawn).text((margin - left, margin + SCALE // 4 - cap_top), char, font=f, fill=255)
    if width > room:
        drawn = drawn.resize((int(drawn.width * room / float(width)), drawn.height), Image.LANCZOS)
        width = room
    out.paste(drawn, ((GLYPH_HEIGHT * SCALE - drawn.width) // 2, -margin))
    picture = Image.new('RGBA', (S, S), (255, 255, 255, 0))
    picture.putalpha(out)
    return picture


def png_bytes(image):
    """A PNG whose bytes depend on the pixels alone."""
    out = io.BytesIO()
    image.save(out, format='PNG', optimize=False, compress_level=9)
    return out.getvalue()


def pictures():
    """name: PNG bytes, for every file of the mod's textures/."""
    out = {}
    KIND_SIZES.update(kind_sizes())
    for entry in LINES + BLOCKS:
        whole = block_picture(entry)
        for key, x, y in entry['pieces']:
            w, h = (int(n) for n in key.split('-')[0].split('x'))
            out[key] = png_bytes(whole.crop((x * SCALE, y * SCALE, (x + w) * SCALE, (y + h) * SCALE)))
    for key, char in GLYPHS:
        out[key] = png_bytes(glyph_picture(char, int(key.split('x')[0])))
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
            if not check:
                # a picture of a piece no longer in the tables
                os.remove(os.path.join(there, name))
    if check:
        for name in differ:
            print('port_ui: %s is not what this script writes' % name)
        return 1 if differ else 0
    print('port_ui: %d files in %s, %d written' % (len(files), os.path.relpath(OUT, ROOT), len(differ)))
    return 0


def measure(folders):
    """Each line of LINES assembled from a dump: where its lettering is,
    and its two most common colours, the brighter the letters' and the
    darker the shadow's -- what that table's numbers came from."""
    missing = 0
    for entry in LINES:
        w, h = entry['size']
        whole = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        found = 0
        for key, x, y in entry['pieces']:
            for folder in folders:
                path = os.path.join(folder, key + '.png')
                if os.path.exists(path):
                    whole.alpha_composite(Image.open(path).convert('RGBA'), (x, y))
                    found += 1
                    break
        words = entry['texts'][0]['words']
        if found < len(entry['pieces']):
            print('%s: %d of its %d pieces not in the dumps' % (words, len(entry['pieces']) - found,
                                                                len(entry['pieces'])))
            missing += 1
            if not found:
                continue
        box = whole.getchannel('A').getbbox()
        colors = sorted(((c, rgb[:3]) for c, rgb in whole.getcolors(w * h) if rgb[3]), reverse=True)[:2]
        colors = sorted((rgb for _, rgb in colors), key=sum, reverse=True)
        print('%-20s ink=%s fill=%s shadow=%s' % (words, box, colors[0], colors[-1]))
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
