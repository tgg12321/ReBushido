#!/usr/bin/env python3
"""Builds the "art" mod of the native port from the player's own disc: every
2D texture of the game's screens -- the menus, the pause menu, the select
screens, the loading screens, the results, the captions and the HUD --
drawn again at a higher resolution, for the remastered picture
(docs/MODS.md, "The art mod").

    python3 tools/port_art.py dump  [--image CUE] [--dump tmp/art/dump]
                                    [--program build/port-gl/rebushido-gl]
    python  tools/port_art.py build [--dump tmp/art/dump ...] [--out tmp/mods/art]
                                    [--scale 6] [--keys KEY,...]
    python3 tools/port_art.py all   (both, where one Python has numpy)

`dump` plays the game with no window on the GL prover (tools/port_gl_build.py;
Linux, or WSL), from the boot through each mode of the top menu, its select
screens, a fight with its pause menu, the fight's end and the menus after it,
writing every texture the game draws (`--dump-textures`) into one folder. A
player who would rather play through the screens themselves runs the game with
`--dump-textures FOLDER` and gives that folder to `build`.

`build` makes each dumped texture `scale` times larger, by its kind:

  - a flat picture of a few colours (an emblem, a frame, a button): each
    colour's area is smoothed as a shape of its own and the colours put back
    by which area is strongest, at twice the scale, then averaged down, so
    every edge between two colours is a smooth line and no colour is mixed
    into a new one inside an area;
  - lettering and the like, up to 48 colours: the colours smoothed (bicubic,
    over colours multiplied by their alpha) and the outline of what is drawn
    made sharp again from the smoothed alpha, so a letter is a clean shape
    with an edge a pixel or two wide;
  - a picture of more colours (a portrait, a loading screen's map, the boot
    logo): Lanczos and an unsharp mask.

All three are written here, with numpy and Pillow; no model and no program of
anyone else's is used, so nothing is added to what the project's licence
covers. A texture whose side is under four texels is left as it is.

Fades: the port draws a texture the game fades through its colour table, or
a picture it fades by writing it again, from the replacement it has seen,
tinted (docs/MODS.md, "Fades"). A step of a fade drawn before the texture it
fades from has been seen -- a fade in from black -- is found here instead:
dumped textures of one size and one outline whose colours are the brightest
one's times a number for each part are written as that one's picture, tinted.

What is written is the game's own pictures made larger: it goes into a folder
git ignores (tmp/mods/art) and is never committed. Its manifest gives it the
order -200, under the built-in mods (english, ui: -100) and under any of the
player's own (0).
"""
import argparse
import os
import shutil
import subprocess
import sys

# numpy's BLAS on one kernel, whatever the PC: OpenBLAS picks one by the
# processor, and the kernels round alike only mostly, so the pages made on
# two PCs differed in a few texels (docs/MODS.md, "The same bytes on every
# PC"). Nehalem's runs on every processor numpy runs on.
os.environ['OPENBLAS_CORETYPE'] = 'Nehalem'

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DUMP = os.path.join(ROOT, 'tmp', 'art', 'dump')
DEFAULT_OUT = os.path.join(ROOT, 'tmp', 'mods', 'art')
DEFAULT_PROGRAM = os.path.join(ROOT, 'build', 'port-gl', 'rebushido-gl')
DEFAULT_IMAGE = os.path.join(ROOT, 'Bushido Blade 2 (USA).cue')

MANIFEST = '''# The game's 2D textures made larger, from your own disc, by
# tools/port_art.py (docs/MODS.md, "The art mod"). Never committed.
name = Remastered art
version = 1.0
order = -200      # under the built-in mods and under any of yours
author = ReBushido (tools/port_art.py)
'''

# ---- the tour: the plans `dump` plays ----

# The top menu's rows, from the top: each mode is chosen by going down to
# it. The slash mode's row is shut until it is won, so its index lands on
# the options' row; the options' row is 7.
MODES = ['story', 'vscpu', 'training', 'vs', 'group', 'link', 'setup']
MODE_ROWS = {'story': 0, 'vscpu': 1, 'training': 2, 'vs': 3, 'group': 4, 'link': 5, 'setup': 7}
TWO_PADS = ('vs', 'group')

PLAN = '''about = "tools/port_art.py: the screens of %(mode)s"
discs = ["port"]
cards = "none"
limit = %(limit)d
input = [
%(presses)s
]
[[checkpoint]]
name = "page"
when = "rb_page == RB_PAGE_UP"
within = 2000
shot = false
[[checkpoint]]
name = "movie"
when = "screen == SCREEN_MOVIE"
within = 1000
shot = false
[[checkpoint]]
name = "menu"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_TOP && screen_frames >= 120"
within = 3000
shot = false
[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = %(limit)d
shot = false
[[checkpoint]]
name = "end"
after = %(end)d
shot = false
'''


def plan_text(mode, limit=9000):
    """A plan that goes from the boot to the mode's row, takes it, presses
    Cross through its select screens, walks in and pauses the fight, fights
    it to its end and presses on through the menus after it."""
    row = MODE_ROWS[mode]
    presses = ['page+30 START', 'movie+300 START', 'movie+600 CROSS', 'movie+900 START']
    presses += ['menu+%d DOWN' % (30 + 30 * i) for i in range(row)]
    presses.append('menu+%d CROSS' % (60 + 30 * row))
    for t in range(300, 2600, 90):
        presses.append('menu+%d CROSS' % t)
        if mode in TWO_PADS:
            presses.append('menu+%d P2 CROSS' % (t + 20))
    presses += ['fight+30f RIGHT hold 20f', 'fight+80f START', 'fight+90 START']
    presses += ['fight+%df %s' % (f, 'CIRCLE' if f % 60 else 'CROSS') for f in range(120, 1500, 30)]
    presses += ['fight+%df CROSS' % f for f in range(1600, 3600, 70)]
    return PLAN % {'mode': mode, 'limit': limit, 'end': limit - 1300,
                   'presses': ',\n'.join('    "%s"' % p for p in presses)}


def dump(args):
    """Plays the tour on the GL prover, every mode at once, into one folder."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import playtest

    program = os.path.abspath(args.program)
    if not os.path.exists(program):
        print('port_art: no %s: python3 tools/port_gl_build.py builds it' % program)
        return 1
    if not os.path.exists(args.image):
        print('port_art: no disc image %s (--image)' % args.image)
        return 1
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    os.makedirs(args.dump, exist_ok=True)
    import tempfile  # here: the tests' own random.py is not the one tempfile needs

    work = tempfile.mkdtemp(prefix='port_art_')
    empty = os.path.join(work, 'no-mods')
    os.makedirs(empty)
    runs = []
    names = playtest.header_names()
    for mode in MODES:
        toml = os.path.join(work, mode + '.toml')
        with open(toml, 'w', encoding='utf-8', newline='\n') as f:
            f.write(plan_text(mode))
        test = playtest.Test(toml)
        _, lines = test.plan(playtest.Disc('port'), names, 1)
        plan = os.path.join(work, mode + '.plan')
        with open(plan, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        out = os.path.join(work, mode)
        os.makedirs(out)
        log = open(os.path.join(work, mode + '.log'), 'w')
        # with no mods, so that every texture is written out, the built-in
        # mods' own among them
        cmd = [program, '--image', args.image, '--plan', plan, '--out', out, '--graphics', 'remastered',
               '--gl-scale', '1', '--mods', empty, '--dump-textures', args.dump]
        runs.append((mode, subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT), log))
    for mode, process, log in runs:
        process.wait()
        log.close()
        print('port_art: %s played' % mode)
    shutil.rmtree(work)
    count = len([n for n in os.listdir(args.dump) if n.endswith('.png')])
    print('port_art: %d textures in %s' % (count, args.dump))
    return 0 if count else 1


# ---- making them larger ----

def upscalers():
    import numpy as np
    from PIL import Image, ImageFilter

    def resize(arr, w, h, how):
        return np.dstack([np.asarray(Image.fromarray(arr[:, :, c].astype(np.float32), mode='F').resize((w, h), how))
                          for c in range(arr.shape[2])])

    def padded_resize(arr, s, how, pad=2):
        h, w = arr.shape[:2]
        p = np.pad(arr, ((pad, pad), (pad, pad), (0, 0)), mode='edge')
        big = resize(p, (w + 2 * pad) * s, (h + 2 * pad) * s, how)
        return big[pad * s:-pad * s, pad * s:-pad * s]

    def unpremultiply(pm, alpha):
        return np.clip(pm / np.maximum(alpha, 1e-3), 0, 255)

    def shapes(rgba, s):
        """Each colour's area smoothed as a shape; the strongest wins."""
        h, w = rgba.shape[:2]
        flat = rgba.reshape(-1, 4).astype(np.int64)
        flat[flat[:, 3] == 0] = 0
        keys = flat[:, 0] | (flat[:, 1] << 8) | (flat[:, 2] << 16) | (flat[:, 3] << 24)
        colors, which = np.unique(keys, return_inverse=True)
        which = which.reshape(h, w)
        best = label = None
        for i in range(len(colors)):
            area = padded_resize((which == i).astype(np.float32)[..., None], 2 * s, Image.BICUBIC)[:, :, 0]
            if best is None:
                best, label = area, np.zeros(area.shape, np.int64)
            else:
                stronger = area > best
                best[stronger] = area[stronger]
                label[stronger] = i
        table = np.stack([colors & 255, (colors >> 8) & 255, (colors >> 16) & 255, (colors >> 24) & 255],
                         1).astype(np.float64)
        big = table[label]
        alpha = big[:, :, 3:4] / 255.0
        small = resize(np.dstack([big[:, :, :3] * alpha, alpha]), w * s, h * s, Image.BOX)
        return np.dstack([unpremultiply(small[:, :, :3], small[:, :, 3:4]), small[:, :, 3:4] * 255])

    def lettering(rgba, s):
        """The colours smoothed, the outline sharp again."""
        alpha = rgba[:, :, 3:4] / 255.0
        pm = padded_resize(rgba[:, :, :3] * alpha, s, Image.BICUBIC)
        big_alpha = padded_resize(alpha, s, Image.BICUBIC)
        edge = np.clip((big_alpha - 0.5) * (s / 1.6) + 0.5, 0, 1)
        return np.dstack([unpremultiply(pm, big_alpha), edge * 255])

    def picture(rgba, s):
        """Lanczos, and an unsharp mask."""
        alpha = rgba[:, :, 3:4] / 255.0
        pm = padded_resize(rgba[:, :, :3] * alpha, s, Image.LANCZOS)
        big_alpha = np.clip(padded_resize(alpha, s, Image.LANCZOS), 0, 1)
        rgb = unpremultiply(pm, big_alpha).astype(np.uint8)
        rgb = np.asarray(Image.fromarray(rgb, 'RGB').filter(
            ImageFilter.UnsharpMask(radius=s * 0.6, percent=60, threshold=2))).astype(np.float64)
        edge = np.clip((big_alpha - 0.5) * (s / 1.6) + 0.5, 0, 1) if alpha.min() < 1 else big_alpha
        return np.dstack([rgb, edge * 255])

    return {'shapes': shapes, 'lettering': lettering, 'picture': picture}


SHAPES_COLORS_MAX = 6
LETTERING_COLORS_MAX = 48
SIDE_MIN = 4
# what a fade among the dumped textures has to be (fades())
FADE_TEXELS_MIN = 256
FADE_BRIGHTNESS_MIN = 40
FADE_ERROR_MAX = 10


def kind_of(name, rgba):
    import numpy as np

    opaque = rgba[rgba[:, :, 3] > 0]
    colors = len(np.unique(opaque.reshape(-1, 4), axis=0)) if len(opaque) else 0
    if name.split('-')[1] == '16' or colors > LETTERING_COLORS_MAX:
        return 'picture'
    if colors <= SHAPES_COLORS_MAX:
        return 'shapes'
    return 'lettering'


def fades(textures):
    """The fades among the dumped textures: {step: (base, tint)}, a step
    being a texture of the base's size and outline whose colours are the
    base's times a number for each part."""
    import numpy as np

    groups = {}
    for name, rgba in textures.items():
        size = name.split('-')[0] + '-' + name.split('-')[1]
        groups.setdefault((size, (rgba[:, :, 3] > 0).tobytes()), []).append(name)
    found = {}
    for names in groups.values():
        if len(names) < 2:
            continue
        names.sort(key=lambda n: -float(textures[n][:, :, :3].sum()))
        base = textures[names[0]]
        drawn = base[:, :, 3] > 0
        b = base[:, :, :3][drawn].astype(np.float64)
        if not len(b):
            continue
        for name in names[1:]:
            c = textures[name][:, :, :3][drawn].astype(np.float64)
            tint = (c.sum(0) + 1e-9) / np.maximum(b.sum(0), 1e-9)
            # a fade to black or from it, of a texture large and bright
            # enough to tell: a small or dark one is like too many others
            if (tint.max() > 0.97 or tint.max() < 0.02 or tint.max() - tint.min() > 0.06 or len(b) < FADE_TEXELS_MIN
                    or b.mean() < FADE_BRIGHTNESS_MIN or np.abs(b * tint - c).max() > FADE_ERROR_MAX):
                continue
            found[name] = (names[0], tint)
    return found


def build(args):
    import numpy as np
    from PIL import Image

    ways = upscalers()
    textures = {}
    for folder in args.dump:
        if not os.path.isdir(folder):
            print('port_art: no folder %s: `dump` first, or --dump FOLDER' % folder)
            return 1
        for entry in sorted(os.listdir(folder)):
            if entry.endswith('.png') and entry[:-4] not in textures:
                textures[entry[:-4]] = np.asarray(Image.open(os.path.join(folder, entry)).convert('RGBA'))
    if args.keys:
        textures = {k: v for k, v in textures.items() if k in args.keys.split(',')}
    out = os.path.join(args.out, 'textures')
    os.makedirs(out, exist_ok=True)
    # what an earlier build wrote goes: the mod is this build's alone
    for entry in os.listdir(out):
        if entry.endswith('.png'):
            os.remove(os.path.join(out, entry))
    counts = {}
    made = {}
    for name, rgba in sorted(textures.items()):
        h, w = rgba.shape[:2]
        if w < SIDE_MIN or h < SIDE_MIN:
            counts['left as it is'] = counts.get('left as it is', 0) + 1
            continue
        kind = kind_of(name, rgba)
        big = ways[kind](rgba.astype(np.float64), args.scale)
        image = Image.fromarray(np.clip(big + 0.5, 0, 255).astype(np.uint8), 'RGBA')
        made[name] = image
        image.save(os.path.join(out, name + '.png'), optimize=False, compress_level=6)
        counts[kind] = counts.get(kind, 0) + 1
    steps = 0
    for name, (base, tint) in fades(textures).items():
        if base not in made:
            continue
        rgba = np.asarray(made[base]).astype(np.float64)
        rgba[:, :, :3] *= tint
        Image.fromarray(np.clip(rgba + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(
            os.path.join(out, name + '.png'), compress_level=6)
        steps += 1
    with open(os.path.join(args.out, 'mod.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(MANIFEST)
    print('port_art: %d textures into %s: %s; %d steps of fades' % (
        len(made) + steps, args.out, ', '.join('%d %s' % (v, k) for k, v in sorted(counts.items())), steps))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('step', nargs='?', default='all', choices=['dump', 'build', 'all'])
    ap.add_argument('--image', default=DEFAULT_IMAGE)
    ap.add_argument('--program', default=DEFAULT_PROGRAM)
    ap.add_argument('--dump', action='append', help='the folder of dumped textures (more than one for build)')
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--scale', type=int, default=6)
    ap.add_argument('--keys', help='only these keys (build)')
    args = ap.parse_args()
    args.dump = args.dump or [DEFAULT_DUMP]
    if args.step in ('dump', 'all'):
        dump_args = argparse.Namespace(**vars(args))
        dump_args.dump = args.dump[0]
        if dump(dump_args):
            return 1
    if args.step in ('build', 'all'):
        return build(args)
    return 0


if __name__ == '__main__':
    sys.exit(main())
