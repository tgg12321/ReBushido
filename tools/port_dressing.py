#!/usr/bin/env python3
"""The stages' dressing (ROADMAP 17; docs/MODS.md, "Dressing"): its files
read, held to the stages' collision grids, and pictured.

    python3 tools/port_dressing.py check [--folder src/port/mods/dressing]
                                         [--grounds FOLDER] [--stages 0-37]
    python3 tools/port_dressing.py parse FILE...
    python3 tools/port_dressing.py rocks [--count 4]
    python  tools/port_dressing.py hashes [--stages 8,11,31]
    python  tools/port_dressing.py shots --stages 8 [--times stage,night]
                                         [--out tmp/stages17] [--program EXE]

`check` reads every dressing file of a mod (the shipped one if nothing
says) and fails where:

- a line is not understood (the same grammar as src/port/dressing.c, which
  tests/port/dressing.c holds to these files too);
- a block lies outside its stage's bounds (tools/port_lights.py, BOUNDS);
- anything that looks solid -- a mesh, a bush, foliage marked `solid` --
  could stand where a fighter can walk: where the stage's collision grid
  has a floor a fighter gets to from the game's own starts (the walk of
  tools/arena_validate.py over the grid, tools/ground_cast.py), with no
  wall to stop him. A mesh standing in for a piece (`piece`) is fitted
  into the piece's own box, and the piece is what the grid was made for,
  so it is not walked against.

The grids are the stages' ground files, read from your disc (`rebushido
--dump-stages`, into `--grounds`, tmp/dressing/grounds if nothing says);
with no disc and no grounds the walk is skipped and says so.

`parse` prints each block of the files as the check reads it.

`shots` runs the window's program (on Windows, the card's, a window never
shown) straight into a fight of the CPU against the CPU on each stage asked
for, at 2,560 by 1,440 with the Remastered picture, the dressing off and
then on, at each time of day asked for, and keeps pictures of it: the
before and after the owner asked to see (tmp/stages17/).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

SHIPPED = os.path.join(ROOT, 'src', 'port', 'mods', 'dressing')
GROUNDS = os.path.join(ROOT, 'tmp', 'dressing', 'grounds')
ALL_STAGES = 38
FORMAT = 1
MOST = 48
AREAS = 8
NAME_MOST = 31
COUNT_MOST = 20000
HEIGHT_MOST = 20000
WORLD_MOST = 40000
KINDS = ['grass', 'bush', 'reeds', 'flowers', 'water', 'particles', 'mesh', 'wind', 'shafts']
PARTICLES = ['dust', 'leaves', 'embers', 'snow', 'petals', 'fireflies', 'rain', 'smoke', 'motes',
             'spray', 'mist']
ONS = ['any', 'green', 'earth', 'sand', 'stone', 'snow', 'blue']
TIMES = ['dawn', 'day', 'dusk', 'night']
# the walk (tools/arena_validate.py's): its step, and how far about a solid
# thing's place no point walked may be
STEP = 250
# how far past its stage's bounds a block's rectangles may reach
MARGIN = 2000

# a block as a file starts it, by kind (src/port/dressing.c)
DEFAULTS = {
    'grass': dict(count=4000, height=(110, 240), solid=False),
    'flowers': dict(count=4000, height=(110, 240), solid=False),
    'bush': dict(count=40, height=(350, 700), solid=True),
    'reeds': dict(count=600, height=(500, 900), solid=False),
    'particles': dict(count=300, height=(24, 24), solid=False),
    'mesh': dict(count=0, height=(0, 0), solid=True),
    'water': dict(count=0, height=(0, 0), solid=False),
    'wind': dict(count=0, height=(0, 0), solid=False),
    'shafts': dict(count=120, height=(2000, 2000), solid=False),
}


class Problem(Exception):
    pass


def number(word):
    if not re.fullmatch(r'[-+]?(\d+(\.\d*)?|\.\d+)', word):
        raise Problem('not a number: %s' % word)
    return float(word)


def wholes(value, count):
    words = value.split()
    if len(words) != count:
        raise Problem('%d numbers wanted' % count)
    return [int(number(w)) for w in words]


def color(value):
    words = value.split()
    if len(words) == 1 and re.fullmatch(r'#[0-9a-fA-F]{6}', words[0]):
        return tuple(int(words[0][1 + 2 * i:3 + 2 * i], 16) for i in range(3))
    rgb = wholes(value, 3)
    if any(c < 0 or c > 255 for c in rgb):
        raise Problem('a colour is #RRGGBB or three numbers of 0 to 255')
    return tuple(rgb)


def strip_comment(line):
    """A line without its comment: `#` at its start or after a space, not a
    colour's."""
    for m in re.finditer('#', line):
        at = m.start()
        if at > 0 and line[at - 1] not in ' \t':
            continue
        if re.match(r'#[0-9a-fA-F]{6}(\s|$)', line[at:]):
            continue
        return line[:at]
    return line


def new_block(kind, name):
    d = DEFAULTS[kind]
    return dict(kind=kind, name=name.lower(), areas=[], avoids=[], count=d['count'],
                height=d['height'], solid=d['solid'], color=None, on='any', at=(0, 0, 0),
                piece=-1, file=None, subkind='dust', scale=1.0, turn=0, times=None, top=3000)


def set_value(block, name, value):
    name = name.lower()
    if name in ('area', 'avoid'):
        r = wholes(value, 4)
        if r[0] >= r[2] or r[1] >= r[3]:
            raise Problem('a rectangle is X0 Z0 X1 Z1, the least first')
        if any(abs(v) > WORLD_MOST for v in r):
            raise Problem('a rectangle lies within 40,000 of the middle')
        target = block['areas' if name == 'area' else 'avoids']
        if len(target) >= AREAS:
            raise Problem('more rectangles than a block holds (8)')
        target.append(tuple(r))
    elif name == 'count':
        block['count'] = wholes(value, 1)[0]
        if not 0 <= block['count'] <= COUNT_MOST:
            raise Problem('`count` is 0 to 20,000')
    elif name in ('color', 'colour'):
        block['color'] = None if value.strip().lower() == 'ground' else color(value)
    elif name == 'tint':
        color(value)
    elif name == 'times':
        words = value.lower().split()
        if not words or any(w not in TIMES + ['always'] for w in words):
            raise Problem('`times` is dawn, day, dusk, night or always')
        block['times'] = [w for w in words if w != 'always'] or None
    elif name == 'at':
        block['at'] = tuple(wholes(value, 3))
    elif name == 'sit':
        if value.strip().lower() not in ('yes', 'on', 'no', 'off'):
            raise Problem('`sit` is yes or no')
    elif name == 'solid':
        word = value.strip().lower()
        if word not in ('yes', 'on', 'no', 'off'):
            raise Problem('`solid` is yes or no')
        block['solid'] = word in ('yes', 'on')
    elif name in ('height', 'size'):
        words = value.split()
        h = wholes(value, len(words)) if len(words) in (1, 2) else None
        if h is None or (len(h) == 2 and block['kind'] != 'shafts' and h[0] > h[1]) or max(h) > HEIGHT_MOST or min(h) < 1:
            raise Problem('`height` is one or two numbers, the least first')
        block['height'] = (h[0], h[-1])
    elif name == 'width':
        w = number(value.strip())
        if not 0 < w <= 10:
            raise Problem('`width` is above 0, up to 10')
    elif name == 'on':
        word = value.strip().lower()
        if block['kind'] == 'particles' and word in ('flames', 'area'):
            pass
        elif word not in ONS:
            raise Problem('`on` is any, green, earth, sand, stone or snow')
        block['on'] = word
    elif name == 'level':
        if not 0 <= number(value.strip()) <= 1:
            raise Problem('`level` is 0 to 1')
    elif name == 'heights':
        h = wholes(value, 2)
        if h[0] > h[1]:
            raise Problem('`heights` is the least y first')
    elif name == 'sway':
        if not 0 <= number(value.strip()) <= 4:
            raise Problem('`sway` is 0 to 4')
    elif name == 'y':
        y = wholes(value, 1)[0]
        block['at'] = (block['at'][0], y, block['at'][2])
    elif name == 'turn':
        block['turn'] = wholes(value, 1)[0]
        if abs(block['turn']) > 360:
            raise Problem('`turn` is -360 to 360')
    elif name == 'scale':
        block['scale'] = number(value.strip())
        if not 0 < block['scale'] <= 100:
            raise Problem('`scale` is above 0, up to 100')
    elif name == 'piece':
        block['piece'] = wholes(value, 1)[0]
        if not -1 <= block['piece'] <= 63:
            raise Problem('`piece` is 0 to 63, or -1')
    elif name == 'file':
        words = value.split()
        if len(words) != 1 or len(words[0]) > NAME_MOST:
            raise Problem('`file` is one word')
        block['file'] = words[0].lower()
    elif name in ('clarity', 'gusts'):
        if not 0 <= number(value.strip()) <= 1:
            raise Problem('`%s` is 0 to 1' % name)
    elif name in ('waves', 'strength'):
        if not 0 <= number(value.strip()) <= 4:
            raise Problem('`%s` is 0 to 4' % name)
    elif name == 'top':
        block['top'] = wholes(value, 1)[0]
        if not 0 <= block['top'] <= HEIGHT_MOST:
            raise Problem('`top` is 0 to 20,000')
    elif name == 'kind':
        word = value.strip().lower()
        if block['kind'] != 'particles' or word not in PARTICLES:
            raise Problem("`kind` is a particles block's")
        block['subkind'] = word
    elif name == 'direction':
        if abs(wholes(value, 1)[0]) > 360:
            raise Problem('`direction` is -360 to 360')
    elif name in ('puddles', 'reflect', 'sky'):
        if not 0 <= number(value.strip()) <= 1:
            raise Problem('`%s` is 0 to 1' % name)
    elif name in ('rain', 'foam', 'glow'):
        if not 0 <= number(value.strip()) <= 4:
            raise Problem('`%s` is 0 to 4' % name)
    elif name == 'flow':
        way, speed = wholes(value, 2)
        if abs(way) > 360 or not 0 <= speed <= 4000:
            raise Problem('`flow` is a way, -360 to 360, and a speed, 0 to 4,000')
    elif name == 'shallow':
        color(value)
    elif name == 'light':
        words = value.split()
        if not 1 <= len(words) <= 4 or any(len(w) > NAME_MOST for w in words):
            raise Problem("`light` is up to 4 names of the stage's lights")
        block['lights'] = [w.lower() for w in words]
    else:
        raise Problem('not a word a dressing block has: %s' % name)


def parse(text, blocks=None):
    """(blocks, problems): a file over the blocks before it."""
    blocks = [] if blocks is None else blocks
    problems = []
    current = None
    skipping = False
    for number_of, raw in enumerate(text.split('\n'), 1):
        line = strip_comment(raw).strip()
        if not line:
            continue
        try:
            if line.startswith('['):
                current = None
                skipping = True
                m = re.fullmatch(r'\[\s*(\S+)(?:\s+(\S+))?\s*\]', line)
                if not m or m.group(1).lower() not in KINDS or len(m.group(2) or '') > NAME_MOST:
                    raise Problem('a block begins `[KIND NAME]`')
                kind = m.group(1).lower()
                name = (m.group(2) or kind).lower()
                for b in blocks:
                    if b['kind'] == kind and b['name'] == name:
                        current = b
                if current is None:
                    if len(blocks) >= MOST:
                        raise Problem('more blocks than a stage holds (48)')
                    current = new_block(kind, name)
                    blocks.append(current)
                skipping = False
            elif line.lower() == 'clear':
                blocks.clear()
                current = None
            elif line.lower().startswith('remove '):
                name = line.split()[1].lower()
                found = [b for b in blocks if b['name'] == name]
                if not found:
                    raise Problem('no block of that name to remove')
                blocks.remove(found[0])
                current = None
            elif '=' in line:
                name, value = line.split('=', 1)
                name = name.strip()
                if name.lower() == 'format':
                    if wholes(value, 1)[0] != FORMAT:
                        raise Problem('`format = 1`')
                elif skipping:
                    pass
                elif current is None:
                    raise Problem("a block's line before any `[KIND NAME]`")
                else:
                    set_value(current, name, value)
            else:
                raise Problem('not `name = value`, `[KIND NAME]`, `clear` nor `remove NAME`')
        except Problem as why:
            problems.append('line %d: %s' % (number_of, why))
    return blocks, problems


def stage_files(folder):
    """{stage: path} of a mod's dressing files."""
    out = {}
    base = os.path.join(folder, 'dressing')
    for here, _, names in os.walk(base):
        for name in names:
            m = re.fullmatch(r'stage_(\d\d)\.txt', name.lower())
            if m:
                out[int(m.group(1))] = os.path.join(here, name)
    return out


def stage_list(text):
    stages = []
    for piece in text.split(','):
        if '-' in piece:
            first, last = piece.split('-')
            stages += list(range(int(first), int(last) + 1))
        elif piece:
            stages.append(int(piece))
    return [s for s in stages if 0 <= s < ALL_STAGES]


# ---- the grounds and the walk ----

def starts_of(stage):
    """The game's own starts of a stage (src/main/resource_data.c)."""
    path = os.path.join(ROOT, 'src', 'main', 'resource_data.c')
    with open(path, encoding='utf-8') as f:
        text = f.read()
    table = text[text.index('g_fight_start_positions'):]
    table = table[table.index('= {') + 3:]
    numbers = [int(n) for n in re.findall(r'-?\d+', table[:table.index('};')])]
    per_stage = 4 * 2 * 3
    mine = numbers[stage * per_stage:(stage + 1) * per_stage]
    return [mine[i:i + 3] for i in range(0, len(mine), 3)]


def find_grounds(folder, image, program):
    """The ground files' folder, dumped from the disc if it is not there:
    None where it cannot be had."""
    if os.path.exists(os.path.join(folder, 's08.dat')):
        return folder
    if not os.path.exists(image) or not os.path.exists(program):
        return None
    os.makedirs(folder, exist_ok=True)
    subprocess.run([program, '--image', image, '--dump-stages', folder, '--frames', '1'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    return folder if os.path.exists(os.path.join(folder, 's08.dat')) else None


class Quiet:
    def ok(self, what):
        pass

    def fail(self, what):
        pass

    def check(self, good, what):
        return good


def walked_points(grounds, stage):
    """{(x, z)} of the places a fighter gets to on a stage, every STEP
    units, from the game's own starts."""
    import arena_validate
    import ground_cast
    import stage_ground

    path = os.path.join(grounds, 's%02d.dat' % stage)
    with open(path, 'rb') as f:
        ground = stage_ground.read(f.read())
    grid = ground_cast.Grid(stage_ground.section(ground, 'collision'))
    seen, _ = arena_validate.walk(grid, starts_of(stage), STEP, Quiet())
    return {(ix * STEP, iz * STEP) for ix, iz, _ in seen}


def obj_box(folder, key):
    """The box of a mesh file's points, x and z: (x0, z0, x1, z1), or None."""
    for here, _, names in os.walk(os.path.join(folder, 'dressing')):
        for name in names:
            if name.lower() == key + '.obj':
                lo = [1e30] * 3
                hi = [-1e30] * 3
                with open(os.path.join(here, name), encoding='utf-8', errors='replace') as f:
                    for line in f:
                        if line.startswith('v '):
                            p = [float(w) for w in line.split()[1:4]]
                            lo = [min(a, b) for a, b in zip(lo, p)]
                            hi = [max(a, b) for a, b in zip(hi, p)]
                return lo, hi
    return None


def solid_footprints(block, folder):
    """The rectangles x0 z0 x1 z1 a solid-looking block may stand in, each
    grown by how far it reaches about its place."""
    if not block['solid'] or block['kind'] in ('water', 'particles', 'wind', 'grass', 'flowers'):
        if not (block['solid'] and block['kind'] in ('grass', 'flowers')):
            return []
    if block['kind'] == 'mesh':
        if block['piece'] >= 0:
            return []
        box = obj_box(folder, block['file'] or '')
        if box is None:
            return []
        lo, hi = box
        import math
        reach = math.hypot(max(abs(lo[0]), abs(hi[0])), max(abs(lo[2]), abs(hi[2]))) * block['scale']
        x, _, z = block['at']
        return [(x - reach, z - reach, x + reach, z + reach)]
    # foliage: its areas, and as far as one of its things reaches
    reach = block['height'][1] * 0.65 if block['kind'] == 'bush' else 60
    return [(a[0] - reach, a[1] - reach, a[2] + reach, a[3] + reach) for a in block['areas']]


def check(args):
    import port_lights

    folder = os.path.abspath(args.folder)
    files = stage_files(folder)
    stages = set(stage_list(args.stages))
    failed = 0
    grounds = find_grounds(os.path.abspath(args.grounds), args.image, args.program)
    if grounds is None:
        print('port_dressing: no ground files (no disc image or no build/port/rebushido): '
              'the walk is skipped')
    for stage in sorted(files):
        if stage not in stages:
            continue
        with open(files[stage], encoding='utf-8') as f:
            blocks, problems = parse(f.read())
        rel = os.path.relpath(files[stage], ROOT).replace(os.sep, '/')
        for p in problems:
            print('FAIL %s: %s' % (rel, p))
            failed += 1
        lo, hi = port_lights.BOUNDS[stage]
        for b in blocks:
            for r in b['areas']:
                inside = (lo[0] - MARGIN <= r[0] and r[2] <= hi[0] + MARGIN and
                          lo[2] - MARGIN <= r[1] and r[3] <= hi[2] + MARGIN)
                if not inside:
                    print('FAIL %s: %s %s: area %s is outside stage %d (%s to %s)'
                          % (rel, b['kind'], b['name'], r, stage, lo, hi))
                    failed += 1
            if b['kind'] == 'mesh' and b['file'] is None:
                print('FAIL %s: mesh %s has no `file`' % (rel, b['name']))
                failed += 1
            if b['kind'] == 'mesh' and b['piece'] < 0 and obj_box(folder, b['file'] or '') is None:
                print('FAIL %s: mesh %s: no %s.obj in the mod' % (rel, b['name'], b['file']))
                failed += 1
        solid = [(b, f) for b in blocks for f in solid_footprints(b, folder)]
        if grounds is None or not solid:
            print('ok   %s: %d blocks%s' % (rel, len(blocks), ', nothing solid' if not solid else ''))
            continue
        walked = walked_points(grounds, stage)
        bad = 0
        for b, (x0, z0, x1, z1) in solid:
            met = [(x, z) for x, z in walked if x0 <= x <= x1 and z0 <= z <= z1]
            if met:
                print('FAIL %s: %s %s looks solid and stands where a fighter walks: '
                      '%d points walked in %s, as (%d, %d)'
                      % (rel, b['kind'], b['name'], len(met), (x0, z0, x1, z1), met[0][0], met[0][1]))
                bad += 1
        failed += bad
        if not bad:
            print('ok   %s: %d blocks, %d solid-looking held off the %d points a fighter walks'
                  % (rel, len(blocks), len(solid), len(walked)))
    print('port_dressing: %d stages, %s' % (len([s for s in files if s in stages]),
                                            'every one holds' if not failed else '%d failures' % failed))
    return 1 if failed else 0


def parse_command(args):
    status = 0
    for path in args.files:
        with open(path, encoding='utf-8') as f:
            blocks, problems = parse(f.read())
        for p in problems:
            print('problem %s %s' % (path, p))
            status = 1
        for b in blocks:
            print('block %s %s %s count %d solid %d areas %d' % (
                path, b['kind'], b['name'], b['count'], int(b['solid']), len(b['areas'])))
    return status


def walkable_map(args):
    """A picture of where a fighter walks on a stage, over its top-down map
    (for placing things by hand)."""
    from PIL import Image, ImageDraw
    grounds = find_grounds(os.path.abspath(args.grounds), args.image, args.program)
    for stage in stage_list(args.stages):
        walked = walked_points(grounds, stage)
        base = os.path.join(args.maps, 'stage_%02d.png' % stage)
        import port_lights
        lo, hi = port_lights.BOUNDS[stage]
        img = Image.open(base).convert('RGB') if os.path.exists(base) else \
            Image.new('RGB', (int((hi[0] - lo[0]) / 40) + 1, int((hi[2] - lo[2]) / 40) + 1))
        d = ImageDraw.Draw(img, 'RGBA')
        for x, z in walked:
            px = (x - lo[0]) / 40.0
            pz = (z - lo[2]) / 40.0
            d.rectangle([px - 3, pz - 3, px + 3, pz + 3], fill=(255, 0, 0, 70))
        out = os.path.join(args.maps, 'walk_%02d.png' % stage)
        img.save(out)
        xs = [p[0] for p in walked]
        zs = [p[1] for p in walked]
        print('stage %d: %d points walked, x %d to %d, z %d to %d -> %s' % (
            stage, len(walked), min(xs), max(xs), min(zs), max(zs), out))
    return 0


# ---- the game's state, with the dressing and without ----

HASH_SETTINGS = """graphics = remastered
stage_kind = remastered
dressing = {dressing}
frame_rate = 60
music = off
"""


def hashes(args):
    """Each stage's fight of the CPU, the dressing on and off, on the window's
    program: the game's state printed at every blank (--trace) must be the
    same, line for line; and the replay made with the dressing on (its state's
    hash every 30 frames) must play back to the same hashes on the program
    with no picture at all (--replay-check)."""
    program = os.path.abspath(args.program)
    headless = os.path.join(os.path.dirname(program), 'rebushido-headless' +
                            ('.exe' if program.endswith('.exe') else ''))
    work = os.path.join(ROOT, 'tmp', 'dressing', 'hashes')
    failed = 0
    for stage in stage_list(args.stages):
        traces = {}
        replays = []
        for dressing in ('on', 'off'):
            folder = os.path.join(work, 'stage%02d-%s' % (stage, dressing))
            if os.path.isdir(folder):
                shutil.rmtree(folder)
            os.makedirs(os.path.join(folder, 'replays'))
            config = os.path.join(folder, 'rebushido.cfg')
            with open(config, 'w', encoding='utf-8', newline='\n') as f:
                f.write(HASH_SETTINGS.format(dressing=dressing))
            cmd = [program, '--hidden', '--image', os.path.abspath(args.image), '--config', config,
                   '--mods', os.path.abspath(args.mods), '--out', folder, '--no-cards', '--fast',
                   '--no-fps', '--window', '1280x720', '--frames', str(args.frames),
                   '--log', os.path.join(folder, 'log.txt'), '--replays', os.path.join(folder, 'replays'),
                   '--trace', '--fight', str(stage)] + args.fighters.split() + ['--fight-cpu']
            env = dict(os.environ, SDL_AUDIODRIVER='dummy')
            env.pop('SDL_VIDEODRIVER', None)
            done = subprocess.run(cmd, env=env, capture_output=True, text=True, errors='replace',
                                  stdin=subprocess.DEVNULL, timeout=900)
            said = (done.stdout + done.stderr).splitlines()
            traces[dressing] = [line for line in said if line.startswith('trace ')]
            if dressing == 'on':
                kept = sorted(os.listdir(os.path.join(folder, 'replays')))
                replays = [os.path.join(folder, 'replays', name) for name in kept]
                if not any('grown from' in line for line in said):
                    print('FAIL stage %d: the dressing was not grown with it on' % stage)
                    failed += 1
        same = traces['on'] == traces['off'] and len(traces['on']) > 100
        print('%s stage %d: the state at %d blanks the same with the dressing on and off'
              % ('ok  ' if same else 'FAIL', stage, len(traces['on'])))
        failed += not same
        if not replays:
            print('FAIL stage %d: no replay to check' % stage)
            failed += 1
        # each fight's replay made with the dressing on, played with it off:
        # its state's hash every 30 frames must be the one recorded
        config = os.path.join(work, 'off.cfg')
        with open(config, 'w', encoding='utf-8', newline='\n') as f:
            f.write(HASH_SETTINGS.format(dressing='off'))
        for replay in replays:
            done = subprocess.run([program, '--hidden', '--config', config, '--mods', os.path.abspath(args.mods),
                                   '--image', os.path.abspath(args.image), '--no-cards', '--no-fps',
                                   '--window', '640x360', '--replay-check', replay],
                                  capture_output=True, text=True, errors='replace', env=env,
                                  stdin=subprocess.DEVNULL, timeout=900)
            said = [line for line in (done.stdout + done.stderr).splitlines() if 'hashes the same' in line]
            good = done.returncode == 0 and bool(said)
            print('%s stage %d: a replay made with the dressing on, played with it off: %s'
                  % ('ok  ' if good else 'FAIL', stage, said[-1].split(': ', 2)[-1] if said else done.returncode))
            failed += not good
    print('port_dressing: %s' % ('the game\'s state is the dressing\'s to nothing' if not failed
                                 else '%d failures' % failed))
    return 1 if failed else 0


# ---- meshes made here ----

def rock_obj(seed, radius=420.0, moss=(0.30, 0.38, 0.20), stone=(0.42, 0.41, 0.38)):
    """A rock as a Wavefront OBJ's text: an icosahedron cut twice, each point
    pushed out or in by a few waves of the seed's own, flattened below and
    sunk a little into the ground; its colours stone, mossed where it faces
    up. In the world's units, y down. Procedural: nothing of anyone's art."""
    import math
    import random

    rnd = random.Random(seed)
    t = (1 + 5 ** 0.5) / 2
    points = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t),
              (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    points = [tuple(c / math.sqrt(1 + t * t) for c in p) for p in points]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
             (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5),
             (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(2):
        middle = {}
        new = []

        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in middle:
                p = [(points[a][k] + points[b][k]) / 2 for k in range(3)]
                n = math.sqrt(sum(c * c for c in p))
                points.append(tuple(c / n for c in p))
                middle[key] = len(points) - 1
            return middle[key]
        for a, b, c in faces:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            new += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = new
    waves = [(rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(1.5, 4.0),
              rnd.uniform(0, 6.28), rnd.uniform(0.05, 0.16)) for _ in range(6)]
    stretch = (rnd.uniform(0.9, 1.35), rnd.uniform(0.55, 0.85), rnd.uniform(0.8, 1.2))
    out = []
    lines = ['# A rock made by tools/port_dressing.py rocks (seed %d): procedural, '
             'the world\'s units, y down' % seed]
    for x, y, z in points:
        r = 1.0 + sum(a * math.sin(f * (x * dx + y * dy + z * dz) + p) for dx, dy, dz, f, p, a in waves)
        px, py, pz = x * r * stretch[0] * radius, y * r * stretch[1] * radius, z * r * stretch[2] * radius
        # its foot flattened, sunk a fifth into the ground (y down: up is -y)
        up = py
        up = max(up, -0.2 * radius)
        out.append((px, -up, pz))
        facing = max(0.0, (y + 0.2) / 1.2)
        grain = rnd.uniform(-0.04, 0.04)
        rgb = [stone[k] + (moss[k] - stone[k]) * facing * facing + grain for k in range(3)]
        lines.append('v %.1f %.1f %.1f %.3f %.3f %.3f' % (px, -up, pz, *[min(1, max(0, c)) for c in rgb]))
    for a, b, c in faces:
        # seen from outside, the world's y down turning the winding
        lines.append('f %d %d %d' % (a + 1, c + 1, b + 1))
    return '\n'.join(lines) + '\n'


def grave_obj(width=464.0, height=1150.0, depth=464.0, stone=(0.50, 0.49, 0.47),
              moss=(0.33, 0.40, 0.25), seed=11):
    """A Japanese gravestone as a Wavefront OBJ's text, `width` across,
    `depth` deep and `height` tall, its foot at y 0 and its top at -height
    (the world's y runs down): a footing stepped twice, and the tall stone with its edges
    chamfered and its top a low pyramid; stone, darker and mossed where it
    faces up and at the foot. Procedural: nothing of anyone's art."""
    import random

    rnd = random.Random(seed)
    lines = ['# A gravestone made by tools/port_dressing.py meshes: procedural, '
             'the world\'s units, y down']
    count = [0]

    def colour(up, low=0.0):
        grain = rnd.uniform(-0.025, 0.025)
        shade = 1.0 - 0.18 * low
        return [min(1, max(0, (stone[k] + (moss[k] - stone[k]) * up * 0.5) * shade + grain))
                for k in range(3)]

    def vertex(x, y, z, rgb):
        lines.append('v %.1f %.1f %.1f %.3f %.3f %.3f' % (x * width, -y * height, z * depth, *rgb))
        count[0] += 1
        return count[0]

    def outline(half_x, half_z, chamfer):
        c = chamfer
        return [(-half_x + c, -half_z), (half_x - c, -half_z), (half_x, -half_z + c), (half_x, half_z - c),
                (half_x - c, half_z), (-half_x + c, half_z), (-half_x, half_z - c), (-half_x, -half_z + c)]

    def prism(y0, y1, half_x, half_z, chamfer, low):
        shape = outline(half_x, half_z, chamfer)
        bottom = [vertex(x, y0, z, colour(0.0, low)) for x, z in shape]
        top_side = [vertex(x, y1, z, colour(0.0, low * 0.5)) for x, z in shape]
        n = len(shape)
        for i in range(n):
            lines.append('f %d %d %d %d' % (bottom[i], bottom[(i + 1) % n], top_side[(i + 1) % n], top_side[i]))
        lines.append('f ' + ' '.join(str(v) for v in reversed(bottom)))
        top = [vertex(x, y1, z, colour(1.0)) for x, z in shape]
        lines.append('f ' + ' '.join(str(v) for v in top))

    # the footing, stepped twice
    prism(0.00, 0.12, 0.50, 0.50, 0.02, 1.0)
    prism(0.12, 0.24, 0.40, 0.40, 0.02, 0.6)
    # the tall stone, its edges chamfered
    # the stone as wide of the footing as a square one, wider where the
    # footing is long
    stele = 0.42 - 0.17 * min(1.0, depth / width)
    prism(0.24, 0.93, stele, 0.25, 0.035, 0.2)
    # its top: a low pyramid
    shape = outline(stele, 0.25, 0.035)
    rim = [vertex(x, 0.93, z, colour(0.9)) for x, z in shape]
    peak = vertex(0.0, 1.0, 0.0, colour(1.0))
    for i in range(len(rim)):
        lines.append('f %d %d %d' % (rim[i], rim[(i + 1) % len(rim)], peak))
    return '\n'.join(lines) + '\n'


def rocks(args):
    folder = os.path.join(args.folder, 'dressing', 'meshes')
    os.makedirs(folder, exist_ok=True)
    for n in range(args.count):
        path = os.path.join(folder, 'rock_%d.obj' % n)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(rock_obj(1000 + n))
        print(path)
    return 0


# the meshes the program makes that stand in for a stage's piece (`piece`),
# and their proportions, width height depth (gl_dressing.c fits each into
# its piece's box, of the piece's own proportions within a quarter)
GRAVES = {'grave': (464.0, 1150.0, 464.0), 'grave_wide': (1732.0, 1150.0, 464.0)}


def meshes(args):
    """The scenery meshes the program ships, made here: the gravestones
    that stand in for the cemetery's own (`piece`)."""
    folder = os.path.join(args.folder, 'dressing', 'meshes')
    os.makedirs(folder, exist_ok=True)
    for name, (w, h, d) in sorted(GRAVES.items()):
        path = os.path.join(folder, name + '.obj')
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(grave_obj(w, h, d))
        print(path)
    return 0


# ---- pictures ----

SHOT_SETTINGS = """graphics = remastered
draw_distance = max
frame_rate = 60
antialiasing = 2
stage_kind = remastered
dressing = {dressing}
time_of_day = {time}
music = off
"""


def raw_to_png(raw, png):
    """A picture --tap-window kept (sdl_window.c: four words, then the
    window's pixels, a row after a row) as a PNG."""
    import struct
    from PIL import Image

    with open(raw, 'rb') as f:
        data = f.read()
    _, w, h, _ = struct.unpack('<4I', data[:16])
    # the window's pixels as port_GlReadWindow keeps them: ARGB words, so
    # blue, green, red, alpha in memory
    image = Image.frombytes('RGBA', (w, h), data[16:16 + 4 * w * h], 'raw', 'BGRA').convert('RGB')
    image.save(png)


def shots(args):
    import card_lock

    program = os.path.abspath(args.program)
    if not os.path.exists(program):
        print('port_dressing: no %s (make port-sdl-windows)' % program)
        return 1
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    work = os.path.join(ROOT, 'tmp', 'dressing', 'shots')
    card_lock.hold(False, 'port_dressing shots')
    keep = [int(b) for b in args.keep.split(',')]
    for stage in stage_list(args.stages):
        for when in args.times.split(','):
            for dressing in ('off', 'on'):
                if args.only_on and dressing == 'off':
                    continue
                name = 'stage%02d-%s-%s' % (stage, when, 'after' if dressing == 'on' else 'before')
                folder = os.path.join(work, name)
                if os.path.isdir(folder):
                    shutil.rmtree(folder)
                os.makedirs(folder)
                config = os.path.join(folder, 'rebushido.cfg')
                with open(config, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(SHOT_SETTINGS.format(dressing=dressing, time=when))
                tap = os.path.join(folder, 'tap')
                os.makedirs(tap)
                cmd = [program, '--hidden', '--image', os.path.abspath(args.image), '--config', config,
                       '--mods', os.path.abspath(args.mods), '--out', folder, '--no-cards', '--fast',
                       '--no-fps', '--window', args.window, '--frames', str(max(keep) + 2),
                       '--log', os.path.join(folder, 'log.txt'),
                       '--tap-window', tap, '--tap-every', str(args.every),
                       '--fight', str(stage)] + args.fighters.split() + ['--fight-cpu']
                env = dict(os.environ, SDL_AUDIODRIVER='dummy')
                env.pop('SDL_VIDEODRIVER', None)
                began = time.time()
                subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               stdin=subprocess.DEVNULL, timeout=600, check=False)
                taken = sorted(os.listdir(tap))
                kept = 0
                for picture in taken:
                    m = re.fullmatch(r'window-(\d+)\.raw', picture)
                    if m and int(m.group(1)) in keep:
                        raw_to_png(os.path.join(tap, picture),
                                   os.path.join(out, '%s-%05d.png' % (name, int(m.group(1)))))
                        kept += 1
                print('port_dressing: %s: %d pictures of %d kept (%.0f s)' % (
                    name, kept, len(taken), time.time() - began), flush=True)
                if not args.keep_work:
                    shutil.rmtree(folder)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='command', required=True)
    c = sub.add_parser('check')
    c.add_argument('--folder', default=SHIPPED)
    c.add_argument('--grounds', default=GROUNDS)
    c.add_argument('--stages', default='0-%d' % (ALL_STAGES - 1))
    c.add_argument('--image', default=os.path.join(ROOT, 'Bushido Blade 2 (USA).cue'))
    c.add_argument('--program', default=os.path.join(ROOT, 'build', 'port', 'rebushido'))
    p = sub.add_parser('parse')
    p.add_argument('files', nargs='+')
    w = sub.add_parser('walk')
    w.add_argument('--grounds', default=GROUNDS)
    w.add_argument('--stages', default='8')
    w.add_argument('--maps', default=os.path.join(ROOT, 'tmp', 'dressing', 'maps'))
    w.add_argument('--image', default=os.path.join(ROOT, 'Bushido Blade 2 (USA).cue'))
    w.add_argument('--program', default=os.path.join(ROOT, 'build', 'port', 'rebushido'))
    hs = sub.add_parser('hashes')
    hs.add_argument('--stages', default='8,11,31')
    hs.add_argument('--frames', type=int, default=4000)
    hs.add_argument('--program', default=os.path.join(ROOT, 'build', 'port-sdl', 'windows', 'rebushido.exe'))
    hs.add_argument('--image', default=os.path.join(ROOT, 'Bushido Blade 2 (USA).cue'))
    hs.add_argument('--mods', default=os.path.join(ROOT, 'build', 'port-sdl', 'windows', 'mods'))
    hs.add_argument('--fighters', default='kannuki mikado')
    m = sub.add_parser('meshes')
    m.add_argument('--folder', default=SHIPPED)
    r = sub.add_parser('rocks')
    r.add_argument('--folder', default=SHIPPED)
    r.add_argument('--count', type=int, default=4)
    s = sub.add_parser('shots')
    s.add_argument('--stages', default='8')
    s.add_argument('--times', default='stage,night')
    s.add_argument('--out', default=os.path.join(ROOT, 'tmp', 'stages17'))
    s.add_argument('--program', default=os.path.join(ROOT, 'build', 'port-sdl', 'windows', 'rebushido.exe'))
    s.add_argument('--image', default=os.path.join(ROOT, 'Bushido Blade 2 (USA).cue'))
    s.add_argument('--mods', default=os.path.join(ROOT, 'tmp', 'dressing', 'mods'))
    s.add_argument('--window', default='2560x1440')
    s.add_argument('--fighters', default='kannuki mikado')
    s.add_argument('--every', type=int, default=60)
    s.add_argument('--keep', default='720,960')
    s.add_argument('--only-on', action='store_true')
    s.add_argument('--keep-work', action='store_true')
    args = ap.parse_args()
    if args.command == 'check':
        return check(args)
    if args.command == 'parse':
        return parse_command(args)
    if args.command == 'hashes':
        return hashes(args)
    if args.command == 'rocks':
        return rocks(args)
    if args.command == 'meshes':
        return meshes(args)
    if args.command == 'walk':
        return walkable_map(args)
    return shots(args)


if __name__ == '__main__':
    sys.exit(main())
