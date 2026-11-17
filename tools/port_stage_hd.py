#!/usr/bin/env python3
"""Builds the "stages-hd" mod of the native port from the player's own disc:
every texture page of every stage drawn again at four times its resolution,
for the remastered picture (docs/MODS.md, "The stages' pages made larger").

    python3 tools/port_stage_hd.py dump  [--image CUE] [--dump tmp/stage-hd/dump]
                                         [--stages 0-37] [--jobs 6]
    python  tools/port_stage_hd.py build [--dump tmp/stage-hd/dump] [--out tmp/mods/stages-hd]
                                         [--scale 4] [--keys KEY,...] [--jobs N]
    python3 tools/port_stage_hd.py all   (both, where one Python has numpy)

`dump` loads every stage on the GL prover (tools/port_gl_build.py; Linux, or
WSL) with no window: the tour of tools/port_stage_prove.py as far as the
fight, with no mod in the way and `--dump-textures`, so the port writes each
page of the stage seen through each of its colour tables, as the stage's file
loaded them, into `pages/` of the dump (gl_scene.c), with a list beside it of
the stage's own textures in that page.

`build` makes each of those pages `scale` times larger, texture by texture:
each of the stage's own textures in the page -- a TIM of the stage's file --
is made larger by itself, so that nothing of the texture packed beside it is
smoothed into it, in four steps, all written here in numpy and Pillow:

  1. **the dither taken out.** The textures are 16 colours each, and their
     artists dithered between two near colours to make a third: at four
     times that is a checkerboard. A texel that stands out from all four of
     its neighbours by no more than a step of its own colours, the same way
     round, is dither; it is smoothed with its neighbours of near colour
     (a bilateral filter), and every other texel is kept as it is, so a
     line, an edge and a grain of the wood stay sharp.
  2. **made larger along its edges**: twice, then twice again, each time by
     directional cubic convolution (Zhou, Shen and Zeng, 2012): a new texel
     is made by a cubic along whichever of two directions changes less about
     it, or a blend of the two where neither does, so an edge is made longer
     and not wider or stepped.
  3. **held to the game's own picture**: made smaller again, the result is
     the texture it was made from, to the colour: what is left over is made
     larger and added back, three times (back-projection). So far off, where
     the card reads the picture's smallest levels, the stage is the colour
     the game drew it.
  4. **sharpened without halos**: an unsharp mask, each texel held between
     the least and the most of the texels about it.

A texel of nothing (colour 0) is clear: the clear and the drawn are made
larger as a shape of their own, with an edge a texel or two wide. The game's
own smaller copies of a texture (a TIM half the size of another of the same
table, at half its place in the page: what the renderer reads far off) are
made from the larger one's picture made smaller, which is sharper than the
copy made larger. The textures of another table in the page are what this
table would show of them and nothing a face draws: they are copied as they
are, each texel a block, so that a face that did draw them is still drawn.

Nothing here is a learned model and no program of anyone else's is used, so
nothing is added to what the project's licence covers.

What is written is the game's own pictures made larger: it goes into a folder
git ignores (tmp/mods/stages-hd) and is never committed. Copy it into `mods/`
beside the program. Its manifest gives it the order -200, as the art mod's.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DUMP = os.path.join(ROOT, 'tmp', 'stage-hd', 'dump')
DEFAULT_OUT = os.path.join(ROOT, 'tmp', 'mods', 'stages-hd')
DEFAULT_PROGRAM = os.path.join(ROOT, 'build', 'port-gl', 'rebushido-gl')
DEFAULT_IMAGE = os.path.join(ROOT, 'Bushido Blade 2 (USA).cue')
ALL_STAGES = 38
# how far a stage's tour is played: past the loading of its fight
DUMP_FRAMES = 3000

MANIFEST = '''# The stages' texture pages made larger, from your own disc, by
# tools/port_stage_hd.py (docs/MODS.md, "The stages' pages made larger").
# Never committed.
name = Stages HD
version = 1.0
order = -200      # under the built-in mods and under any of yours
author = ReBushido (tools/port_stage_hd.py)
'''

PAGE_SIDE = 256

# ---- dump: every stage loaded once ----


def stage_list(text):
    stages = []
    for piece in text.split(','):
        if '-' in piece:
            first, last = piece.split('-')
            stages += list(range(int(first), int(last) + 1))
        elif piece:
            stages.append(int(piece))
    return [s for s in stages if 0 <= s < ALL_STAGES]


def dump(args):
    """Loads each stage on the GL prover, some at once, into one folder."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import port_remaster_prove as remaster

    program = os.path.abspath(args.program)
    if not os.path.exists(program):
        print('port_stage_hd: no %s: python3 tools/port_gl_build.py builds it' % program)
        return 1
    if not os.path.exists(args.image):
        print('port_stage_hd: no disc image %s (--image)' % args.image)
        return 1
    remaster.PROGRAM = os.path.relpath(program, ROOT)
    os.chdir(ROOT)
    os.makedirs(args.dump, exist_ok=True)
    work = tempfile.mkdtemp(prefix='port_stage_hd_')
    empty = os.path.join(work, 'no-mods')
    os.makedirs(empty)
    toml = remaster.write_tour(os.path.join(work, 'tour.toml'), DUMP_FRAMES, 1)
    waiting = stage_list(args.stages)
    running = []
    failed = []
    while waiting or running:
        while waiting and len(running) < args.jobs:
            stage = waiting.pop(0)
            out = os.path.join(work, 'stage%02d' % stage)
            os.makedirs(out)
            plan = remaster.plan_for(toml, os.path.join(out, 'plan.txt'), stage, DUMP_FRAMES)
            log = open(os.path.join(out, 'log.txt'), 'w')
            cmd = [program, '--image', args.image, '--plan', plan, '--out', out,
                   '--graphics', 'remastered', '--gl-scale', '1', '--mods', empty,
                   '--dump-textures', args.dump]
            running.append((stage, subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT), log, out))
        stage, process, log, out = running.pop(0)
        process.wait()
        log.close()
        with open(os.path.join(out, 'log.txt'), errors='replace') as f:
            said = [line for line in f if "the stage's pages" in line]
        print('port_stage_hd: stage %2d %s: %s' % (stage, remaster.STAGE_NAMES[stage],
                                                    said[-1].strip() if said else 'NO PAGES'), flush=True)
        if not said and stage not in failed:
            # a run that stopped short (the machine busy): once more
            failed.append(stage)
            waiting.append(stage)
            shutil.rmtree(out)
        elif said and stage in failed:
            failed.remove(stage)
    shutil.rmtree(work)
    pages = os.path.join(args.dump, 'pages')
    count = len([n for n in os.listdir(pages) if n.endswith('.png')]) if os.path.isdir(pages) else 0
    print('port_stage_hd: %d pages in %s%s' % (count, pages,
                                              '; no pages from stages %s' % failed if failed else ''))
    return 0 if count else 1


# ---- build: the upscaler ----


def read_sidecar(path):
    """The page's own table and the stage's textures in it: (table, [(u, v,
    w, h, table)])."""
    table = None
    textures = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            words = line.split()
            if not words:
                continue
            if words[0] == 'page':
                table = (int(words[6]), int(words[7]))
            elif words[0] == 'texture':
                u, v, w, h = (int(x) for x in words[1:5])
                textures.append((u, v, w, h, (int(words[6]), int(words[7]))))
    return table, textures


def make_upscaler(np, Image):
    """The four steps (the module's docstring), as functions of numpy
    arrays of floats, 0 to 255, rows by columns by channels."""

    def resize(arr, w, h, how):
        return np.dstack([np.asarray(Image.fromarray(arr[:, :, c].astype(np.float32), mode='F').resize((w, h), how))
                          for c in range(arr.shape[2])])

    def box_down(arr, s):
        h, w = arr.shape[:2]
        return arr.reshape(h // s, s, w // s, s, -1).mean(axis=(1, 3))

    def fill_clear(rgb, alpha):
        """The colour of a clear texel from its drawn neighbours, so that
        smoothing an edge does not bring black in."""
        rgb = rgb.copy()
        known = alpha > 0
        if known.all() or not known.any():
            return rgb
        for _ in range(max(rgb.shape[:2])):
            if known.all():
                break
            p = np.pad(rgb * known[..., None], ((1, 1), (1, 1), (0, 0)), mode='edge')
            k = np.pad(known.astype(np.float64), 1, mode='edge')
            total = np.zeros_like(rgb)
            count = np.zeros(known.shape)
            for dy, dx in ((0, 1), (2, 1), (1, 0), (1, 2)):
                total += p[dy:dy + known.shape[0], dx:dx + known.shape[1]]
                count += k[dy:dy + known.shape[0], dx:dx + known.shape[1]]
            grow = (~known) & (count > 0)
            rgb[grow] = total[grow] / count[grow][:, None]
            known = known | grow
        return rgb

    def dedither(rgb, alpha):
        """Step 1: a texel that stands out from all four neighbours the same
        way round by no more than a step of the texture's own colours is
        dither, and is smoothed with its neighbours of near colour."""
        h, w = rgb.shape[:2]
        if h < 3 or w < 3:
            return rgb
        colors = np.unique(rgb[alpha > 0].reshape(-1, 3), axis=0)
        if len(colors) < 3:
            return rgb
        # a step of its own colours: the median of each colour's distance
        # to the nearest other
        d = np.sqrt(((colors[:, None, :] - colors[None, :, :]) ** 2).sum(-1))
        d[d == 0] = np.inf
        step = float(np.median(d.min(axis=1)))
        luma = rgb @ np.array([0.299, 0.587, 0.114])
        p = np.pad(luma, 1, mode='reflect')
        n = [p[0:h, 1:w + 1], p[2:h + 2, 1:w + 1], p[1:h + 1, 0:w], p[1:h + 1, 2:w + 2]]
        above = np.all([luma > x for x in n], axis=0)
        below = np.all([luma < x for x in n], axis=0)
        reach = np.max([np.abs(luma - x) for x in n], axis=0)
        dither = (above | below) & (reach <= 2.0 * step) & (alpha > 0)
        if not dither.any():
            return rgb
        # the bilateral filter, 3 by 3, its range a step and a half
        pr = np.pad(rgb, ((1, 1), (1, 1), (0, 0)), mode='reflect')
        total = np.zeros_like(rgb)
        weight = np.zeros((h, w))
        sigma = 1.5 * step
        for dy in range(3):
            for dx in range(3):
                q = pr[dy:dy + h, dx:dx + w]
                space = 1.0 if (dy, dx) == (1, 1) else (0.6 if dy == 1 or dx == 1 else 0.35)
                rng = np.exp(-((q - rgb) ** 2).sum(-1) / (2 * sigma * sigma))
                total += q * (space * rng)[..., None]
                weight += space * rng
        out = rgb.copy()
        out[dither] = (total / weight[..., None])[dither]
        return out

    def dcci2(img):
        """Step 2, once: twice the size by directional cubic convolution."""
        h, w, c = img.shape
        pad = 2
        p = np.pad(img, ((pad, pad + 1), (pad, pad + 1), (0, 0)), mode='reflect')
        luma = p @ (np.array([0.299, 0.587, 0.114]) if c >= 3 else np.ones(c) / c)
        out = np.zeros((2 * h, 2 * w, c))
        out[0::2, 0::2] = img
        threshold = 1.15
        power = 5

        def cubic(a, b, cc, d):
            return (-a + 9 * b + 9 * cc - d) / 16.0

        def blend(g1, g2, along1, along2):
            """The value along the direction that changes less, or both
            weighed by how little each changes."""
            r = (1 + g1) / (1 + g2)
            w1 = 1.0 / (1.0 + g1 ** power)
            w2 = 1.0 / (1.0 + g2 ** power)
            mix = (w1[..., None] * along1 + w2[..., None] * along2) / (w1 + w2)[..., None]
            result = np.where((r > threshold)[..., None], along2, mix)
            return np.where((r < 1.0 / threshold)[..., None], along1, result)

        # the texels between four: at (i + 1/2, j + 1/2)
        def at(dy, dx, src):
            return src[pad + dy:pad + dy + h, pad + dx:pad + dx + w]
        g_down = np.zeros((h, w))   # along "\": (y, x) to (y + 1, x + 1)
        g_up = np.zeros((h, w))     # along "/": (y, x + 1) to (y + 1, x)
        for dy in range(-1, 2):
            for dx in range(-1, 2):
                g_down += np.abs(at(dy, dx, luma) - at(dy + 1, dx + 1, luma))
                g_up += np.abs(at(dy, dx + 1, luma) - at(dy + 1, dx, luma))
        along_down = cubic(at(-1, -1, p), at(0, 0, p), at(1, 1, p), at(2, 2, p))
        along_up = cubic(at(-1, 2, p), at(0, 1, p), at(1, 0, p), at(2, -1, p))
        out[1::2, 1::2] = blend(g_down, g_up, along_down, along_up)
        # then the texels between two, on the lattice the first step filled
        q = np.pad(out, ((2 * pad, 2 * pad), (2 * pad, 2 * pad), (0, 0)), mode='reflect')
        ql = q @ (np.array([0.299, 0.587, 0.114]) if c >= 3 else np.ones(c) / c)
        H, W = out.shape[:2]

        def big(dy, dx, src, oy, ox):
            return src[2 * pad + oy + dy:2 * pad + oy + dy + H:2, 2 * pad + ox + dx:2 * pad + ox + dx + W:2]
        for oy, ox in ((0, 1), (1, 0)):
            # gradients across and down about the new texel, from its
            # neighbours a step away on the filled lattice
            g_across = (np.abs(big(0, -1, ql, oy, ox) - big(0, 1, ql, oy, ox))
                        + np.abs(big(-2, -1, ql, oy, ox) - big(-2, 1, ql, oy, ox)) * 0.5
                        + np.abs(big(2, -1, ql, oy, ox) - big(2, 1, ql, oy, ox)) * 0.5
                        + np.abs(big(-1, 0, ql, oy, ox) - big(-1, 2, ql, oy, ox)) * 0.5
                        + np.abs(big(-1, 0, ql, oy, ox) - big(-1, -2, ql, oy, ox)) * 0.5
                        + np.abs(big(1, 0, ql, oy, ox) - big(1, 2, ql, oy, ox)) * 0.5
                        + np.abs(big(1, 0, ql, oy, ox) - big(1, -2, ql, oy, ox)) * 0.5)
            g_down2 = (np.abs(big(-1, 0, ql, oy, ox) - big(1, 0, ql, oy, ox))
                       + np.abs(big(-1, -2, ql, oy, ox) - big(1, -2, ql, oy, ox)) * 0.5
                       + np.abs(big(-1, 2, ql, oy, ox) - big(1, 2, ql, oy, ox)) * 0.5
                       + np.abs(big(0, -1, ql, oy, ox) - big(2, -1, ql, oy, ox)) * 0.5
                       + np.abs(big(0, -1, ql, oy, ox) - big(-2, -1, ql, oy, ox)) * 0.5
                       + np.abs(big(0, 1, ql, oy, ox) - big(2, 1, ql, oy, ox)) * 0.5
                       + np.abs(big(0, 1, ql, oy, ox) - big(-2, 1, ql, oy, ox)) * 0.5)
            along_across = cubic(big(0, -3, q, oy, ox), big(0, -1, q, oy, ox), big(0, 1, q, oy, ox),
                                 big(0, 3, q, oy, ox))
            along_down = cubic(big(-3, 0, q, oy, ox), big(-1, 0, q, oy, ox), big(1, 0, q, oy, ox),
                               big(3, 0, q, oy, ox))
            out[oy::2, ox::2] = blend(g_across, g_down2, along_across, along_down)
        return out

    def larger(img, s):
        """Step 2: `s` times larger, a power of two, by doubling; any other
        scale by Lanczos from the nearest power below."""
        out = img
        done = 1
        while done * 2 <= s:
            out = dcci2(out)
            done *= 2
        if done != s:
            h, w = img.shape[:2]
            out = resize(out, w * s, h * s, Image.LANCZOS)
        return out

    def bounds(small, s):
        """The least and the most of each source texel and its eight
        neighbours, a block each: no texel made larger goes outside what
        the source has about it, which is what keeps a dark bar from a
        bright rim (ringing)."""
        h, w = small.shape[:2]
        p = np.pad(small, ((1, 1), (1, 1), (0, 0)), mode='edge')
        stack = np.stack([p[dy:dy + h, dx:dx + w] for dy in range(3) for dx in range(3)])
        low = np.repeat(np.repeat(stack.min(axis=0), s, axis=0), s, axis=1)
        high = np.repeat(np.repeat(stack.max(axis=0), s, axis=0), s, axis=1)
        return low, high

    def held(big, small, s, times=3):
        """Step 3: made smaller it is `small`; what is left over is made
        larger and added back, held inside the source's own bounds."""
        h, w = small.shape[:2]
        low, high = bounds(small, s)
        for _ in range(times):
            left = small - box_down(big, s)
            big = np.clip(big + resize(left, w * s, h * s, Image.BILINEAR), low, high)
        return big

    def sharpened(big, small, s, amount=0.5):
        """Step 4: an unsharp mask held between the least and the most of
        the texels about each, and inside the source's own bounds."""
        h, w = big.shape[:2]
        blur = resize(resize(big, max(1, w // 2), max(1, h // 2), Image.BOX), w, h, Image.BICUBIC)
        sharp = big + amount * (big - blur)
        p = np.pad(big, ((1, 1), (1, 1), (0, 0)), mode='edge')
        stack = np.stack([p[dy:dy + h, dx:dx + w] for dy in range(3) for dx in range(3)])
        low, high = bounds(small, s)
        return np.clip(np.clip(sharp, stack.min(axis=0), stack.max(axis=0)), low, high)

    def texture(rgba, s):
        """One texture of the stage, `s` times larger."""
        pad = 3
        h, w = rgba.shape[:2]
        alpha = rgba[:, :, 3] / 255.0
        rgb = fill_clear(rgba[:, :, :3], alpha)
        rgb = dedither(rgb, alpha)
        mode = 'reflect' if min(h, w) > pad else 'edge'
        p_rgb = np.pad(rgb, ((pad, pad), (pad, pad), (0, 0)), mode=mode)
        p_alpha = np.pad(alpha, pad, mode=mode)
        big = larger(p_rgb, s)
        big = held(big, p_rgb, s)
        big = sharpened(big, p_rgb, s)
        big = big[pad * s:(pad + h) * s, pad * s:(pad + w) * s]
        if alpha.min() >= 1.0:
            big_alpha = np.ones(big.shape[:2])
        else:
            a = larger(p_alpha[..., None] * 255.0, s)[:, :, 0] / 255.0
            a = a[pad * s:(pad + h) * s, pad * s:(pad + w) * s]
            big_alpha = np.clip((a - 0.5) * (s / 1.6) + 0.5, 0, 1)
        return np.dstack([np.clip(big, 0, 255), big_alpha * 255.0])

    return {'texture': texture, 'box_down': box_down}


def build_page(job):
    """One page of the dump made larger: (name, its texels, its list, the
    scale, the output folder) -> counts."""
    import numpy as np
    from PIL import Image

    name, png, sidecar, scale, out = job
    ways = make_upscaler(np, Image)
    page = np.asarray(Image.open(png).convert('RGBA')).astype(np.float64)
    table, textures = read_sidecar(sidecar)
    canvas = np.zeros((PAGE_SIDE * scale, PAGE_SIDE * scale, 4))
    made = {}
    counts = {'own': 0, 'copies': 0, 'other': 0}
    seen = set()
    # the largest first, so that a smaller copy of one can be made from it
    for u, v, w, h, their in sorted(textures, key=lambda t: -t[2] * t[3]):
        u0, v0 = max(u, 0), max(v, 0)
        u1, v1 = min(u + w, PAGE_SIDE), min(v + h, PAGE_SIDE)
        if u1 <= u0 or v1 <= v0 or (u0, v0, u1, v1) in seen:
            continue
        seen.add((u0, v0, u1, v1))
        rgba = page[v0:v1, u0:u1]
        at = (slice(v0 * scale, v1 * scale), slice(u0 * scale, u1 * scale))
        if their != table:
            # another table's texture: what this one shows of it, as it is
            canvas[at] = np.repeat(np.repeat(rgba, scale, axis=0), scale, axis=1)
            counts['other'] += 1
            continue
        parent = made.get((2 * u0, 2 * v0, 2 * (u1 - u0), 2 * (v1 - v0)))
        if parent is not None:
            # the game's own smaller copy of a texture: the larger one's
            # picture made smaller, its clear texels kept as the copy has them
            big = ways['box_down'](parent, 2)
            canvas[at] = big
            counts['copies'] += 1
        else:
            big = ways['texture'](rgba, scale)
            canvas[at] = big
            counts['own'] += 1
        made[(u0, v0, u1 - u0, v1 - v0)] = canvas[at].copy()
    image = Image.fromarray(np.clip(canvas + 0.5, 0, 255).astype(np.uint8), 'RGBA')
    image.save(os.path.join(out, name + '.png'), compress_level=6)
    return counts


def build(args):
    try:
        import numpy  # noqa: F401
        import PIL  # noqa: F401
    except ImportError:
        print('port_stage_hd: build needs numpy and Pillow')
        return 1
    pages = os.path.join(args.dump, 'pages')
    if not os.path.isdir(pages):
        print('port_stage_hd: no folder %s: `dump` first, or --dump FOLDER' % pages)
        return 1
    names = sorted(n[:-4] for n in os.listdir(pages)
                   if n.endswith('.png') and os.path.exists(os.path.join(pages, n[:-4] + '.txt')))
    if args.keys:
        names = [n for n in names if n in args.keys.split(',')]
    out = os.path.join(args.out, 'textures')
    os.makedirs(out, exist_ok=True)
    # what an earlier build wrote goes: the mod is this build's alone
    for entry in os.listdir(out):
        if entry.endswith('.png'):
            os.remove(os.path.join(out, entry))
    jobs = [(n, os.path.join(pages, n + '.png'), os.path.join(pages, n + '.txt'), args.scale, out)
            for n in names]
    totals = {}
    if args.jobs > 1:
        import concurrent.futures
        with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(build_page, jobs))
    else:
        results = [build_page(job) for job in jobs]
    for counts in results:
        for k, v in counts.items():
            totals[k] = totals.get(k, 0) + v
    with open(os.path.join(args.out, 'mod.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(MANIFEST)
    print('port_stage_hd: %d pages into %s: %d textures made larger, %d of the game\'s smaller copies '
          'from them, %d of another table copied' % (len(names), args.out, totals.get('own', 0),
                                                     totals.get('copies', 0), totals.get('other', 0)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('step', nargs='?', default='all', choices=['dump', 'build', 'all'])
    ap.add_argument('--image', default=DEFAULT_IMAGE)
    ap.add_argument('--program', default=DEFAULT_PROGRAM)
    ap.add_argument('--dump', default=DEFAULT_DUMP)
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--stages', default='0-%d' % (ALL_STAGES - 1))
    ap.add_argument('--scale', type=int, default=4)
    ap.add_argument('--keys', help='only these keys (build)')
    ap.add_argument('--jobs', type=int, default=max(1, min(6, (os.cpu_count() or 2) - 1)))
    args = ap.parse_args()
    if args.step in ('dump', 'all') and dump(args):
        return 1
    if args.step in ('build', 'all'):
        return build(args)
    return 0


if __name__ == '__main__':
    sys.exit(main())
