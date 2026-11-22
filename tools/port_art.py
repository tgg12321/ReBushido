#!/usr/bin/env python3
"""Builds the "art" mod of the native port from the player's own disc: every
2D texture of the game's screens -- the menus, the pause menu, the select
screens, the loading screens, the results, the captions and the HUD --
drawn again at a higher resolution, for the remastered picture
(docs/MODS.md, "The art mod").

    python3 tools/port_art.py dump  [--image CUE] [--dump tmp/art/dump]
                                    [--program build/port-gl/rebushido-gl]
                                    [--modes a,b] [--logs FOLDER]
    python  tools/port_art.py build [--dump tmp/art/dump ...] [--out tmp/mods/art]
                                    [--scale 6] [--keys KEY,...] [--upscaler FOLDER|off]
    python3 tools/port_art.py all   (both, where one Python has numpy)

`dump` plays the game with no window on the GL prover (tools/port_gl_build.py;
Linux, or WSL), from the boot through each mode of the top menu, its select
screens, a fight with its pause menu, the fight's end and the menus after it,
versus mode's stage pages walked across, all three, the options' pages, the
slash mode won and the ten endings (MODES; --modes for some), writing every texture
the game draws (`--dump-textures`) into one folder, and where each was in
video memory into its where.txt. A player who would rather play through the
screens themselves runs the game with `--dump-textures FOLDER` and gives that
folder to `build` (with no where.txt, each picture's tiles are made larger
apart).

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
  - a painting, up to 48 colours that cover the texture and are not one ramp
    of one hue (a background's tile, the stage select's map, a panel), and a
    picture of more colours (a portrait, a loading screen's map, the boot
    logo): Real-ESRGAN's x4plus model, through realesrgan-ncnn-vulkan on the
    PC's graphics card, four times larger and then Lanczos to the scale. The
    tiles of one picture, which lie side by side in a page of video memory
    with one colour table and go on from each other, are put back together
    first (where.txt) and made larger whole, so no tile has an edge of its
    own. The outline is today's way's, and so are the darkest colours.

    Where there is no upscaler (--upscaler off, or none found) or it cannot
    run (no Vulkan), a painting is made as lettering and a picture by
    Lanczos and an unsharp mask, as before the model: those are written here,
    with numpy and Pillow, and are the same bytes on every PC. The model's
    are the same on one graphics card every time, and within a few steps of
    255 on another. A texture whose side is under four texels is left as it
    is.

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
import time

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
MADE_BY = '''# its pictures and paintings made larger by %s
'''

# ---- the tour: the plans `dump` plays ----

# The tours, each a run of its own: each mode of the top menu with its
# select screens and a fight; `stages`, versus mode's stage pages walked
# across; `setup`, the options and their pages; `slash`, the slash mode
# (shut until it is won: opened by --unlock-all) to its end; and the
# endings, `ending-N`, each begun from a fight (below, ENDINGS).
MODES = ['story', 'vscpu', 'training', 'vs', 'group', 'link', 'setup', 'stages', 'slash',
         'ending-0', 'ending-1', 'ending-2', 'ending-3', 'ending-4', 'ending-5', 'ending-6', 'ending-7']
# The top menu's rows, from the top: each mode is chosen by going down to
# it. With the slash mode shut (an empty save) the options' row is the
# seventh, 6; with it open (`slash`, --unlock-all) the slash mode is 6.
MODE_ROWS = {'story': 0, 'vscpu': 1, 'training': 2, 'vs': 3, 'group': 4, 'link': 5, 'slash': 6}
OPTIONS_ROW = 6
TWO_PADS = ('vs', 'group')
# What each tour's run is given besides the plan: the slash mode open
UNLOCK = ['--unlock-all']
# A fight straight from the start (docs/PORT.md, "Straight into a fight"),
# for the endings
ENDING_FIGHT = ['--fight', 'tatara-shrine', 'kannuki', 'mikado']
# runs at once
DUMP_JOBS = 8
# `stages`: versus mode's select screens pressed through as
# tools/port_stage_select.py presses them, then the stage pages walked
# across, left to the first and right to the third, so that every page's
# map, names and title are drawn (the other tours take the page they open
# on), then a fight and its loading screen
STAGES_PRESSES = [
    'menu+30 DOWN', 'menu+60 DOWN', 'menu+90 DOWN', 'menu+150 CROSS',
    'select+280 CROSS', 'select+320 P2 CROSS', 'select+500 CROSS', 'select+540 P2 CROSS',
    'stages+60 LEFT', 'stages+120 LEFT', 'stages+180 LEFT', 'stages+240 LEFT',
    'stages+300 DOWN', 'stages+360 DOWN', 'stages+420 RIGHT',
] + ['stages+%d RIGHT' % t for t in range(480, 1200, 60)] + ['stages+1260 UP', 'stages+1320 CROSS']
STAGES_CHECKPOINTS = '''[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 3000
shot = false
[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 3000
shot = false
'''

PLAN = '''about = "tools/port_art.py: the screens of %(mode)s"
discs = ["port"]
cards = "none"
limit = %(limit)d
input = [
%(presses)s
]
%(state)s[[checkpoint]]
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
%(between)s[[checkpoint]]
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


# `setup`: the options page (its six rows, each choice both ways and back:
# the monitor's B&W shows the page in grey), the memory card's page (its
# load and save shut with no card), and the key configuration, both pads'
# cursors walked down it, an action given no button (N/A) and back
SETUP_PRESSES = (['menu+%d DOWN' % (30 + 30 * i) for i in range(OPTIONS_ROW)] + [
    'menu+240 CROSS',
    'menu+330 RIGHT', 'menu+400 RIGHT', 'menu+460 DOWN', 'menu+520 RIGHT', 'menu+580 RIGHT',
    'menu+640 DOWN', 'menu+700 RIGHT', 'menu+760 RIGHT', 'menu+820 DOWN', 'menu+880 CROSS',
    'menu+960 UP', 'menu+1020 CROSS', 'menu+1300 CROSS', 'menu+1400 UP', 'menu+1460 CROSS', 'menu+1740 CROSS',
    'menu+1840 TRIANGLE', 'menu+1940 DOWN', 'menu+2000 CROSS',
    'menu+2100 DOWN', 'menu+2160 DOWN', 'menu+2220 DOWN', 'menu+2280 CROSS', 'menu+2340 DOWN', 'menu+2400 CROSS',
    'menu+2460 P2 DOWN', 'menu+2520 P2 DOWN', 'menu+2580 P2 DOWN', 'menu+2640 P2 DOWN',
    'menu+2760 DOWN', 'menu+2820 CROSS', 'menu+2900 CROSS', 'menu+3000 CROSS', 'menu+3100 DOWN', 'menu+3160 CROSS',
    'menu+3300 CROSS', 'menu+3400 CROSS'])
SETUP_PLAN = '''about = "tools/port_art.py: the screens of setup"
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
name = "end"
after = 3600
shot = false
'''

# The endings (fight.h, enum Ending): ten, of nine pictures, that only a
# story, a full run against the CPU, a group battle or the slash mode won
# reaches. Each `ending-N` begins a fight straight from the start and
# writes the ending into the game's memory as it begins (`poke`: the
# ending's number, then the screen, SCREEN_ENDING_INIT, as the game's own
# step does where a run is won): from there it is the game's own way on,
# the ending's picture, its scene, and after it the screen that follows
# that ending. `ending-0` goes on through the staff roll to its end and the
# save's question; `ending-6` and `ending-7`, a slash mode won fast, are
# told the run had no continue as well, so the game goes on to the other
# ending of that side, 8 and 9, as it does.
ENDINGS = {'ending-%d' % n: n for n in range(8)}
SCREEN_ENDING_INIT = 18
ENDING_PLAN = '''about = "tools/port_art.py: the screens of %(mode)s"
discs = ["port"]
cards = "none"
limit = %(limit)d
input = [
%(presses)s
]
[state]
ending = "g_fight_ending:u8"
clean_unlock = "g_chanbara_clean_unlock:u8"
chanbara_count = "g_chanbara_count:u8"
[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 2000
shot = false
[[checkpoint]]
name = "ending"
when = "screen == SCREEN_ENDING_UPDATE"
within = 2000
shot = false
%(staff)s[[checkpoint]]
name = "end"
after = %(after)d
shot = false
'''
STAFF_CHECKPOINT = '''[[checkpoint]]
name = "staff"
when = "screen == SCREEN_STAFF_ROLL_UPDATE"
within = 2000
shot = false
'''
# the staff roll's 2,880 frames, at a frame a blank, and the save's question
STAFF_BLANKS = 3300
# `slash`: the hundred opponents' count written to the last (`poke`), and
# the opponent then struck down, so that the run is won as it begins: the
# replay, the best times with the run's time among them, the endings of
# a run won fast and with no continue, and the save's question
SLASH_STATE = '''[state]
chanbara_count = "g_chanbara_count:u8"
ending = "g_fight_ending:u8"
'''
# What follows the fight, each screen pressed on from a checkpoint of its
# own, so that a load that takes another number of frames on another
# program (the window's, the GL prover's) moves nothing: the replay
# skipped, the best times read, the two endings, the save's question
SLASH_AFTER = '''[[checkpoint]]
name = "replay"
when = "screen == SCREEN_REPLAY_UPDATE"
within = 2000
shot = false
[[checkpoint]]
name = "times"
when = "screen == SCREEN_BEST_TIMES_UPDATE"
within = 2000
shot = false
[[checkpoint]]
name = "fast"
when = "screen == SCREEN_ENDING_UPDATE"
within = 2000
shot = false
[[checkpoint]]
name = "clean"
when = "screen == SCREEN_ENDING_UPDATE && ending >= 8"
within = 2000
shot = false
[[checkpoint]]
name = "save"
when = "screen == SCREEN_SAVE_UPDATE"
within = 2000
shot = false
[[checkpoint]]
name = "end"
after = 300
shot = false
'''
SLASH_PRESSES = ['replay+60 CROSS', 'times+240 CROSS', 'fast+300 CROSS', 'clean+300 CROSS', 'save+120 CROSS']
SLASH_POKES = [('fight', 60, 1, 'chanbara_count', 100), ('fight', 120, 1, 'p2_defeated', 1)]


def tour_args(mode):
    """What the tour's run is given besides its plan."""
    if mode == 'slash':
        return list(UNLOCK)
    if mode in ENDINGS:
        return list(ENDING_FIGHT)
    return []


def tour_pokes(mode):
    """The tour's pokes: [(checkpoint, blanks after it, blanks held, value's
    name, value)]."""
    if mode == 'slash':
        return list(SLASH_POKES)
    if mode in ENDINGS:
        n = ENDINGS[mode]
        pokes = [('fight', 60, 1, 'ending', n)]
        if n in (6, 7):
            pokes.append(('fight', 60, 1, 'clean_unlock', 1))
        return pokes + [('fight', 60, 1, 'screen', SCREEN_ENDING_INIT)]
    return []


def plan_lines(mode, playtest, names, folder, limit=9000):
    """The tour's plan as the program reads it (its lines), with its pokes;
    `folder` to write the plan's TOML into for a moment."""
    toml = os.path.join(folder, mode + '.toml')
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write(plan_text(mode, limit))
    _, lines = playtest.Test(toml).plan(playtest.Disc('port'), names, 1)
    os.remove(toml)
    values = {}
    for line in lines:
        words = line.split()
        if words and words[0] == 'var':
            values[words[1]] = (int(words[2]), int(words[3]))
    checkpoints = [line.split()[1] for line in lines if line.startswith('cp ')]
    for checkpoint, after, hold, name, value in tour_pokes(mode):
        address, size = values[name]
        lines.append('poke %d %d %d %d %d %d' % (checkpoints.index(checkpoint) + 1, after, hold, address, size, value))
    return lines


def plan_text(mode, limit=9000):
    """A plan that goes from the boot to the mode's row, takes it, presses
    Cross through its select screens, walks in and pauses the fight, fights
    it to its end and presses on through the menus after it; or the
    options' pages walked (`setup`), or an ending begun from a fight."""
    if mode == 'setup':
        presses = ['page+30 START', 'movie+300 START', 'movie+600 CROSS', 'movie+900 START'] + SETUP_PRESSES
        return SETUP_PLAN % {'limit': limit, 'presses': ',\n'.join('    "%s"' % p for p in presses)}
    if mode == 'slash':
        # the top menu's row and the select screens as the other modes',
        # then the fight won as it begins (SLASH_POKES) and the screens
        # after it each from its own checkpoint
        row = MODE_ROWS[mode]
        presses = ['page+30 START', 'movie+300 START', 'movie+600 CROSS', 'movie+900 START']
        presses += ['menu+%d DOWN' % (30 + 30 * i) for i in range(row)]
        presses.append('menu+%d CROSS' % (60 + 30 * row))
        presses += ['menu+%d CROSS' % t for t in range(300, 1500, 90)]
        presses += ['fight+30f RIGHT hold 20f'] + SLASH_PRESSES
        text = PLAN % {'mode': mode, 'limit': limit, 'end': 0, 'between': '', 'state': SLASH_STATE,
                       'presses': ',\n'.join('    "%s"' % p for p in presses)}
        # the fight's checkpoint, then the screens after it in place of the
        # fight's end
        return text[:text.index('[[checkpoint]]\nname = "end"')] + SLASH_AFTER
    if mode in ENDINGS:
        n = ENDINGS[mode]
        presses = ['ending+%d CROSS' % t for t in range(400, 1700, 300)]
        staff = ''
        after = 1800
        if n == 0:
            # the staff roll to its end, then the save's question
            presses = ['ending+400 CROSS', 'staff+%d CROSS' % (STAFF_BLANKS - 100), 'staff+%d CROSS' % STAFF_BLANKS]
            staff = STAFF_CHECKPOINT
            after = STAFF_BLANKS + 300
        elif n == 1:
            presses = ['ending+400 CROSS', 'staff+60 CROSS', 'staff+300 CROSS']
            staff = STAFF_CHECKPOINT
            after = 600
        return ENDING_PLAN % {'mode': mode, 'limit': limit, 'staff': staff, 'after': after,
                              'presses': ',\n'.join('    "%s"' % p for p in presses)}
    presses = ['page+30 START', 'movie+300 START', 'movie+600 CROSS', 'movie+900 START']
    between = ''
    if mode == 'stages':
        presses += STAGES_PRESSES
        between = STAGES_CHECKPOINTS
    else:
        row = MODE_ROWS[mode]
        presses += ['menu+%d DOWN' % (30 + 30 * i) for i in range(row)]
        presses.append('menu+%d CROSS' % (60 + 30 * row))
        for t in range(300, 2600, 90):
            presses.append('menu+%d CROSS' % t)
            if mode in TWO_PADS:
                presses.append('menu+%d P2 CROSS' % (t + 20))
    presses += ['fight+30f RIGHT hold 20f', 'fight+80f START', 'fight+90 START']
    presses += ['fight+%df %s' % (f, 'CIRCLE' if f % 60 else 'CROSS') for f in range(120, 1500, 30)]
    presses += ['fight+%df CROSS' % f for f in range(1600, 3600, 70)]
    return PLAN % {'mode': mode, 'limit': limit, 'end': limit - 1300, 'between': between, 'state': '',
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
    names = playtest.header_names()
    modes = getattr(args, 'modes', None) or MODES
    logs = getattr(args, 'logs', None)
    waiting = list(modes)
    running = []
    lines = set()
    while waiting or running:
        while waiting and len(running) < DUMP_JOBS:
            mode = waiting.pop(0)
            plan = os.path.join(work, mode + '.plan')
            with open(plan, 'w', encoding='utf-8', newline='\n') as f:
                f.write('\n'.join(plan_lines(mode, playtest, names, work)) + '\n')
            out = os.path.join(work, mode)
            os.makedirs(out)
            log = open(os.path.join(work, mode + '.log'), 'w')
            # with no mods, so that every texture is written out, the
            # built-in mods' own among them
            cmd = [program, '--image', args.image, '--plan', plan, '--out', out, '--graphics', 'remastered',
                   '--gl-scale', '1', '--mods', empty, '--dump-textures', args.dump] + tour_args(mode)
            running.append((mode, subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT), log))
        mode, process, log = running.pop(0)
        process.wait()
        log.close()
        with open(os.path.join(work, mode + '.log'), encoding='utf-8', errors='replace') as f:
            text = f.read()
        lines |= where_lines(text)
        if logs:
            os.makedirs(logs, exist_ok=True)
            with open(os.path.join(logs, mode + '.log'), 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
        print('port_art: %s played' % mode)
    add_where(args.dump, lines)
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
# how much brighter than its base a tint's part may be measured
FADE_TINT_SLACK = 0.03


# a painting: a texture of up to 48 colours that covers its rectangle (the
# backgrounds' tiles, the stage select's map, the loading screen's, the
# select screens' panels and emblems), which lettering never does
PAINTING_COVER_MIN = 0.9
PAINTING_COLORS_MIN = 8
PAINTING_SIDE_MIN = 16
# ... and whose colours are not one ramp of one hue: a soft shape of a few
# steps from dark to light (the top menu's moon, the flares of the fight)
# has the model make a hard edge of its soft one and is kept to today's
# way. The spread of the colours across their main line over the spread
# along it: the moon is 0.025, the flares 0; the loading screen's map of
# ink on paper, of one hue too, 0.039
PAINTING_SPREAD_MIN = 0.03

# ---- the learned upscaler ----

# Real-ESRGAN (github.com/xinntao/Real-ESRGAN, BSD-3-Clause, the weights
# with it) through realesrgan-ncnn-vulkan (github.com/xinntao/
# Real-ESRGAN-ncnn-vulkan, MIT; Tencent's ncnn, BSD-3-Clause): a program of
# its own on any Vulkan graphics card, run with paths as its arguments
# (docs/MODS.md, "The art mod"). x4plus for both kinds: the anime model and
# realesr-animevideov3 were tried and flatten the clouds, the maps and the
# landscapes into shapes of one colour.
UPSCALER = 'realesrgan-ncnn-vulkan'
MODEL_OF = {'picture': 'realesrgan-x4plus', 'painting': 'realesrgan-x4plus'}
# what a kind is made by when there is no model: today's ways, so that the
# mod without the model is the bytes it always was
FALLBACK_OF = {'picture': 'picture', 'painting': 'lettering'}
MODEL_SCALE = 4
# texels of each texture's edge repeated round it, so that the model sees
# no edge where a tile of a larger picture meets the next
MODEL_PAD = 8
# a card too slow for the model (or none, under a software Vulkan) is given
# up on and the mod made without it
MODEL_SECONDS = 600
CREATE_NO_WINDOW = 0x08000000
# two tiles are put together where the step across their join is no more
# than this many times the steps beside it, and this much
JOIN_STEP_TIMES = 2.0
JOIN_STEP_MIN = 6.0
# where today's way leaves a texel this dark or darker, its colour is
# today's, and the model's is mixed in above it: the model lifts black a
# step or two, which a texture the game draws adding to what is under it
# (the top menu's moon) shows as a box round it
DARK_LEVEL = 24.0


def kind_of(name, rgba):
    import numpy as np

    drawn = rgba[:, :, 3] > 0
    opaque = rgba[drawn]
    colors = len(np.unique(opaque.reshape(-1, 4), axis=0)) if len(opaque) else 0
    if name.split('-')[1] == '16' or colors > LETTERING_COLORS_MAX:
        return 'picture'
    if colors <= SHAPES_COLORS_MAX:
        return 'shapes'
    if (colors >= PAINTING_COLORS_MIN and min(rgba.shape[:2]) >= PAINTING_SIDE_MIN
            and drawn.mean() >= PAINTING_COVER_MIN and spread(opaque) >= PAINTING_SPREAD_MIN):
        return 'painting'
    return 'lettering'


def spread(texels):
    """How far the colours of `texels` (n by 4) stray from the line they
    lie along most: the second spread over the first, 0 for colours of
    one ramp."""
    import numpy as np

    rgb = texels[:, :3].astype(np.float64)
    rgb = rgb - rgb.mean(0)
    ev = np.sort(np.clip(np.linalg.eigvalsh(rgb.T @ rgb / max(len(rgb), 1)), 0, None))[::-1]
    return float(np.sqrt(ev[1] / ev[0])) if ev[0] > 0 else 0.0


def checkouts():
    """This checkout, and the main one where this is a worktree of it (its
    .git a file that names the main one's .git/worktrees/NAME)."""
    out = [ROOT]
    dot_git = os.path.join(ROOT, '.git')
    if os.path.isfile(dot_git):
        with open(dot_git, encoding='utf-8') as f:
            line = f.read().strip()
        if line.startswith('gitdir:'):
            gitdir = line[len('gitdir:'):].strip()
            if os.name != 'nt' and len(gitdir) > 2 and gitdir[1] == ':':
                # a worktree made on Windows, read in WSL
                gitdir = '/mnt/%s/%s' % (gitdir[0].lower(), gitdir[3:].replace('\\', '/'))
            out.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(gitdir)))))
    return out


def find_upscaler(given):
    """The folder of realesrgan-ncnn-vulkan and its models/: `given`, or
    RB_UPSCALER, or the package's prepare/upscaler/ beside these tools, or
    tmp/art-tools/ of the checkout. None where there is none, or `off`."""
    if given == 'off':
        return None
    exe = UPSCALER + ('.exe' if os.name == 'nt' else '')
    places = [given] if given and given != 'auto' else [
        os.environ.get('RB_UPSCALER'),
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'upscaler')] + [
        os.path.join(top, 'tmp', 'art-tools', 'realesrgan-ncnn-vulkan-20220424') for top in checkouts()]
    for place in places:
        if place and os.path.isfile(os.path.join(place, exe)) and all(
                os.path.isfile(os.path.join(place, 'models', m + '.bin')) for m in MODEL_OF.values()):
            return os.path.abspath(place)
    if given and given != 'auto':
        print('port_art: no %s with its models in %s' % (exe, given))
    return None


def fill_under(rgba):
    """The colours of a texture with each texel it does not draw given its
    nearest drawn neighbour's, so the model sees no black round a shape."""
    import numpy as np

    rgb = rgba[:, :, :3].astype(np.float64)
    have = rgba[:, :, 3] > 0
    if have.all() or not have.any():
        return rgb
    while not have.all():
        total = np.zeros_like(rgb)
        count = np.zeros(have.shape)
        padded_rgb = np.pad(rgb, ((1, 1), (1, 1), (0, 0)))
        padded = np.pad(have, 1)
        h, w = have.shape
        for dy, dx in ((0, 1), (2, 1), (1, 0), (1, 2)):
            near = padded[dy:dy + h, dx:dx + w]
            total += padded_rgb[dy:dy + h, dx:dx + w] * near[..., None]
            count += near
        new = ~have & (count > 0)
        rgb[new] = total[new] / count[new][:, None]
        have = have | new
    return rgb


def upscale_by_model(upscaler, textures, scale, work=None):
    """{name: RGB array `scale` times larger} of {name: (rgba, model)}, by
    the model on the PC's graphics card; (made, why) where `why` says what
    failed, and the textures it was are not in `made`. `work` is a folder
    of its own to work in (a new one in the system's if not said), removed
    after."""
    import numpy as np
    from PIL import Image

    made = {}
    if work is None:
        import tempfile  # here: the tests' own random.py is not the one tempfile needs

        work = tempfile.mkdtemp(prefix='port_art_model_')
    os.makedirs(work, exist_ok=True)
    why = None
    exe = os.path.join(upscaler, UPSCALER + ('.exe' if os.name == 'nt' else ''))
    try:
        for model in sorted(set(m for _, m in textures.values())):
            names = sorted(n for n, (_, m) in textures.items() if m == model)
            src = os.path.join(work, model + '-in')
            out = os.path.join(work, model + '-out')
            os.makedirs(src)
            os.makedirs(out)
            for name in names:
                rgb = np.pad(fill_under(textures[name][0]), ((MODEL_PAD, MODEL_PAD), (MODEL_PAD, MODEL_PAD), (0, 0)),
                             mode='edge')
                Image.fromarray(np.clip(rgb + 0.5, 0, 255).astype(np.uint8), 'RGB').save(
                    os.path.join(src, name + '.png'))
            cmd = [exe, '-i', src, '-o', out, '-n', model, '-s', str(MODEL_SCALE), '-m',
                   os.path.join(upscaler, 'models'), '-f', 'png']
            try:
                proc = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=MODEL_SECONDS,
                                      creationflags=CREATE_NO_WINDOW if os.name == 'nt' else 0)
            except (OSError, subprocess.TimeoutExpired) as e:
                why = '%s: %s' % (model, e)
                continue
            said = (proc.stdout + proc.stderr).strip().splitlines()
            for name in names:
                h, w = textures[name][0].shape[:2]
                path = os.path.join(out, name + '.png')
                big = Image.open(path).convert('RGB') if os.path.exists(path) else None
                if big is None or big.size != ((w + 2 * MODEL_PAD) * MODEL_SCALE, (h + 2 * MODEL_PAD) * MODEL_SCALE):
                    why = '%s: %s' % (model, said[-1] if said else 'no picture written (exit %d)' % proc.returncode)
                    break
                big = big.crop((MODEL_PAD * MODEL_SCALE, MODEL_PAD * MODEL_SCALE, (MODEL_PAD + w) * MODEL_SCALE,
                                (MODEL_PAD + h) * MODEL_SCALE))
                if scale != MODEL_SCALE:
                    big = big.resize((w * scale, h * scale), Image.LANCZOS)
                made[name] = np.asarray(big).astype(np.float64)
            else:
                continue
            # one texture failed: none of this model's is used, so the
            # mod is never part one way and part the other by chance
            for name in names:
                made.pop(name, None)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return made, why


WHERE = 'where.txt'
WHERE_HEAD = '# where each texture was in video memory: key, page x y, at u v, table x y (tools/port_art.py)\n'


def where_lines(log_text):
    """The lines of where.txt that a run's log says: every texture written
    out (`wrote`) or found written (`had`), where it was."""
    import re

    out = set()
    for m in re.finditer(r'mods: (?:wrote|had) .*?([0-9]+x[0-9]+-[0-9]+-[0-9a-f]+)\.png \(page (-?\d+) (-?\d+), '
                         r'at (-?\d+) (-?\d+), table (-?\d+) (-?\d+)\)', log_text):
        out.add(' '.join(m.groups()))
    return out


def add_where(dump, lines):
    """`lines` added to the dump's where.txt, which is kept sorted, so that
    it says the same whatever order the runs ended in."""
    path = os.path.join(dump, WHERE)
    have = set()
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            have = set(line.strip() for line in f if line.strip() and not line.startswith('#'))
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(WHERE_HEAD + ''.join(line + '\n' for line in sorted(have | set(lines))))


def read_where(folders):
    """{key: [(depth, table x, table y, x, y)]}: where each texture was, x
    and y counted in texels of its own depth across video memory."""
    out = {}
    for folder in folders:
        path = os.path.join(folder, WHERE)
        if not os.path.exists(path):
            continue
        with open(path, encoding='utf-8') as f:
            for line in f:
                if line.startswith('#') or not line.strip():
                    continue
                key, px, py, u, v, cx, cy = line.split()
                depth = int(key.split('-')[1])
                place = (depth, int(cx), int(cy), int(px) * (16 // depth) + int(u), int(py) + int(v))
                if place not in out.setdefault(key, []):
                    out[key].append(place)
    for places in out.values():
        places.sort()
    return out


def goes_on(left, right):
    """Whether `right` (texels, rows by columns) goes on from `left`'s last
    column as a picture does: the step across the join no larger than
    those beside it. Two pieces of one page laid side by side that are not
    side by side on the screen (the top menu's moon, whose two halves lie
    so) do not, and are made larger apart."""
    import numpy as np

    if left.shape[1] < 2 or right.shape[1] < 2:
        return True
    a = left[:, :, :3].astype(np.float64)
    b = right[:, :, :3].astype(np.float64)
    across = np.abs(a[:, -1] - b[:, 0]).mean()
    beside = (np.abs(a[:, -1] - a[:, -2]).mean() + np.abs(b[:, 0] - b[:, 1]).mean()) / 2
    return across <= JOIN_STEP_TIMES * beside + JOIN_STEP_MIN


def touching(textures, members):
    """[(name, x, y)] split into the groups whose rectangles overlap or
    share a length of edge, one with another: the tiles of one picture,
    and not what else of its page shares its colour table."""
    def rect(member):
        name, x, y = member
        return x, y, x + textures[name].shape[1], y + textures[name].shape[0]

    def meet(a, b):
        ax0, ay0, ax1, ay1 = rect(a)
        bx0, by0, bx1, by1 = rect(b)
        across = min(ax1, bx1) - max(ax0, bx0)
        down = min(ay1, by1) - max(ay0, by0)
        if across > 0 and down > 0:
            return True
        if down > 0 and across == 0:
            left, right = (a, b) if ax1 == bx0 else (b, a)
            top, bottom = max(ay0, by0), min(ay1, by1)
            return goes_on(textures[left[0]][top - left[2]:bottom - left[2]],
                           textures[right[0]][top - right[2]:bottom - right[2]])
        if across > 0 and down == 0:
            upper, lower = (a, b) if ay1 == by0 else (b, a)
            first, last = max(ax0, bx0), min(ax1, bx1)
            return goes_on(textures[upper[0]][:, first - upper[1]:last - upper[1]].transpose(1, 0, 2),
                           textures[lower[0]][:, first - lower[1]:last - lower[1]].transpose(1, 0, 2))
        return False

    parts = []
    for member in members:
        joined = [part for part in parts if any(meet(member, other) for other in part)]
        for part in joined:
            parts.remove(part)
        parts.append(sorted([member] + [m for part in joined for m in part]))
    return sorted(parts)


def canvases(textures, names, where):
    """The textures of `names` put back together where they lay side by
    side in video memory, with one colour table: the tiles of one picture
    (a background, a map) are cut from one page, and made larger apart
    each would have an edge of its own. [(x0, y0, w, h, [(name, x, y)])],
    a texture of no known place on one of its own; one that does not
    agree with what lies under it on another canvas."""
    import numpy as np

    groups = {}
    alone = []
    for name in sorted(names):
        if name in where:
            depth, cx, cy, x, y = where[name][0]
            groups.setdefault((depth, cx, cy), []).append((name, x, y))
        else:
            alone.append(name)
    out = []
    for key in sorted(groups):
        laid = []  # [[(name, x, y)]]
        for name, x, y in groups[key]:
            h, w = textures[name].shape[:2]
            for members in laid:
                fits = True
                for other, ox, oy in members:
                    oh, ow = textures[other].shape[:2]
                    x0, y0, x1, y1 = max(x, ox), max(y, oy), min(x + w, ox + ow), min(y + h, oy + oh)
                    if x0 < x1 and y0 < y1 and not np.array_equal(
                            textures[name][y0 - y:y1 - y, x0 - x:x1 - x], textures[other][y0 - oy:y1 - oy, x0 - ox:x1 - ox]):
                        fits = False
                        break
                if fits:
                    members.append((name, x, y))
                    break
            else:
                laid.append([(name, x, y)])
        for members in [part for members in laid for part in touching(textures, members)]:
            x0 = min(x for _, x, _ in members)
            y0 = min(y for _, _, y in members)
            x1 = max(x + textures[n].shape[1] for n, x, _ in members)
            y1 = max(y + textures[n].shape[0] for n, _, y in members)
            out.append((x0, y0, x1 - x0, y1 - y0, [(n, x - x0, y - y0) for n, x, y in members]))
    for name in alone:
        h, w = textures[name].shape[:2]
        out.append((0, 0, w, h, [(name, 0, 0)]))
    return out


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


# The built-in mods whose pictures a tint of is not made here
SHIPPED_MODS = ('ui', 'english')


def shipped_keys(folders=None):
    """The keys the built-in mods replace: theirs in `folders` (the PC
    package's mods beside its program: port_prepare.py), or the
    repository's."""
    keys = set()
    for folder in folders or [os.path.join(ROOT, 'src', 'port', 'mods')]:
        for mod in SHIPPED_MODS:
            there = os.path.join(folder, mod, 'textures')
            if os.path.isdir(there):
                keys |= set(n[:-4] for n in os.listdir(there) if n.endswith('.png'))
    return keys


def tints_of_shipped(textures, shipped):
    """{name: base}: the dumped textures, of no built-in mod's, that are a
    texture of a built-in mod's tinted: its size and outline, its colours
    that one's times a number for each part, none brighter."""
    import numpy as np

    groups = {}
    for name, rgba in textures.items():
        if name in shipped:
            groups.setdefault((rgba.shape, (rgba[:, :, 3] > 0).tobytes()), []).append(name)
    out = {}
    for name, rgba in sorted(textures.items()):
        if name in shipped:
            continue
        drawn = rgba[:, :, 3] > 0
        if not drawn.any():
            continue
        c = rgba[:, :, :3][drawn].astype(np.float64)
        for base in groups.get((rgba.shape, drawn.tobytes()), []):
            b = textures[base][:, :, :3][drawn].astype(np.float64)
            tint = (c.sum(0) + 1e-9) / np.maximum(b.sum(0), 1e-9)
            if tint.max() <= 1.0 + FADE_TINT_SLACK and np.abs(b * tint - c).max() <= FADE_ERROR_MAX:
                out[name] = base
                break
    return out


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
    # a step of a tint of a texture a built-in mod replaces (a row of a
    # menu its cursor tints gold) is drawn from that mod's picture, tinted
    # (docs/MODS.md, "Fades"): a picture of the art's own would be drawn
    # in its place
    tints = tints_of_shipped(textures, shipped_keys(getattr(args, 'shipped', None)))
    textures = {k: v for k, v in textures.items() if k not in tints}
    out = os.path.join(args.out, 'textures')
    os.makedirs(out, exist_ok=True)
    # what an earlier build wrote goes: the mod is this build's alone
    for entry in os.listdir(out):
        if entry.endswith('.png'):
            os.remove(os.path.join(out, entry))
    counts = {}
    made = {}
    kinds = {}
    for name, rgba in sorted(textures.items()):
        h, w = rgba.shape[:2]
        if w < SIDE_MIN or h < SIDE_MIN:
            counts['left as it is'] = counts.get('left as it is', 0) + 1
            continue
        kinds[name] = kind_of(name, rgba)
    # the pictures and the paintings by the model, where there is a card
    # that runs it; what it could not make is made as without it
    upscaler = find_upscaler(getattr(args, 'upscaler', 'auto'))
    by_model = {}
    how = 'numpy and Pillow alone (no %s)' % UPSCALER
    if upscaler:
        # the pictures and paintings with what lay beside them, each canvas
        # made larger whole
        laid = canvases(textures, [n for n, k in kinds.items() if k != 'shapes'], read_where(args.dump))
        asked = {}
        cut = {}
        for i, (_, _, w, h, members) in enumerate(laid):
            member_kinds = [kinds[n] for n, _, _ in members]
            if not any(k in MODEL_OF for k in member_kinds):
                continue
            canvas = np.zeros((h, w, 4), np.uint8)
            for name, x, y in members:
                th, tw = textures[name].shape[:2]
                drawn = textures[name][:, :, 3:4] > 0
                canvas[y:y + th, x:x + tw] = np.where(drawn, textures[name], canvas[y:y + th, x:x + tw])
            job = 'canvas%04d' % i
            asked[job] = (canvas, MODEL_OF['picture' if 'picture' in member_kinds else 'painting'])
            cut[job] = members
        started = time.time()
        made_by, why = upscale_by_model(upscaler, asked, args.scale)
        s = args.scale
        for job, members in cut.items():
            if job not in made_by:
                continue
            for name, x, y in members:
                th, tw = textures[name].shape[:2]
                by_model[name] = made_by[job][y * s:(y + th) * s, x * s:(x + tw) * s]
                if kinds[name] not in MODEL_OF:
                    # beside a painting in its page: one with it
                    kinds[name] = 'painting'
        models = sorted(set(m for j, (_, m) in asked.items() if j in made_by))
        print('port_art: %d textures on %d canvases by %s in %.1f seconds%s' % (
            len(by_model), len(made_by), ', '.join(models) or 'no model', time.time() - started,
            '; the rest without it: ' + why if why else ''))
        if models:
            how = '%s, by %s' % (', '.join(models), UPSCALER)
    for name, kind in sorted(kinds.items()):
        rgba = textures[name]
        if kind in MODEL_OF and name not in by_model:
            kind = FALLBACK_OF[kind]
        big = ways[FALLBACK_OF.get(kind, kind)](rgba.astype(np.float64), args.scale)
        if name in by_model:
            # the model's colours, today's in the dark, and the outline of
            # today's way
            light = np.clip(big[:, :, :3].max(2, keepdims=True) / DARK_LEVEL, 0, 1)
            big = np.dstack([by_model[name] * light + big[:, :, :3] * (1 - light), big[:, :, 3:4]])
        image = Image.fromarray(np.clip(big + 0.5, 0, 255).astype(np.uint8), 'RGBA')
        made[name] = image
        image.save(os.path.join(out, name + '.png'), optimize=False, compress_level=6)
        kind += ' by the model' if name in by_model else ''
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
        f.write(MANIFEST + MADE_BY % how)
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
    ap.add_argument('--modes', type=lambda s: s.split(','), help='only these tours (dump): %s' % ','.join(MODES))
    ap.add_argument('--logs', help="a folder for each tour's log (dump)")
    ap.add_argument('--upscaler', default='auto',
                    help="the folder of realesrgan-ncnn-vulkan and its models/ (build); off for numpy and Pillow "
                    "alone; auto: RB_UPSCALER, the package's prepare/upscaler/, tmp/art-tools/")
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
