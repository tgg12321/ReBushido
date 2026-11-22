#!/usr/bin/env python3
"""What of the game's 2D screens no mod replaces (docs/MODS.md, "What is
covered"): every screen the art's tours reach, played with
`--dump-textures`, and each texture the game drew held to the mods that
replace them -- the ones the program ships (src/port/mods/) and the art made
from the player's disc (tmp/mods/art/, tools/port_art.py).

    python3 tools/port_coverage.py play   [--out build/coverage] [--modes a,b]
                                          (Linux or WSL: the GL prover)
    python  tools/port_coverage.py report [--out build/coverage] [--art tmp/mods/art]
                                          [--check] [--sheet]

`play` plays port_art.py's tours (port_art.MODES) into OUT/dump, each
tour's log into OUT/logs. `report` writes OUT/report.txt and prints its
summary: for each tour, the textures it drew, and each one's replacement:

  ui, english   a picture of a built-in mod (the sharp menus' lettering,
                the English menu)
  art           the art mod's
  fade          none of its own, but a step of a fade of one that has one:
                the same size and outline, its colours that one's times a
                number for each part, which the program draws from that
                one's picture, tinted (MODS.md, "Fades")
  small         a side under four texels, which the art leaves as the game
                drew it (a rule, a dot of a cursor)
  NONE          nothing: the game's own texture, made larger by the
                card's filter

and, as a list of its own, the lettering the art alone replaces (a
texture of the art's kind `lettering` that no built-in mod has): where the
sharp menus are still to reach, though that kind has the frames and the
icons as well. With --check it ends 1 if any texture is NONE. With
--sheet it writes OUT/sheet-TOUR.png, every NONE texture and every
lettering of the art alone, by where it lay in video memory, with its key.

A tour that ends short of its screens is said so (its checkpoints, from the
log). What no tour reaches is in MODS.md, "What is covered".
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import port_art  # noqa: E402

DEFAULT_OUT = os.path.join(ROOT, 'build', 'coverage')
SHIPPED = os.path.join(ROOT, 'src', 'port', 'mods')
KEY = re.compile(r'mods: (?:wrote|had) .*?([0-9]+x[0-9]+-[0-9]+-[0-9a-f]+)\.png')
# The order the kinds of replacement are told in
KINDS = ['ui', 'english', 'art', 'fade', 'small', 'NONE']


def play(args):
    dump_args = argparse.Namespace(program=args.program, image=args.image, dump=os.path.join(args.out, 'dump'),
                                   modes=args.modes, logs=os.path.join(args.out, 'logs'))
    return port_art.dump(dump_args)


def tours(logs):
    """{tour: (keys it drew, its checkpoints reached, whether it failed)}."""
    out = {}
    for name in sorted(os.listdir(logs)):
        if not name.endswith('.log'):
            continue
        with open(os.path.join(logs, name), encoding='utf-8', errors='replace') as f:
            text = f.read()
        keys = set(KEY.findall(text))
        reached = [line.split()[1] for line in text.splitlines() if line.startswith('cp ')]
        out[name[:-4]] = (keys, reached, '(failure)' in reached)
    return out


def replacements(art):
    """{key: mod} of the built-in mods' pictures and the art's."""
    out = {}
    for mod in sorted(os.listdir(SHIPPED)):
        folder = os.path.join(SHIPPED, mod, 'textures')
        if os.path.isdir(folder):
            for name in os.listdir(folder):
                if name.endswith('.png'):
                    out.setdefault(name[:-4], mod)
    folder = os.path.join(art, 'textures')
    if os.path.isdir(folder):
        for name in os.listdir(folder):
            if name.endswith('.png'):
                out.setdefault(name[:-4], 'art')
    return out


def fade_of(name, rgba, covered):
    """Whether `rgba` is a step of a fade of a texture of `covered` ({name:
    rgba}, the textures with a replacement): its size and outline, its
    colours that one's times a number for each part."""
    import numpy as np

    size = name.split('-')[0] + '-' + name.split('-')[1]
    drawn = rgba[:, :, 3] > 0
    if not drawn.any():
        return False
    c = rgba[:, :, :3][drawn].astype(np.float64)
    for other, base in covered.get(size, []):
        if base.shape != rgba.shape or not np.array_equal(base[:, :, 3] > 0, drawn):
            continue
        b = base[:, :, :3][drawn].astype(np.float64)
        tint = (c.sum(0) + 1e-9) / np.maximum(b.sum(0), 1e-9)
        if tint.max() <= 1.03 and np.abs(b * tint - c).max() <= port_art.FADE_ERROR_MAX:
            return True
    return False


def report(args):
    import numpy as np
    from PIL import Image

    dump = os.path.join(args.out, 'dump')
    logs = os.path.join(args.out, 'logs')
    if not os.path.isdir(logs) or not os.path.isdir(dump):
        print('port_coverage: no %s: `play` first' % logs)
        return 1
    seen = tours(logs)
    have = replacements(args.art)
    textures = {}
    for keys, _, _ in seen.values():
        for key in keys:
            path = os.path.join(dump, key + '.png')
            if key not in textures and os.path.exists(path):
                textures[key] = np.asarray(Image.open(path).convert('RGBA'))
    covered = {}
    for key, rgba in textures.items():
        if key in have:
            covered.setdefault(key.split('-')[0] + '-' + key.split('-')[1], []).append((key, rgba))
    status = {}
    kinds = {}
    for key, rgba in sorted(textures.items()):
        h, w = rgba.shape[:2]
        if key in have:
            status[key] = have[key]
        elif w < port_art.SIDE_MIN or h < port_art.SIDE_MIN:
            status[key] = 'small'
        elif fade_of(key, rgba, covered):
            status[key] = 'fade'
        else:
            status[key] = 'NONE'
        if status[key] == 'art' and min(w, h) >= port_art.SIDE_MIN:
            kinds[key] = port_art.kind_of(key, rgba)
    lines = ['# What the mods replace of what the tours drew (tools/port_coverage.py)',
             '# art: %s' % os.path.relpath(args.art, ROOT).replace(os.sep, '/'), '']
    total = {k: 0 for k in KINDS}
    for key in textures:
        total[status[key] if status[key] in total else 'ui'] += 1
    lines.append('all tours: %d textures: %s' % (len(textures), ', '.join('%d %s' % (total[k], k) for k in KINDS)))
    lettering = sorted(k for k, v in kinds.items() if v == 'lettering')
    lines.append('lettering the art alone replaces: %d' % len(lettering))
    lines.append('')
    nothing = []
    for tour, (keys, reached, failed) in sorted(seen.items()):
        drawn = sorted(k for k in keys if k in textures)
        counts = {k: 0 for k in KINDS}
        for key in drawn:
            counts[status[key] if status[key] in counts else 'ui'] += 1
        mine = [k for k in drawn if status[k] == 'NONE']
        letters = [k for k in drawn if kinds.get(k) == 'lettering']
        lines.append('%s: %d textures: %s; lettering of the art alone %d; reached %s%s' % (
            tour, len(drawn), ', '.join('%d %s' % (counts[k], k) for k in KINDS if counts[k]), len(letters),
            ' '.join(reached), '' if not failed else ' (short of its end)'))
        for key in mine:
            lines.append('    NONE %s' % key)
            nothing.append(key)
        for key in letters:
            lines.append('    lettering %s' % key)
        if args.sheet and (mine or letters):
            sheet(os.path.join(args.out, 'sheet-%s.png' % tour), [(k, textures[k]) for k in mine + letters],
                  set(mine))
    with open(os.path.join(args.out, 'report.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('\n'.join(line for line in lines if not line.startswith('    ')))
    print('port_coverage: %s' % os.path.join(args.out, 'report.txt'))
    if args.check and nothing:
        print('port_coverage: %d textures have no replacement' % len(set(nothing)))
        return 1
    return 0


def sheet(path, items, marked):
    """The textures, four times larger, each with its key, those of
    `marked` framed in red."""
    from PIL import Image, ImageDraw

    scale = 4
    width = 1600
    x = y = row = 0
    placed = []
    for key, rgba in items:
        w, h = rgba.shape[1] * scale, rgba.shape[0] * scale + 12
        if x + w > width and x:
            x, y, row = 0, y + row + 4, 0
        placed.append((key, rgba, x, y))
        x += max(w, 8 * len(key) // 2) + 6
        row = max(row, h)
    image = Image.new('RGB', (width, y + row + 4), (60, 0, 60))
    draw = ImageDraw.Draw(image)
    for key, rgba, x, y in placed:
        picture = Image.fromarray(rgba, 'RGBA').resize((rgba.shape[1] * scale, rgba.shape[0] * scale), Image.NEAREST)
        image.paste(picture, (x, y + 12), picture)
        draw.text((x, y), key, fill=(255, 80, 80) if key in marked else (255, 255, 0))
    image.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('step', choices=['play', 'report'])
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--art', default=port_art.DEFAULT_OUT, help='the art mod made from the disc')
    ap.add_argument('--image', default=port_art.DEFAULT_IMAGE)
    ap.add_argument('--program', default=port_art.DEFAULT_PROGRAM)
    ap.add_argument('--modes', type=lambda s: s.split(','), help='only these tours (play)')
    ap.add_argument('--check', action='store_true', help='end 1 if a texture has no replacement')
    ap.add_argument('--sheet', action='store_true', help="a picture of each tour's textures of no replacement")
    args = ap.parse_args()
    if args.step == 'play':
        return play(args)
    return report(args)


if __name__ == '__main__':
    sys.exit(main())
