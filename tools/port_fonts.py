#!/usr/bin/env python3
"""The port's one typeface, Shippori Mincho (docs/PORT.md, "The port's
lettering"): its Latin letters cut out of the family's own files into
src/port/fonts/.

    python tools/port_fonts.py FOLDER    FOLDER has ShipporiMincho-Regular.ttf,
                                         -Bold.ttf and -ExtraBold.ttf as
                                         Google Fonts ships them
                                         (github.com/google/fonts, ofl/shipporimincho)
    python tools/port_fonts.py --list    the weights and what each is for

Shippori Mincho (FONTDASU, the SIL Open Font Licence 1.1, no Reserved Font
Name) is a Japanese mincho whose Latin letters were drawn to sit beside its
kana and kanji: wedge serifs and the stroke contrast of a brush, figures
that line up. Its files are eight megabytes each for the Japanese; the port
draws only Latin, so this keeps the printable ASCII, Latin-1 and the
common punctuation (dashes, quotes, the ellipsis, the arrows) and drops the
hinting, which neither stb_truetype nor the tools' Pillow at these sizes
uses: about 34 kilobytes a weight. The licence allows a modified
font to be shipped under it; the family's name is kept, which it allows
since it reserves none. src/port/fonts/OFL-ShipporiMincho.txt is its
licence. Needs fontTools.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'port', 'fonts')
# (file, what it is for)
WEIGHTS = [
    ('ShipporiMincho-Regular.ttf', 'text: the settings menu, lines, notes, long lettering'),
    ('ShipporiMincho-Bold.ttf', 'the menus\' lines and names, headings'),
    ('ShipporiMincho-ExtraBold.ttf', 'titles, the top menu\'s rows, digits'),
]
UNICODES = ([*range(0x20, 0x7F)] + [*range(0xA0, 0x100)] +
            [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193,
             0x2605, 0x2606, 0x25B2, 0x25B6, 0x25BC, 0x25C0])


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    if sys.argv[1] == '--list':
        for name, what in WEIGHTS:
            print('%-30s %s' % (name, what))
        return
    from fontTools import subset

    for name, _ in WEIGHTS:
        options = subset.Options()
        options.hinting = False
        options.layout_features = ['kern']
        options.name_IDs = ['*']
        options.notdef_outline = True
        font = subset.load_font(os.path.join(sys.argv[1], name), options)
        sub = subset.Subsetter(options)
        sub.populate(unicodes=UNICODES)
        sub.subset(font)
        # the same bytes each time it is made
        font['head'].modified = font['head'].created
        out = os.path.join(OUT, name)
        subset.save_font(font, out, options)
        print('port_fonts: %s, %d bytes' % (out, os.path.getsize(out)))


if __name__ == '__main__':
    main()
