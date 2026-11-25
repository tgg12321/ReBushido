#!/usr/bin/env python3
"""Holds a custom arena (`.arena`, tools/arena_compile.py) to what a fight
on it needs, and fails if it falls short (docs/MODS.md, "Custom arenas",
"The validator").

    python3 tools/arena_validate.py ARENA [--fight] [--mods FOLDER]
                                    [--out FOLDER] [--limit BLANKS]
                                    [--tolerance UNITS] [--step UNITS]

What it checks, on the file alone:
  1. the file: the port's own reading of it (a magic and a format, a
     ground, its tables' lines), its ground read whole by
     tools/stage_ground.py and written again the same; six sections, no
     areas or props;
  2. the game's limits: a host the select screen has and nothing of its
     own (port_arenas.h); the ground no longer than the host's on the disc
     and what stays in memory of it no more than the host's; 500 tiles,
     1,000 entries of the cells' lists, 256 corners a tile, a collision
     grid of 65,534 bytes, every collision face's outline within what the
     GTE's projection reaches, its plane through its outline, its outline
     convex; 20 colour tables in the fog's row;
  3. the floor: from every start, every point a fighter can get to is
     walked, a step of `--step` (250 units) at a time, with the game's own
     cast (tools/ground_cast.py): a wall stops the walk at the feet, and
     one he could climb (a floor within 1,100 units up past it) does not.
     Every point reached must have a floor under it: a hole to fall
     through, or the edge of the grid, fails, with where. So the arena's
     barriers must close it, or the out of bounds must have ground under
     it where the author meant a fighter to go;
  4. the starts: each on a level floor (not a wall's or a barrier's
     surface) within 50 units of its height, a wall no nearer than 500
     units, the two of a pair 1,500 units apart at least, among the
     points walked;
  5. what is met and what is seen agree: at every point walked a floor
     is drawn within `--tolerance` (200 units) of the floor met, and every
     wall met but a barrier has a face drawn within it.
And with `--fight`, the fight (in WSL, with the programs `make port`
builds and the player's disc, as tools/port_rollback.py finds it):
  6. both sizes of the port (build/port/, build/port32/) play the same
     fight on the arena, the CPU against the CPU (`--fight KEY ...
     --fight-cpu --no-cards`, a plan of the play tests' kind written for
     each), and their traces are the same, line for line; at every
     checkpoint both fighters stand within the points walked (by 1,000
     units) and within 400 units of a floor's height, and the fight ends
     with a winner before `--limit` blanks.

Exit 0 if every check passes, 1 if one fails, 2 for a usage mistake."""
import argparse
import math
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import stage_ground  # noqa: E402
import ground_cast  # noqa: E402
import arena_compile  # noqa: E402

CELLS = 32
CELL = 2000
HALF = 32000
# The stages an arena may stand in for (port_arenas.h,
# port_ArenaHostIsPlain): the length of each one's ground on the disc
# (whole sectors) and how much of it stays in memory (up to its cells).
HOSTS = {
    0: (108544, 40700), 1: (139264, 45088), 6: (151552, 56784), 9: (157696, 93164),
    10: (147456, 88908), 12: (108544, 56496), 15: (102400, 49528), 16: (147456, 59316),
    17: (114688, 54184), 19: (131072, 47924), 20: (98304, 42456), 21: (122880, 31796),
    22: (139264, 41340), 25: (178176, 73604), 26: (155648, 63396), 27: (126976, 38216),
    28: (131072, 37512), 29: (159744, 65288), 30: (131072, 49764), 31: (141312, 54276),
}
HOST_DEFAULT = 25
TILES_MAX = 500
PIECES_MAX = 60
RUN_ENTRIES_MAX = 1000
GRID_BYTES_MAX = 0xFFFE
CLUTS_MAX = 20
# the disc's stages' pages, 10 to 13 at y 256 (GROUND.md), in pixels of
# video memory: a page is 64 of them across, 256 down
PAGE_PIXELS = 64
PAGE_SIDE = 256
TEXTURE_X_LOW = 10 * PAGE_PIXELS
TEXTURE_X_HIGH = 14 * PAGE_PIXELS
TEXTURE_Y = 256
PROJECTION_MAX = 8000
FLAT_NORMAL_Y = -4000          # a level floor's normal (of 4096)
CLIMB = 1100                   # how far up past a wall a fighter may climb
FEET = 60                      # the walk's casts, this far above the feet
STEP_UP = 600                  # and a floor looked for from this far up
FALL_CAST = 20000              # how far down a floor is looked for
START_HEIGHT = 50
START_WALL = 500
START_APART = 1500
FIGHT_REACH = 1000
FIGHT_HEIGHT = 400
FIGHTERS = ('kannuki', 'gengoro:nodachi')
PROGRAMS = {64: 'build/port/rebushido', 32: 'build/port32/rebushido'}


class Report:
    def __init__(self):
        self.failed = 0

    def ok(self, what):
        print('OK   %s' % what)

    def fail(self, what):
        print('FAIL %s' % what)
        self.failed += 1

    def check(self, good, what):
        (self.ok if good else self.fail)(what)
        return good


# ---- the file ----

def read_file(path, report):
    with open(path, 'rb') as f:
        data = f.read()
    try:
        lines, ground_bytes = arena_compile.read_arena(data)
    except (arena_compile.ArenaError, ValueError) as e:
        report.fail('the file: %s' % e)
        return None
    ground = stage_ground.read(ground_bytes)
    again = stage_ground.write(ground)
    kinds = [s['kind'] for s in ground['sections']]
    report.check(kinds == ['meshes', 'collision', 'unused', 'obstacles', 'cells', 'textures'],
                 'the ground: six sections read whole (%s)' % ' '.join(kinds))
    report.check(again == ground_bytes, 'the ground written again from its numbers is the same')
    return lines, ground_bytes, ground


def check_limits(lines, ground_bytes, ground, report):
    host = int(lines.get('stands_in_for', [str(HOST_DEFAULT)])[-1])
    if not report.check(host in HOSTS, 'the host, stage %d, is one an arena may stand in for' % host):
        return host
    room, resident_room = HOSTS[host]
    count = struct_u32(ground_bytes, 0)
    offsets = [struct_u32(ground_bytes, 4 + 4 * i) for i in range(count + 1)]
    resident = offsets[count - 2]
    report.check(len(ground_bytes) <= room, 'the ground, %d bytes, fits the host\'s %d' % (len(ground_bytes), room))
    report.check(resident <= resident_room, 'what stays in memory, %d bytes, is no more than the host\'s %d'
                 % (resident, resident_room))
    meshes = stage_ground.section(ground, 'meshes')['meshes']
    cells = stage_ground.section(ground, 'cells')
    report.check(len(cells['tiles']) <= TILES_MAX, '%d tiles of %d' % (len(cells['tiles']), TILES_MAX))
    report.check(len(cells['pieces']) <= PIECES_MAX, '%d pieces of %d' % (len(cells['pieces']), PIECES_MAX))
    entries = sum(len(r['draws']) for r in cells['runs'])
    report.check(entries <= RUN_ENTRIES_MAX, '%d entries in the cells\' lists of %d' % (entries, RUN_ENTRIES_MAX))
    corners = max([len(m['vertices']) for m in meshes if 'vertices' in m] or [0])
    report.check(corners <= 256, 'the most corners a tile has: %d of 256' % corners)
    grid_bytes = offsets[2] - offsets[1]
    report.check(grid_bytes <= GRID_BYTES_MAX, 'the collision grid: %d bytes of %d' % (grid_bytes, GRID_BYTES_MAX))
    worst = 0
    off_plane = 0
    concave = 0
    for faces in stage_ground.section(ground, 'collision')['cells'].values():
        for f in faces:
            for u, v in f['outline']:
                worst = max(worst, abs(u), abs(v))
            n = f['normal']
            # the outline's corners back in the cell, on the plane?
            for u, v in f['outline']:
                p = [f['origin'][k] + (u * f['u'][k] + v * f['v'][k]) / 16384 for k in range(3)]
                if abs(sum(n[k] * p[k] for k in range(3)) + f['d']) / 16384 > 8:
                    off_plane += 1
            o = f['outline']
            for i in range(len(o) - 2):
                a, b, c = o[i], o[i + 1], o[i + 2]
                # the game's outlines turn the one way (its inside test)
                if (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]) < 0:
                    concave += 1
                    break
    report.check(worst <= PROJECTION_MAX, 'every collision face within %d units of its middle (%d)'
                 % (PROJECTION_MAX, worst))
    report.check(off_plane == 0, 'every collision face\'s outline on its plane (%d corners off)' % off_plane)
    report.check(concave == 0, 'every collision face convex (%d not)' % concave)
    records = stage_ground.section(ground, 'textures')['groups'][0]
    cluts = {tuple(r['clut']) for r in records}
    in_row = all(c[1] == 480 and 0 <= c[0] < 320 and c[0] % 16 == 0 for c in cluts)
    report.check(len(cluts) <= CLUTS_MAX and in_row, '%d colour tables, in the fog\'s row' % len(cluts))
    # each picture, a texture or a smaller copy of one, in the stage's
    # pages and over no other (the compiler's places: arena_compile.py,
    # place_textures)
    tims = stage_ground.section(ground, 'textures')['tims']
    boxes = []
    for r in records:
        tim = tims[r['tim']]
        at = 8 + (struct_u32(tim, 8) if struct_u32(tim, 4) & 8 else 0)
        w = int.from_bytes(tim[at + 8:at + 10], 'little')
        h = int.from_bytes(tim[at + 10:at + 12], 'little')
        boxes.append((r['image'][0], r['image'][1], w, h))
    outside = [b for b in boxes if b[0] < TEXTURE_X_LOW or b[0] + b[2] > TEXTURE_X_HIGH or b[1] < TEXTURE_Y
               or b[1] + b[3] > TEXTURE_Y + PAGE_SIDE or b[0] // PAGE_PIXELS != (b[0] + b[2] - 1) // PAGE_PIXELS]
    over = sum(1 for i in range(len(boxes)) for j in range(i)
               if boxes[i][0] < boxes[j][0] + boxes[j][2] and boxes[j][0] < boxes[i][0] + boxes[i][2]
               and boxes[i][1] < boxes[j][1] + boxes[j][3] and boxes[j][1] < boxes[i][1] + boxes[i][3])
    report.check(not outside and not over, '%d pictures (textures and their smaller copies) in pages 10 to 13, '
                 'none over another (%d outside, %d over)' % (len(boxes), len(outside), over))
    return host


def struct_u32(data, at):
    return int.from_bytes(data[at:at + 4], 'little')


# ---- what is drawn, in the world ----

def render_faces(ground):
    """Every face of every tile as (world corners, normal of 1)."""
    meshes = stage_ground.section(ground, 'meshes')['meshes']
    faces = []
    for tile in stage_ground.section(ground, 'cells')['tiles']:
        mesh = meshes[tile['mesh']]
        if 'vertices' not in mesh:
            continue
        cx, cz = tile['cell'] % CELLS, tile['cell'] // CELLS
        corner = (cx * CELL - HALF, 0, cz * CELL - HALF)
        verts = [(v[0] + corner[0], v[1], v[2] + corner[2]) for v in mesh['vertices']]
        for group in mesh['groups']:
            quad = (group['type'] >> 3) == 1
            for rec in group['records']:
                b = bytes.fromhex(rec) if isinstance(rec, str) else rec
                if quad:
                    c = [verts[i] for i in b[14:18]]
                    pts = [c[0], c[1], c[3], c[2]]
                else:
                    pts = [verts[i] for i in b[12:15]]
                n = arena_compile.polygon_normal(pts)
                faces.append((pts, arena_compile.norm(n)))
    return faces


def floor_drawn_at(faces, x, z):
    """The heights of the faces drawn over (x, z) that face up."""
    heights = []
    for pts, n in faces:
        if n[1] > -0.3:
            continue
        for i in range(1, len(pts) - 1):
            a, b, c = pts[0], pts[i], pts[i + 1]
            d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if abs(d) < 1e-9:
                continue
            l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
            l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
            l3 = 1 - l1 - l2
            if min(l1, l2, l3) >= -1e-6:
                heights.append(l1 * a[1] + l2 * b[1] + l3 * c[1])
                break
    return heights


def distance_to_face(p, pts, n):
    """How far a point is from a convex face (its plane where it is over
    the face, else the nearest corner or edge)."""
    plane = arena_compile.dot(arena_compile.sub(p, pts[0]), n)
    inside = True
    for i in range(len(pts)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        if arena_compile.dot(arena_compile.cross(arena_compile.sub(b, a), arena_compile.sub(p, a)), n) < 0:
            inside = False
            break
    if inside:
        return abs(plane)
    best = None
    for i in range(len(pts)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        ab = arena_compile.sub(b, a)
        t = max(0.0, min(1.0, arena_compile.dot(arena_compile.sub(p, a), ab) / max(arena_compile.dot(ab, ab), 1e-9)))
        q = arena_compile.add(a, arena_compile.mul(ab, t))
        d = math.dist(p, q)
        best = d if best is None else min(best, d)
    return best


# ---- the walk ----

def walk(grid, starts, step, report):
    """Every point a fighter can get to from the starts: {(ix, iz, y)}.
    Fails at the first hole and at the edge of the grid."""
    seen = set()
    todo = []
    holes = []
    for x, y, z in starts:
        floor = grid.floor_under(x, z, y - 600, 4000)
        if floor:
            key = (x // step, z // step, floor[0])
            if key not in seen:
                seen.add(key)
                todo.append((x // step * step, floor[0], z // step * step))
    while todo:
        x, y, z = todo.pop()
        for dx, dz in ((step, 0), (-step, 0), (0, step), (0, -step)):
            nx, nz = x + dx, z + dz
            if abs(nx) >= HALF - step or abs(nz) >= HALF - step:
                holes.append((nx, nz, 'the edge of the grid'))
                continue
            blocked = grid.cast((x, y - FEET, z), (nx, y - FEET, nz))
            # a floor met on the way (a slope up) is walked onto: looked
            # for from a little higher
            top = y - STEP_UP
            if blocked and blocked[1][1] > ground_cast.FLOOR_NORMAL_Y:
                # a wall: can he climb it?
                if grid.cast((x, y - CLIMB, z), (nx, y - CLIMB, nz)):
                    continue
                top = y - CLIMB
            floor = grid.floor_under(nx, nz, top, min(FALL_CAST, 32000 - top))
            if floor is None:
                holes.append((nx, nz, 'no floor under it'))
                continue
            ny = floor[0]
            key = (nx // step, nz // step, ny // 50)
            if key in seen:
                continue
            seen.add(key)
            todo.append((nx, ny, nz))
        if len(holes) > 20:
            break
    for hx, hz, why in holes[:5]:
        report.fail('a fighter can get to (%d, %d): %s' % (hx, hz, why))
    if not holes:
        report.ok('the floor: %d points walked from the starts, a floor under every one, the edge never reached'
                  % len(seen))
    return seen, not holes


def check_starts(lines, grid, walked, step, report):
    starts = []
    pairs = []
    for i in '1234':
        values = lines.get('start' + i)
        if not values:
            continue
        v = [int(t) for t in values[-1].replace(',', ' ').split()]
        pairs.append((i, v[0:3], v[3:6]))
        starts += [v[0:3], v[3:6]]
    if not report.check(bool(pairs), 'the starts: %d pairs' % len(pairs)):
        return starts
    reached = {(k[0], k[1]) for k in walked}
    for i, a, b in pairs:
        for p in (a, b):
            floor = grid.floor_under(p[0], p[2], p[1] - 600, 4000)
            good = floor is not None and abs(floor[0] - p[1]) <= START_HEIGHT and \
                floor[1][1] <= FLAT_NORMAL_Y and floor[2] not in (ground_cast.PLAIN, ground_cast.BARRIER)
            report.check(good, 'start%s (%d, %d, %d) stands on a level floor of its height%s'
                         % (i, p[0], p[1], p[2], '' if good else ' (%s)' % (floor,)))
            near = None
            for k in range(8):
                ang = k * math.pi / 4
                q = (p[0] + int(START_WALL * math.cos(ang)), p[1] - 500, p[2] + int(START_WALL * math.sin(ang)))
                if grid.cast((p[0], p[1] - 500, p[2]), q):
                    near = q
            report.check(near is None, 'start%s (%d, %d): no wall within %d units' % (i, p[0], p[2], START_WALL))
            report.check((p[0] // step, p[2] // step) in reached, 'start%s (%d, %d) among the points walked'
                         % (i, p[0], p[2]))
        apart = math.dist((a[0], a[2]), (b[0], b[2]))
        report.check(apart >= START_APART, 'start%s\'s two fighters %d units apart (%d at least)'
                     % (i, apart, START_APART))
    return starts


def check_agreement(ground, grid, walked, step, tolerance, report):
    faces = render_faces(ground)
    bad = []
    for ix, iz, yq in sorted(walked):
        x, z = ix * step, iz * step
        floor = grid.floor_under(x, z, yq * 50 - 600, 4000)
        if floor is None:
            continue
        heights = floor_drawn_at(faces, x, z)
        if not heights or min(abs(h - floor[0]) for h in heights) > tolerance:
            bad.append((x, z, floor[0], heights[:3]))
    for x, z, y, h in bad[:5]:
        report.fail('at (%d, %d) the floor met is at %d and the floor drawn %s' % (x, z, y, h or 'nowhere'))
    if not bad:
        report.ok('the floor drawn within %d units of the floor met at every point walked' % tolerance)
    unseen = []
    for cell, cfaces in stage_ground.section(ground, 'collision')['cells'].items():
        cx, cz = int(cell) % CELLS, int(cell) // CELLS
        for f in cfaces:
            if f['surface'] == ground_cast.BARRIER or f['normal'][1] < -8192:
                continue
            # the middle of its outline, in the world
            o = f['outline'][:-1]
            mu = sum(p[0] for p in o) / len(o)
            mv = sum(p[1] for p in o) / len(o)
            p = [f['origin'][k] + (mu * f['u'][k] + mv * f['v'][k]) / 16384 for k in range(3)]
            p[0] += cx * CELL - HALF
            p[2] += cz * CELL - HALF
            # only what is within the cell itself: the rest is its margin
            if not (cx * CELL - HALF <= p[0] <= (cx + 1) * CELL - HALF and
                    cz * CELL - HALF <= p[2] <= (cz + 1) * CELL - HALF):
                continue
            if min((distance_to_face(p, pts, n) for pts, n in faces), default=1e9) > tolerance:
                unseen.append(tuple(int(c) for c in p))
    for p in unseen[:5]:
        report.fail('a wall met at %s has nothing drawn within %d units (a barrier would be meant unseen)'
                    % (p, tolerance))
    if not unseen:
        report.ok('every wall met but a barrier has a face drawn within %d units' % tolerance)


# ---- the fight ----

FIGHT_TOML = '''about = "An arena's fight, both fighters the CPU's (tools/arena_validate.py)"
discs = ["port"]
cards = "none"
limit = %(limit)d
seconds = 900
input = []
compare = ["screen", "stage", "winner", "p1_defeated", "p1_x", "p1_y", "p1_z", "p2_defeated", "p2_x", "p2_y", "p2_z"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 4000
shot = false

[[checkpoint]]
name = "f"
frames = 30
repeat = %(repeat)d
shot = false
'''


def fight(key, mods, out, limit, walked, step, report):
    sys.path.insert(0, os.path.join(REPO, 'tools'))
    import playtest
    os.makedirs(out, exist_ok=True)
    toml = os.path.join(out, 'arena_fight.toml')
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write(FIGHT_TOML % {'limit': limit, 'repeat': max(1, (limit - 1000) // 30)})
    traces = {}
    for bits, program in sorted(PROGRAMS.items()):
        program = os.path.join(REPO, program)
        if not os.path.exists(program):
            report.fail('the fight: no %s (make port)' % program)
            return
        playtest.PORT_PROGRAM = program
        playtest.PORT_SYMBOLS = program + '.sym'
        test = playtest.Test(toml)
        disc = playtest.Disc('port')
        test.set_discs([disc])
        names, lines = test.plan(disc, playtest.header_names(), test.discs.index('port') + 1)
        plan = os.path.join(out, 'plan%d.txt' % bits)
        with open(plan, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        cmd = [program, '--image', playtest.RETAIL_CUE, '--mods', mods, '--plan', plan, '--no-cards',
               '--out', os.path.join(out, 'out%d' % bits),
               '--fight', key, FIGHTERS[0], FIGHTERS[1], '--fight-cpu']
        os.makedirs(os.path.join(out, 'out%d' % bits), exist_ok=True)
        with open(os.path.join(out, 'trace%d.txt' % bits), 'wb') as o, \
                open(os.path.join(out, 'log%d.txt' % bits), 'wb') as e:
            code = subprocess.run(cmd, cwd=REPO, stdout=o, stderr=e, stdin=subprocess.DEVNULL,
                                  timeout=1800).returncode
        with open(os.path.join(out, 'trace%d.txt' % bits), encoding='utf-8', errors='replace') as f:
            traces[bits] = f.read()
        # 1: the run reached its limit, a checkpoint not met (the fight
        # over, the frames counted no more)
        if not report.check(code in (0, 1), 'the fight on the %d-bit port ran (exit %d; %s)'
                            % (bits, code, os.path.join(out, 'log%d.txt' % bits))):
            return
    report.check(traces[64] == traces[32], 'the fight: both sizes printed the same, %d lines'
                 % traces[64].count('\n'))
    order = [l.split()[1] for l in open(os.path.join(out, 'plan64.txt')) if l.startswith('var ')]
    reached = [(k[0] * step, k[1] * step, k[2] * 50) for k in walked]
    xs = [p[0] for p in reached]
    zs = [p[1] for p in reached]
    ys = [p[2] for p in reached]
    winner = -1
    strays = []
    checkpoints = 0
    for line in traces[64].splitlines():
        w = line.split()
        if not w or w[0] != 'cp' or w[1] == '(failure)':
            continue
        checkpoints += 1
        values = dict(zip(order, (int(v) for v in w[3:])))
        winner = values.get('winner', -1)
        for side in ('p1', 'p2'):
            x, y, z = values[side + '_x'], values[side + '_y'], values[side + '_z']
            if not (min(xs) - FIGHT_REACH <= x <= max(xs) + FIGHT_REACH and
                    min(zs) - FIGHT_REACH <= z <= max(zs) + FIGHT_REACH and
                    min(ys) - FIGHT_HEIGHT <= y <= max(ys) + FIGHT_HEIGHT):
                strays.append((w[1], side, x, y, z))
    for cp, side, x, y, z in strays[:3]:
        report.fail('the fight: at %s %s is at (%d, %d, %d), outside the arena' % (cp, side, x, y, z))
    if not strays:
        report.ok('the fight: both fighters on the arena at all %d checkpoints' % checkpoints)
    report.check(winner in (0, 1), 'the fight ended with a winner (%s) within %d blanks'
                 % ({0: 'the first', 1: 'the second'}.get(winner, 'none'), limit))


def main(argv):
    parser = argparse.ArgumentParser(description='Holds an arena to what a fight needs.')
    parser.add_argument('arena')
    parser.add_argument('--fight', action='store_true', help='and the CPU against the CPU, both sizes')
    parser.add_argument('--mods', help='the mods folder the arena is in (for --fight)')
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'arena'))
    parser.add_argument('--limit', type=int, default=20000)
    parser.add_argument('--tolerance', type=int, default=200)
    parser.add_argument('--step', type=int, default=250)
    args = parser.parse_args(argv[1:])
    report = Report()
    print('arena_validate: %s' % args.arena)
    read = read_file(args.arena, report)
    if read is None:
        return 1
    lines, ground_bytes, ground = read
    check_limits(lines, ground_bytes, ground, report)
    grid = ground_cast.Grid(stage_ground.section(ground, 'collision'))
    starts = []
    for i in '1234':
        for v in lines.get('start' + i, [])[-1:]:
            n = [int(t) for t in v.replace(',', ' ').split()]
            starts += [n[0:3], n[3:6]]
    walked, closed = walk(grid, starts, args.step, report)
    check_starts(lines, grid, walked, args.step, report)
    check_agreement(ground, grid, walked, args.step, args.tolerance, report)
    if args.fight:
        if not closed:
            report.fail('the fight: not run, the arena has a hole')
        else:
            key = os.path.splitext(os.path.basename(args.arena))[0].lower()
            mods = args.mods or os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(args.arena))))
            if os.path.isdir(args.out):
                shutil.rmtree(args.out)
            fight(key, mods, args.out, args.limit, walked, args.step, report)
    print('arena_validate: %s' % ('%d checks failed' % report.failed if report.failed else 'every check passed'))
    return 1 if report.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
