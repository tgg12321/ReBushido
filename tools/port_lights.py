#!/usr/bin/env python3
"""Finds where the stages' own lights are, for the light files the program
ships (src/port/mods/lights/; docs/MODS.md, "Lights"), and checks the files.

    python3 tools/port_lights.py dump  [--image CUE] [--dump tmp/lights/dump]
                                       [--stages 0-37] [--jobs 6]
    python3 tools/port_lights.py find  [--dump tmp/lights/dump] [--stages 0-37]
                                       [--top 12] [--out tmp/lights/found]
    python3 tools/port_lights.py check [--folder src/port/mods/lights/lights]

`dump` loads every stage on the GL prover (tools/port_gl_build.py; Linux, or
WSL) with no window: the tour of tools/port_stage_prove.py as far as the
fight, with the ground and its pieces as one mesh on every stage and
`--dump-textures`, so that the program writes the stage's mesh as it built
it, each vertex where it is in the world with its texture point, page and
colour table, and all of video memory beside it
(gl_light_list.h, `port_GlLightListDumpMesh`), into `meshes/stage_NN.bin`
of the dump; and the run's log, which says where the stage's own flames are
(port_lights.h), into `flames.txt`. The dump is of your disc: it goes into a
folder git ignores and is never committed.

`find` reads each stage's mesh, looks up the texels of every face (a few
points of each, by its texture points), and keeps those that look lit from
within: bright and warm (a lantern's paper, a fire, a lit window) or bright
and white against a dark stage. Those near each other in the world are
gathered, and the largest gatherings are written, with their places, the
mean colour of their texels and whether they are of the ground or of a
piece, as a draft light file a stage (`--out`), beside the flames the game
itself draws. A draft is where a hand starts: the files shipped are those
drafts looked at in the pictures, kept or left out, moved, coloured and
made to flicker by hand. Stages with nothing that gives light get no file.

`check` reads every shipped file as the program does (the parser is
tests/port/lights.c's, the C itself): every line understood, and every
light on the stage inside the stage's bounds (BOUNDS below, measured from
the meshes by `find --bounds`).
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DUMP = os.path.join(ROOT, 'tmp', 'lights', 'dump')
DEFAULT_FOUND = os.path.join(ROOT, 'tmp', 'lights', 'found')
DEFAULT_PROGRAM = os.path.join(ROOT, 'build', 'port-gl', 'rebushido-gl')
DEFAULT_IMAGE = os.path.join(ROOT, 'Bushido Blade 2 (USA).cue')
SHIPPED = os.path.join(ROOT, 'src', 'port', 'mods', 'lights', 'lights')
ALL_STAGES = 38
# how far a stage's tour is played: into its fight
DUMP_FRAMES = 3200
# and further with --shots, the fighter run across the stage
SHOT_FRAMES = 2400
SHOT_LAPS = 18
SHEET_PICTURES = 6
SHEET_WIDTH = 480

# Each stage's bounds in the world, x, y and z, least and most, from the
# ground's mesh (`find --bounds`), rounded out to 500 units: a shipped
# light on the stage lies inside them. The world's y runs down.
BOUNDS = {
    0: ([-12000, -7000, -10000], [8500, 2000, 10500]),  # main_tatara_shrine
    1: ([-16500, -6000, -10000], [14500, 1000, 10500]),  # meikyokan_dojo
    2: ([-12000, -6000, -12000], [12500, 1000, 13500]),  # shainto_hq
    3: ([-18000, -9000, -14000], [18500, 2000, 14500]),  # tatara_shrine
    4: ([-14000, -4000, -16000], [14000, 1000, 16500]),  # bus_stop
    5: ([-6000, -8000, -16000], [6500, 7000, 19500]),  # lost_bridge
    6: ([-23500, -8000, -12000], [22500, 3000, 13500]),  # tatara_shrine_path
    7: ([-10000, -4000, -8500], [14500, 1000, 12000]),  # bamboo_thicket
    8: ([-14000, -6500, -20500], [16500, 1500, 20500]),  # outside_meikyokan
    9: ([-24000, -5000, -24000], [30500, 2500, 24500]),  # future_dump_site
    10: ([-14000, -12000, -15500], [22500, 8000, 16000]),  # snake_cliff
    11: ([-20000, -8000, -16000], [18500, 2000, 16500]),  # kimigaura_coast
    12: ([-13000, -8000, -14000], [13000, 8000, 23000]),  # orange_tree_fields
    13: ([-17000, 0, -11000], [17500, 9500, 11000]),  # castle_rooftop
    14: ([-12000, -10000, -12000], [8500, 1000, 19000]),  # under_lost_bridge
    15: ([-10000, -8000, -16000], [10500, 5500, 14000]),  # terraced_fields
    16: ([-8000, -8000, -16000], [20500, 5500, 16500]),  # under_railway
    17: ([-14000, -8000, -18000], [14500, 9000, 13000]),  # water_tower_dam
    18: ([-6000, -7000, -18500], [6500, 4000, 16500]),  # lumberyard
    19: ([-12000, -10500, -20500], [12500, 4500, 15500]),  # lighthouse
    20: ([-12000, -8500, -19000], [16500, 4500, 8000]),  # kabuki_stage
    21: ([-14000, -7500, -14000], [13000, 4000, 16500]),  # abandoned_ship
    22: ([-16000, -3000, -12000], [16500, 7000, 15500]),  # ferry_dock
    23: ([-18000, -6000, -8000], [16500, 1000, 8500]),  # shainto_gate
    24: ([-13000, -8000, -12500], [9000, 4000, 12500]),  # purple_cavern
    25: ([-12500, -7000, -24000], [14500, 2000, 24500]),  # abandoned_station
    26: ([-31500, -5500, -21000], [32500, 8000, 18500]),  # railway
    27: ([-16000, -8000, -9000], [20500, 1000, 7500]),  # abandoned_tunnel
    28: ([-7500, -8000, -18000], [6500, 1000, 18500]),  # abandoned_shipyard
    29: ([-14500, -8000, -24000], [18500, 6000, 24500]),  # building_rooftop
    30: ([-10000, -6000, -18000], [8500, 1000, 20500]),  # building_parking_lot
    31: ([-17000, -4000, -10000], [20500, 6000, 9500]),  # shainto_cemetery
    32: ([-10000, -6000, -10000], [8500, 1000, 10500]),  # team_1
    33: ([-10000, -2500, -10500], [10500, 1500, 10500]),  # team_2
    34: ([-16000, -5000, -4000], [16500, 1000, 2500]),  # chanbara
    35: ([-10000, 0, -10000], [10500, 1000, 10500]),  # practice_1
    36: ([-14000, 0, -14000], [14500, 1500, 14500]),  # practice_2
    37: ([-22000, -8000, -28000], [24500, 2000, 26500]),  # stage_37
}

# ---- the mesh's file (gl_light_list.c, gl_stage.c's StageVertex) ----

VRAM_W, VRAM_H = 1024, 512
# StageVertex: x y z u v (floats), colour[4], page[4] (s16: the page's x
# and y, the colour table's x and y), info[4] (the texel's bits, the mix),
# tile, pad, normal[4], box[4], corner[4][3]
VERTEX = struct.Struct('<5f4B4h4BHH4h4B12f')


def stage_list(text):
    stages = []
    for piece in text.split(','):
        if '-' in piece:
            first, last = piece.split('-')
            stages += list(range(int(first), int(last) + 1))
        elif piece:
            stages.append(int(piece))
    return [s for s in stages if 0 <= s < ALL_STAGES]


def read_mesh(path):
    """(vertices, pieces_from, vram): each vertex (x, y, z, u, v, page_x,
    page_y, clut_x, clut_y, bits)."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'RBLM':
        raise SystemExit('port_lights: %s is not a mesh\'s dump' % path)
    _version, _stage, count, stride, pieces_from = struct.unpack_from('<5I', data, 4)
    at = 24
    vertices = []
    for i in range(count):
        v = VERTEX.unpack_from(data, at + i * stride)
        vertices.append((v[0], v[1], v[2], v[3], v[4], v[9], v[10], v[11], v[12], v[13], v[5], v[6], v[7]))
    at += count * stride
    vram = struct.unpack_from('<%dH' % (VRAM_W * VRAM_H), data, at)
    at += 2 * VRAM_W * VRAM_H
    # the stage's textures as loaded (PortStageTexture): a face names its
    # colour table by a row the fog writes over each frame; the table
    # itself is where it was loaded
    tables = []
    if _version >= 2:
        (textures,) = struct.unpack_from('<I', data, at)
        at += 4
        for i in range(textures):
            t = struct.unpack_from('<10h', data, at + 20 * i)
            clut_x, clut_y, clut_w, clut_h, named_y = t[5], t[6], t[7], t[8], t[9]
            if clut_x >= 0:
                tables.append((clut_x, clut_x + max(1, clut_w), named_y, max(1, clut_h), clut_y))
    loaded = {}

    def row_of(x, y):
        if (x, y) not in loaded:
            loaded[(x, y)] = y
            for x0, x1, named, h, real in tables:
                if x0 <= x < x1 and named <= y < named + h:
                    loaded[(x, y)] = real + y - named
                    break
        return loaded[(x, y)]

    vertices = [x[:8] + (row_of(x[7], x[8]),) + x[9:] for x in vertices]
    return vertices, pieces_from, vram


def texel(vram, page_x, page_y, clut_x, clut_y, bits, u, v):
    """The colour a face reads at (u, v): (r, g, b) of 0..1, or None where
    it is clear."""
    u, v = int(u) & 0xFF, int(v) & 0xFF
    row = ((page_y + v) % VRAM_H) * VRAM_W
    if bits == 0:
        index = (vram[row + (page_x + u // 4) % VRAM_W] >> ((u % 4) * 4)) & 0xF
        color = vram[(clut_y % VRAM_H) * VRAM_W + (clut_x + index) % VRAM_W]
    elif bits == 1:
        index = (vram[row + (page_x + u // 2) % VRAM_W] >> ((u % 2) * 8)) & 0xFF
        color = vram[(clut_y % VRAM_H) * VRAM_W + (clut_x + index) % VRAM_W]
    else:
        color = vram[row + (page_x + u) % VRAM_W]
    if color == 0:
        return None
    return ((color & 31) / 31.0, ((color >> 5) & 31) / 31.0, ((color >> 10) & 31) / 31.0)


def glow(rgb, bright):
    """How much a texel looks lit from within, 0 or up to 1: brighter than
    `bright` (the stage's own brightest few in a hundred) and warm, or near
    white. The colours are as drawn: the texel times the vertex's colour."""
    r, g, b = rgb
    high, low = max(rgb), min(rgb)
    if high < bright or high <= 0:
        return 0.0
    saturation = (high - low) / high
    if r >= g >= b and saturation > 0.3:
        # warm: red to yellow, a flame's or a lantern's
        hue = 60.0 * (g - b) / (high - low)
        if 12.0 <= hue <= 62.0:
            return 1.0
    if saturation < 0.15:
        return 0.6  # a white light: a lamp, a lit window
    return 0.0


def lit(rgb, vertex):
    """A texel as the GPU draws it, its vertex's colour over 128 times."""
    return tuple(min(1.0, rgb[i] * vertex[i] / 128.0) for i in range(3))


# ---- dump: every stage loaded once ----


def dump(args):
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import port_remaster_prove as remaster
    import tempfile  # here: tests/random.py stands in for the library's where the tests import this

    program = os.path.abspath(args.program)
    if not os.path.exists(program):
        print('port_lights: no %s: python3 tools/port_gl_build.py builds it' % program)
        return 1
    if not os.path.exists(args.image):
        print('port_lights: no disc image %s (--image)' % args.image)
        return 1
    remaster.PROGRAM = os.path.relpath(program, ROOT)
    os.chdir(ROOT)
    os.makedirs(os.path.join(args.dump, 'meshes'), exist_ok=True)
    work = tempfile.mkdtemp(prefix='port_lights_')
    empty = os.path.join(work, 'no-mods')
    os.makedirs(empty)
    mods = os.path.abspath(args.mods) if args.mods else empty
    frames = DUMP_FRAMES + (SHOT_FRAMES if args.shots else 0)
    # the fight's checkpoints, two seconds each: one, or the run across
    toml = remaster.write_tour(os.path.join(work, 'tour.toml'), frames, SHOT_LAPS if args.shots else 1)
    waiting = stage_list(args.stages)
    running = []
    flames = {}
    tried = set()
    while waiting or running:
        while waiting and len(running) < args.jobs:
            stage = waiting.pop(0)
            out = os.path.join(work, 'stage%02d' % stage)
            os.makedirs(out, exist_ok=True)
            plan = remaster.plan_for(toml, os.path.join(out, 'plan.txt'), stage, frames)
            log = open(os.path.join(out, 'log.txt'), 'w')
            cmd = [program, '--image', args.image, '--plan', plan, '--out', out,
                   '--graphics', 'remastered', '--gl-scale', '1', '--mods', mods,
                   '--stage-mesh', 'every', '--stage-props', 'every',
                   '--dump-textures', args.dump]
            if args.shots:
                # pictures of the fight's tour, to look at: the stage's
                # lights on (the mods' files) and the flashes
                os.makedirs(os.path.join(out, 'shots'))
                cmd[cmd.index('--gl-scale') + 1] = '2'
                cmd += ['--gl-shots', os.path.join(out, 'shots'), '--gl-every', str(args.shots),
                        '--sky', 'night', '--draw-distance', 'max', '--fighter-models', 'off',
                        '--lights', 'on', '--stage-lights', 'on', '--effect-lights', 'on']
            running.append((stage, subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT), log, out))
        stage, process, log, out = running.pop(0)
        process.wait()
        log.close()
        mesh = os.path.join(args.dump, 'meshes', 'stage_%02d.bin' % stage)
        with open(os.path.join(out, 'log.txt'), errors='replace') as f:
            log_lines = f.readlines()
        said = [line.split("flame", 1)[1].strip() for line in log_lines
                if line.startswith("lights: stage %d's flame" % stage)]
        # what the mods' light files came to, and any line not used
        for line in log_lines:
            if line.startswith('lights: ') and "'s flame" not in line:
                print('port_lights: stage %2d: %s' % (stage, line.strip()))
        flames[stage] = said
        if not os.path.exists(mesh) and stage not in tried:
            tried.add(stage)  # the machine busy: once more
            waiting.append(stage)
        print('port_lights: stage %2d %s: %s, %d flames' % (
            stage, remaster.STAGE_NAMES[stage], 'mesh' if os.path.exists(mesh) else 'NO MESH',
            len(said)), flush=True)
        if args.shots:
            contact_sheet(os.path.join(out, 'shots'),
                          os.path.join(args.dump, 'shots', 'stage_%02d.png' % stage))
        shutil.rmtree(out)
    shutil.rmtree(work)
    path = os.path.join(args.dump, 'flames.txt')
    old = read_flames(args.dump)
    old.update({s: f for s, f in flames.items() if f or s not in old})
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        for stage in sorted(old):
            for line in old[stage]:
                f.write('%d %s\n' % (stage, line))
    return 0


def contact_sheet(folder, out):
    """The last pictures in `folder`, which are of the fight,
    six of them smaller on one sheet (Pillow; with none, nothing)."""
    try:
        from PIL import Image
    except ImportError:
        print('port_lights: no Pillow here: no sheet of pictures')
        return
    every = sorted(os.listdir(folder))
    names = [n for n in every if n.startswith('gl-') and n.endswith('.png') and n[3:9].isdigit()]
    if not names:
        print('port_lights: no pictures in %s' % folder)
        return
    # the fight's: the last of them
    names = names[-SHEET_PICTURES:]
    step = max(1, len(names) // SHEET_PICTURES)
    names = names[::step][:SHEET_PICTURES]
    pictures = [Image.open(os.path.join(folder, n)).convert('RGB') for n in names]
    w, h = SHEET_WIDTH, SHEET_WIDTH * pictures[0].height // pictures[0].width
    sheet = Image.new('RGB', (w * 2, h * ((len(pictures) + 1) // 2)))
    for i, picture in enumerate(pictures):
        sheet.paste(picture.resize((w, h), Image.LANCZOS), ((i % 2) * w, (i // 2) * h))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)


def read_flames(folder):
    """{stage: ['0, kind 1, at x y z', ...]} of the dump's flames.txt."""
    flames = {}
    path = os.path.join(folder, 'flames.txt')
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            for line in f:
                stage, rest = line.split(' ', 1)
                flames.setdefault(int(stage), []).append(rest.strip())
    return flames


# ---- find: what looks lit from within ----

# the points of a triangle looked at, as weights of its three corners
SAMPLES = [(1 / 3, 1 / 3, 1 / 3), (0.6, 0.2, 0.2), (0.2, 0.6, 0.2), (0.2, 0.2, 0.6),
           (0.45, 0.45, 0.1), (0.1, 0.45, 0.45), (0.45, 0.1, 0.45)]
CELL = 500  # world units a gathering's cell
# what counts as bright on a stage: its brightest this part of the area
# seen, and never less than this
BRIGHTEST = 0.03
LEAST_BRIGHT = 0.45


def area(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    x, y, z = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    return 0.5 * (x * x + y * y + z * z) ** 0.5


def bounds_of(vertices, pieces_from):
    ground = vertices[:pieces_from] or vertices
    least = [min(v[i] for v in ground) for i in range(3)]
    most = [max(v[i] for v in ground) for i in range(3)]
    return ([int((x // 500) * 500) for x in least], [int(-((-x) // 500) * 500) for x in most])


def find_stage(path):
    vertices, pieces_from, vram = read_mesh(path)
    samples = []
    for t in range(0, len(vertices) - 2, 3):
        a, b, c = vertices[t], vertices[t + 1], vertices[t + 2]
        size = area(a, b, c)
        if size <= 0:
            continue
        for wa, wb, wc in SAMPLES:
            u = a[3] * wa + b[3] * wb + c[3] * wc
            v = a[4] * wa + b[4] * wb + c[4] * wc
            rgb = texel(vram, a[5], a[6], a[7], a[8], a[9], u, v)
            if rgb is None:
                continue
            vertex = [a[10 + i] * wa + b[10 + i] * wb + c[10 + i] * wc for i in range(3)]
            samples.append((t, a, b, c, wa, wb, wc, size / len(SAMPLES), lit(rgb, vertex)))
    if not samples:
        return [], bounds_of(vertices, pieces_from)
    # the stage's brightest few in a hundred, by area, and never dim
    ranked = sorted(samples, key=lambda s: -max(s[8]))
    total = sum(s[7] for s in samples)
    bright, seen_area = 1.0, 0.0
    for s in ranked:
        seen_area += s[7]
        bright = max(s[8])
        if seen_area >= total * BRIGHTEST:
            break
    bright = max(bright, LEAST_BRIGHT)
    cells = {}
    for t, a, b, c, wa, wb, wc, size, rgb in samples:
        weight = glow(rgb, bright) * size
        if weight <= 0:
            continue
        x = a[0] * wa + b[0] * wb + c[0] * wc
        y = a[1] * wa + b[1] * wb + c[1] * wc
        z = a[2] * wa + b[2] * wb + c[2] * wc
        key = (int(x // CELL), int(y // CELL), int(z // CELL))
        cell = cells.setdefault(key, [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0])
        cell[0] += weight
        for i, value in enumerate((x, y, z)):
            cell[1 + i] += value * weight
        for i, value in enumerate(rgb):
            cell[4 + i] += value * weight
        cell[7] += 1 if t >= pieces_from else 0
    # neighbouring cells gathered into one
    gatherings = []
    seen = set()
    for key in sorted(cells, key=lambda k: -cells[k][0]):
        if key in seen:
            continue
        group = [key]
        seen.add(key)
        at = 0
        while at < len(group):
            kx, ky, kz = group[at]
            at += 1
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        other = (kx + dx, ky + dy, kz + dz)
                        if other in cells and other not in seen:
                            seen.add(other)
                            group.append(other)
        total = [sum(cells[k][i] for k in group) for i in range(8)]
        weight = total[0]
        gatherings.append({
            'weight': weight,
            'at': [int(total[1 + i] / weight) for i in range(3)],
            'rgb': [int(255 * total[4 + i] / weight) for i in range(3)],
            'piece': total[7] > 0,
            'cells': len(group),
        })
    gatherings.sort(key=lambda g: -g['weight'])
    return gatherings, bounds_of(vertices, pieces_from)


def find(args):
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import port_remaster_prove as remaster

    flames = read_flames(args.dump)
    os.makedirs(args.out, exist_ok=True)
    for stage in stage_list(args.stages):
        path = os.path.join(args.dump, 'meshes', 'stage_%02d.bin' % stage)
        if not os.path.exists(path):
            print('port_lights: stage %2d: no mesh in the dump' % stage)
            continue
        found, (least, most) = find_stage(path)
        if args.bounds:
            print('    %d: (%s, %s),  # %s' % (stage, least, most, remaster.STAGE_NAMES[stage]))
            continue
        name = 'stage_%02d' % stage
        lines = ['# A draft of %s (%s) by tools/port_lights.py find: what looks lit' % (
                     name, remaster.STAGE_NAMES[stage]),
                 '# from within, the largest first. Not a light file to ship as it is.',
                 '# bounds %s to %s' % (least, most), 'format = 1', '']
        for line in flames.get(stage, []):
            lines.append('# the game draws flame %s' % line)
        for i, g in enumerate(found[:args.top]):
            lines += ['', '# weight %.0f, %d cells, %s' % (
                          g['weight'], g['cells'], 'a piece' if g['piece'] else 'the ground'),
                      '[light found_%d]' % i,
                      'at = %d %d %d' % tuple(g['at']),
                      'color = #%02X%02X%02X' % tuple(g['rgb'])]
        with open(os.path.join(args.out, name + '.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        print('port_lights: stage %2d %-22s %d flames, %d gatherings%s' % (
            stage, remaster.STAGE_NAMES[stage], len(flames.get(stage, [])), len(found),
            ', the largest %.0f at %s' % (found[0]['weight'], found[0]['at']) if found else ''))
    return 0


# ---- check: the shipped files ----


def check(args):
    """Every shipped file parsed by the C (tests/port/lights.c) and its lights
    inside the stage's bounds: the test itself."""
    test = subprocess.run([sys.executable, os.path.join(ROOT, 'tests', 'run.py'), '--rebushido',
                           'port/lights'], cwd=ROOT)
    return test.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='what', required=True)
    d = sub.add_parser('dump')
    d.add_argument('--image', default=DEFAULT_IMAGE)
    d.add_argument('--program', default=DEFAULT_PROGRAM)
    d.add_argument('--dump', default=DEFAULT_DUMP)
    d.add_argument('--stages', default='0-%d' % (ALL_STAGES - 1))
    d.add_argument('--jobs', type=int, default=6)
    d.add_argument('--shots', type=int, default=0, metavar='EVERY',
                   help='and pictures of the fight every EVERY blanks, six on a sheet in shots/')
    d.add_argument('--mods', default=None, help='the mods to run with (none if not said)')
    f = sub.add_parser('find')
    f.add_argument('--dump', default=DEFAULT_DUMP)
    f.add_argument('--stages', default='0-%d' % (ALL_STAGES - 1))
    f.add_argument('--top', type=int, default=12)
    f.add_argument('--out', default=DEFAULT_FOUND)
    f.add_argument('--bounds', action='store_true', help='print BOUNDS for this file')
    c = sub.add_parser('check')
    c.add_argument('--folder', default=SHIPPED)
    args = ap.parse_args()
    return {'dump': dump, 'find': find, 'check': check}[args.what](args)


if __name__ == '__main__':
    sys.exit(main())
