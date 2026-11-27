#!/usr/bin/env python3
"""Makes the kanabo of the example mod src/port/mods/kanabo/: a Japanese
iron-studded war club, as a weapon model the port's remastered picture draws
in a fighter's hand (docs/PORT.md, "The fighters' own models", "The
weapons").

    python3 tools/weapon_kanabo.py [--out src/port/mods/kanabo/models/kanabo.glb]
                                   [--picture tmp/kanabo/kanabo.png]
    python3 tools/weapon_kanabo.py --motions disc/MOTION --mod DIR

The second makes the club its own motions from your disc, the example of a
move joined to another file's motion (docs/MODS.md): the katana's file of
the first set, U123.BBM, with the first stance's two blows (the moves its
commands 00d5 and 00d6 lead to from standing) playing the broadsword's
(K123.BBM's moves of the same commands), as DIR/motions/kanabo.bbm, and
`motions = kanabo` in DIR/weapons/kanabo.txt. DIR is a copy of the mod;
nothing of the disc is in the one the program ships.

Everything in it is made here, by arithmetic: the mesh, its normals and the
small picture it is coloured from. Nothing is read from the disc, and no mesh
or picture of anyone else's (another game's, a model library's, a learned
model's) is used. The club is a historical weapon's shape, not a copy of any
drawing of one.

The club, in the game's units and axes (y down), the hand at the origin and
the weapon along +z as the katana's points are (g_fighter_katana_weapon_points:
the tip at (0, -60, 950), the point near the hand at (0, 0, 100)):

  - a pommel ring at the butt, about z = -120, its loop in the y-z plane,
    under an iron butt cap;
  - an octagonal grip wrapped in dark cord over pale skin, the cord's
    diamonds showing it, from z = -90 to z = 255;
  - a round iron collar at z = 255 to 275;
  - the club's octagonal body, widening from 28 to 52 units (the corners'
    distance from the axis) from z = 275 to z = 1100, of wood with an iron
    band at each end, and rows of square-pyramid iron studs, one a face a
    row, the rows turned by half a face each from the one below;
  - a rounded top, in four bands, to z = 1160.

The faces of the octagons are lit flat, the collar and the ring smooth.

The file is what src/port/gl/gl_fighter_file.c reads (gl_fighter_file.h):
glTF 2.0 binary, one skin of the 21 bones `bone00` to `bone20` at the bind
pose of no turn and no place (every inverse bind matrix the identity), every
vertex wholly on bone 0 (JOINTS_0 0, WEIGHTS_0 1), as tools/fighter_export.py
writes a weapon of the disc (read_weapon), and by the same writer
(write_glb). It has no _RB_TERM attributes: for a vertex on one bone the
term the port makes of the blend as it loads, (x, y, z, 1) on bone 0, is
the one fighter_export.py would write, and leaving them out halves the
file. The port gives every bone a weapon's world matrix,
so the vertices are where the weapon's own were. `extras.rb_weapon` is 0, the
katana's model, which the port stores and does not otherwise read (the file
is found by its name, `models/W00.glb`, not by it); `extras.rb_mod_weapon`
names the club. There is no `rb_palette`: the picture is the club's own, no
colour table of the game's stands behind it.
"""
import argparse
import math
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import bbm_join  # noqa: E402
import fighter_export  # noqa: E402

OUT = os.path.join(ROOT, 'src', 'port', 'mods', 'kanabo', 'models', 'kanabo.glb')

# ---- the picture: 256 by 256, three regions ----

SIDE = 256
# x0, y0, x1, y1 in texels
WOOD = (0, 0, 128, 256)
IRON = (128, 0, 256, 64)
CORD = (128, 64, 256, 256)

# The octagons' corners are at 22.5 degrees and every 45 after, so that a
# face looks along +x, -y, -x, +y.
SIDES = 8
PHASE = math.pi / SIDES

GRIP_FROM, GRIP_TO, GRIP_R = -90.0, 255.0, 17.0
COLLAR_FROM, COLLAR_TO, COLLAR_R = 255.0, 275.0, 36.0
BODY_FROM, BODY_TO = 275.0, 1100.0
BODY_R_FROM, BODY_R_TO = 28.0, 52.0
BAND = 26.0  # the iron bands' length at either end of the body
BAND_PROUD = 2.5  # how far a band stands out of the wood
STUD_FIRST, STUD_LAST, STUD_ROWS = 350.0, 1040.0, 12
STUD_HALF, STUD_HEIGHT = 9.0, 16.0


def clamp(v):
    return max(0, min(255, int(round(v))))


def picture():
    """The club's colours: wood with its grain, iron, and cord wrapped in
    diamonds over pale skin. Returns the RGBA bytes."""
    rng = random.Random(1998)
    rgba = bytearray(SIDE * SIDE * 4)

    def put(x, y, c):
        at = (y * SIDE + x) * 4
        rgba[at:at + 4] = bytes([clamp(c[0]), clamp(c[1]), clamp(c[2]), 255])

    # wood: streaks along the length (v), each column its own shade, the
    # streaks wandering a little
    x0, y0, x1, y1 = WOOD
    shade = [rng.uniform(-1, 1) for _ in range(64)]
    knots = [(rng.uniform(x0, x1), rng.uniform(y0, y1), rng.uniform(4, 9)) for _ in range(3)]
    for y in range(y0, y1):
        for x in range(x0, x1):
            wander = 3.0 * math.sin(y * 0.045 + x * 0.11) + 1.5 * math.sin(y * 0.13)
            u = (x + wander) * 0.5
            a = shade[int(u) % 64]
            b = shade[(int(u) + 1) % 64]
            t = u - int(u)
            s = a * (1 - t) + b * t
            ring = 0.0
            for kx, ky, kr in knots:
                d = math.hypot(x - kx, (y - ky) * 0.5)
                if d < kr * 3:
                    ring += 0.6 * math.cos(d * 1.6) * (1 - d / (kr * 3))
            g = 0.5 + 0.5 * math.sin(u * 2.3 + 2.0 * s)
            k = 1.0 + 0.10 * s - 0.16 * g * g + 0.10 * ring + rng.uniform(-0.03, 0.03)
            put(x, y, (84 * k, 46 * k, 22 * k))
    # iron: near black, a little grain and pitting
    x0, y0, x1, y1 = IRON
    for y in range(y0, y1):
        for x in range(x0, x1):
            k = 1.0 + rng.uniform(-0.12, 0.12) + 0.06 * math.sin(x * 0.7 + y * 0.3)
            if rng.random() < 0.02:
                k *= 0.6
            put(x, y, (42 * k, 42 * k, 46 * k))
    # cord: the picture is laid round the grip as 8 faces across and the
    # grip's length down, so the diamonds are drawn in the grip's own units
    x0, y0, x1, y1 = CORD
    around = SIDES * 2 * GRIP_R * math.sin(math.pi / SIDES)
    along = GRIP_TO - GRIP_FROM
    pitch = around / 2.0  # two diamonds round the grip, four seen across
    for y in range(y0, y1):
        for x in range(x0, x1):
            pu = (x - x0 + 0.5) / (x1 - x0) * around
            pv = (y - y0 + 0.5) / (y1 - y0) * along
            a = ((pu + pv) / pitch) % 1.0
            b = ((pu - pv) / pitch) % 1.0
            # a band of cord along each diagonal; between them the skin
            da = abs(a - 0.5) * 2
            db = abs(b - 0.5) * 2
            on = max(da, db)
            if on > 0.55:
                # the cord: darker at its edges, the band over on top
                over = da if (int((pu + pv) / pitch) + int((pu - pv) / pitch)) % 2 else db
                edge = (on - 0.55) / 0.45
                k = 0.75 + 0.45 * math.sin(edge * math.pi) + 0.15 * (over > 0.55) + rng.uniform(-0.05, 0.05)
                put(x, y, (34 * k, 28 * k, 40 * k))
            else:
                # the skin under it, pale and pebbled
                k = 1.0 - 0.25 * (1 - on / 0.55) ** 2 + rng.uniform(-0.08, 0.08)
                put(x, y, (118 * k, 104 * k, 78 * k))
    return rgba


def region_uv(region, s, t):
    """A point of a region, s across and t down from 0 to 1, as glTF's
    texture point (0, 0 the picture's top left), kept half a texel inside
    the region so that nothing beside it is read."""
    x0, y0, x1, y1 = region
    s = min(max(s, 0.0), 1.0)
    t = min(max(t, 0.0), 1.0)
    return ((x0 + 0.5 + s * (x1 - x0 - 1)) / SIDE, (y0 + 0.5 + t * (y1 - y0 - 1)) / SIDE)


# ---- the mesh ----

class Mesh:
    """Faces, each its corners round it, a texture point and a normal each."""

    def __init__(self):
        self.faces = []

    def face(self, corners, uvs, normals=None):
        if normals is None:
            n = face_normal(corners)
            normals = [n] * len(corners)
        self.faces.append((corners, uvs, normals))


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def unit(a):
    d = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / d, a[1] / d, a[2] / d)


def face_normal(corners):
    """The face's normal, turned away from the club's axis (or along it,
    for a face across the axis)."""
    n = (0.0, 0.0, 0.0)
    for i in range(1, len(corners) - 1):
        c = cross(sub(corners[i], corners[0]), sub(corners[i + 1], corners[0]))
        n = (n[0] + c[0], n[1] + c[1], n[2] + c[2])
    n = unit(n)
    mid = [sum(p[k] for p in corners) / len(corners) for k in range(3)]
    out = (mid[0], mid[1], 0.0)
    if abs(n[2]) > 0.9 or dot(out, out) < 1e-6:
        # a cap: its side of the club decides
        out = (0.0, 0.0, mid[2] - CENTRE_Z)
    if dot(n, out) < 0:
        n = (-n[0], -n[1], -n[2])
    return n


# the middle of the club along its length: a cap faces away from it
CENTRE_Z = 500.0


def ring(z, r, sides=SIDES, phase=PHASE):
    return [(r * math.cos(phase + 2 * math.pi * k / sides), r * math.sin(phase + 2 * math.pi * k / sides), z)
            for k in range(sides)]


def tube(mesh, rings, region, smooth=False, around=1):
    """Bands between successive rings of (z, r), the region laid across each
    face (`around` 1) or once round the whole (`around` SIDES), its rows
    down the length of the whole tube."""
    z_first, z_last = rings[0][0], rings[-1][0]
    for (za, ra), (zb, rb) in zip(rings, rings[1:]):
        a, b = ring(za, ra), ring(zb, rb)
        ta = (za - z_first) / (z_last - z_first or 1.0)
        tb = (zb - z_first) / (z_last - z_first or 1.0)
        for k in range(SIDES):
            j = (k + 1) % SIDES
            if around == 1:
                s0, s1 = 0.0, 1.0
            else:
                s0, s1 = k / SIDES, (k + 1) / SIDES
            corners = [a[k], a[j], b[j], b[k]]
            uvs = [region_uv(region, s0, ta), region_uv(region, s1, ta),
                   region_uv(region, s1, tb), region_uv(region, s0, tb)]
            normals = None
            if smooth:
                normals = [unit((p[0], p[1], 0.0)) for p in corners]
            mesh.face(corners, uvs, normals)


def round_tube(mesh, z0, z1, r, region, sides=16):
    """A short round cylinder with flat ends, smooth round its side."""
    a, b = ring(z0, r, sides, 0.0), ring(z1, r, sides, 0.0)
    for k in range(sides):
        j = (k + 1) % sides
        corners = [a[k], a[j], b[j], b[k]]
        uvs = [region_uv(region, k / sides, 0.2), region_uv(region, (k + 1) / sides, 0.2),
               region_uv(region, (k + 1) / sides, 0.8), region_uv(region, k / sides, 0.8)]
        mesh.face(corners, uvs, [unit((p[0], p[1], 0.0)) for p in corners])
    for z, rim in ((z0, a), (z1, b)):
        centre = (0.0, 0.0, z)
        for k in range(sides):
            j = (k + 1) % sides
            corners = [centre, rim[k], rim[j]]
            uvs = [region_uv(region, 0.5, 0.5), region_uv(region, 0.5 + 0.5 * math.cos(2 * math.pi * k / sides),
                                                          0.5 + 0.5 * math.sin(2 * math.pi * k / sides)),
                   region_uv(region, 0.5 + 0.5 * math.cos(2 * math.pi * j / sides),
                             0.5 + 0.5 * math.sin(2 * math.pi * j / sides))]
            mesh.face(corners, uvs, [(0.0, 0.0, 1.0 if z == z1 else -1.0)] * 3)


def cap(mesh, z, r, region, facing):
    """An octagon across the axis at z, facing -z (-1) or +z (+1)."""
    rim = ring(z, r)
    centre = (0.0, 0.0, z)
    for k in range(SIDES):
        j = (k + 1) % SIDES
        ak = PHASE + 2 * math.pi * k / SIDES
        aj = PHASE + 2 * math.pi * j / SIDES
        corners = [centre, rim[k], rim[j]]
        uvs = [region_uv(region, 0.5, 0.5), region_uv(region, 0.5 + 0.5 * math.cos(ak), 0.5 + 0.5 * math.sin(ak)),
               region_uv(region, 0.5 + 0.5 * math.cos(aj), 0.5 + 0.5 * math.sin(aj))]
        mesh.face(corners, uvs, [(0.0, 0.0, float(facing))] * 3)


def annulus(mesh, z, r_in, r_out, region, facing):
    """The step between two octagons at z."""
    a, b = ring(z, r_in), ring(z, r_out)
    for k in range(SIDES):
        j = (k + 1) % SIDES
        corners = [a[k], a[j], b[j], b[k]]
        uvs = [region_uv(region, 0.0, 0.0), region_uv(region, 1.0, 0.0),
               region_uv(region, 1.0, 0.2), region_uv(region, 0.0, 0.2)]
        mesh.face(corners, uvs, [(0.0, 0.0, float(facing))] * 4)


def body_radius(z):
    t = (z - BODY_FROM) / (BODY_TO - BODY_FROM)
    return BODY_R_FROM + (BODY_R_TO - BODY_R_FROM) * t


def body(mesh):
    """The wood between the bands, the bands, and the rounded top."""
    wood_from, wood_to = BODY_FROM + BAND, BODY_TO - BAND
    steps = 12
    rings = [(wood_from + (wood_to - wood_from) * i / steps,
              body_radius(wood_from + (wood_to - wood_from) * i / steps)) for i in range(steps + 1)]
    tube(mesh, rings, WOOD)
    for z0, z1 in ((BODY_FROM, wood_from), (wood_to, BODY_TO)):
        r0, r1 = body_radius(z0) + BAND_PROUD, body_radius(z1) + BAND_PROUD
        tube(mesh, [(z0, r0), (z1, r1)], IRON, around=SIDES)
    # the bands' steps down to the wood (the lower band stands on the
    # collar, which is wider)
    annulus(mesh, wood_from, body_radius(wood_from), body_radius(wood_from) + BAND_PROUD, IRON, +1)
    annulus(mesh, wood_to, body_radius(wood_to), body_radius(wood_to) + BAND_PROUD, IRON, -1)
    # the top: the band's octagon rounded in to a point on the axis
    top_r = body_radius(BODY_TO) + BAND_PROUD
    dome = [(BODY_TO, top_r)]
    for i in range(1, 5):
        a = (math.pi / 2) * i / 5
        dome.append((BODY_TO + 60.0 * math.sin(a), top_r * math.cos(a)))
    tube(mesh, dome, IRON, around=SIDES)
    cap(mesh, dome[-1][0], dome[-1][1], IRON, +1)


def studs(mesh):
    """Rows of square pyramids of iron on the body's faces."""
    for row in range(STUD_ROWS):
        z = STUD_FIRST + (STUD_LAST - STUD_FIRST) * row / (STUD_ROWS - 1)
        r = body_radius(z)
        apothem = r * math.cos(math.pi / SIDES)
        width = 2 * r * math.sin(math.pi / SIDES)
        half = min(STUD_HALF, width * 0.38)
        for k in range(SIDES):
            # the face's middle; every other row half a face round, on the
            # corner between two faces, where the stud stands on the edge
            if row % 2 == 0:
                angle = PHASE + 2 * math.pi * (k + 0.5) / SIDES
                out = apothem
            else:
                angle = PHASE + 2 * math.pi * k / SIDES
                out = r * 0.97
            n = (math.cos(angle), math.sin(angle), 0.0)
            across = (-math.sin(angle), math.cos(angle), 0.0)
            centre = (n[0] * (out - 1.0), n[1] * (out - 1.0), z)
            apex = (centre[0] + n[0] * STUD_HEIGHT, centre[1] + n[1] * STUD_HEIGHT, z)
            base = []
            for sa, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                base.append((centre[0] + across[0] * half * sa, centre[1] + across[1] * half * sa,
                             z + half * sz))
            for i in range(4):
                corners = [base[i], base[(i + 1) % 4], apex]
                uvs = [region_uv(IRON, 0.1 + 0.2 * i, 0.9), region_uv(IRON, 0.3 + 0.2 * i, 0.9),
                       region_uv(IRON, 0.2 + 0.2 * i, 0.1)]
                fn = unit(cross(sub(corners[1], corners[0]), sub(corners[2], corners[0])))
                mid = [sum(p[c] for p in corners) / 3 for c in range(3)]
                if dot(fn, sub(mid, centre)) < 0:
                    fn = (-fn[0], -fn[1], -fn[2])
                mesh.face(corners, uvs, [fn] * 3)


def pommel(mesh):
    """The butt cap and the ring through it."""
    cap_from = GRIP_FROM - 16.0
    tube(mesh, [(cap_from, GRIP_R + 2.0), (GRIP_FROM, GRIP_R + 2.0)], IRON, around=SIDES)
    annulus(mesh, GRIP_FROM, GRIP_R, GRIP_R + 2.0, IRON, +1)
    cap(mesh, cap_from, GRIP_R + 2.0, IRON, -1)
    # the ring: a torus in the y-z plane, its centre under the cap
    major, minor = 17.0, 4.5
    cz = cap_from - major + minor * 0.5
    big, small = 14, 6
    points = []
    for i in range(big + 1):
        a = 2 * math.pi * i / big
        row = []
        for j in range(small + 1):
            b = 2 * math.pi * j / small
            centre = (0.0, major * math.cos(a), cz + major * math.sin(a))
            n = (math.sin(b), math.cos(b) * math.cos(a), math.cos(b) * math.sin(a))
            row.append(((centre[0] + minor * n[0], centre[1] + minor * n[1], centre[2] + minor * n[2]), n))
        points.append(row)
    for i in range(big):
        for j in range(small):
            quad = [points[i][j], points[i + 1][j], points[i + 1][j + 1], points[i][j + 1]]
            uvs = [region_uv(IRON, i / big, j / small), region_uv(IRON, (i + 1) / big, j / small),
                   region_uv(IRON, (i + 1) / big, (j + 1) / small), region_uv(IRON, i / big, (j + 1) / small)]
            mesh.face([q[0] for q in quad], uvs, [q[1] for q in quad])


def kanabo():
    mesh = Mesh()
    pommel(mesh)
    tube(mesh, [(GRIP_FROM, GRIP_R), (GRIP_TO, GRIP_R)], CORD, around=SIDES)
    round_tube(mesh, COLLAR_FROM, COLLAR_TO, COLLAR_R, IRON)
    body(mesh)
    studs(mesh)
    return mesh


def vertices_of(mesh):
    """The faces as the file's vertices (a vertex for each corner of a
    face, so that a flat face has its own normal) and triangles, every
    vertex on bone 0."""
    vertices, triangles, lookup = [], [], {}
    for corners, uvs, normals in mesh.faces:
        idx = []
        for p, uv, n in zip(corners, uvs, normals):
            key = (tuple(round(c, 4) for c in p), tuple(round(c, 6) for c in uv), tuple(round(c, 5) for c in n))
            if key not in lookup:
                lookup[key] = len(vertices)
                place = [float(c) for c in p]
                vertices.append((place, list(n), list(uv), [(0, 1.0)]))
            idx.append(lookup[key])
        for c in range(1, len(idx) - 1):
            triangles.append((idx[0], idx[c], idx[c + 1]))
    return vertices, triangles


# The club's own motions (--motions): the file it is made on, the file its
# blows are brought from, and the commands from standing in the first stance
# that lead to them.
MOTIONS_INTO = 'U123.BBM'
MOTIONS_FROM = 'K123.BBM'
MOTIONS_COMMANDS = (0x00D5, 0x00D6)
MOTIONS_KEY = 'kanabo'


def led_to(motion_file, command):
    """The move of the file's own that `command` leads to from standing in
    the first stance."""
    standing = [place for place, said in motion_file.names().items()
                if said == 'standing in stance 1']
    for flags, target in motion_file.commands(standing[0]):
        if flags == command and target & bbm_join.OWN_FILE:
            return target & ~bbm_join.OWN_FILE
    raise SystemExit('weapon_kanabo: %s: no command %04x from standing in the first stance'
                     % (motion_file.name, command))


def make_motions(folder, mod):
    """The club's own motions into the mod at `mod`, from the disc's motion
    files in `folder`."""
    into = bbm_join.read_file(os.path.join(folder, MOTIONS_INTO))
    source = bbm_join.read_file(os.path.join(folder, MOTIONS_FROM))
    pairs = [(led_to(into, command), led_to(source, command)) for command in MOTIONS_COMMANDS]
    data = bbm_join.join(into, source, pairs, say=lambda line: print('weapon_kanabo: ' + line))
    os.makedirs(os.path.join(mod, 'motions'), exist_ok=True)
    out = os.path.join(mod, 'motions', MOTIONS_KEY + '.bbm')
    with open(out, 'wb') as f:
        f.write(data)
    weapon = os.path.join(mod, 'weapons', 'kanabo.txt')
    with open(weapon, encoding='utf-8') as f:
        text = f.read()
    if not any(line.split('=')[0].strip() == 'motions' for line in text.split('\n')):
        with open(weapon, 'a', encoding='utf-8', newline='\n') as f:
            f.write('# its own motions, made from the disc by tools/weapon_kanabo.py --motions:\n'
                    "# the katana's, but for the first stance's two blows, the broadsword's\n"
                    'motions = %s\n' % MOTIONS_KEY)
    print('weapon_kanabo: %s: %d bytes' % (out, len(data)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', default=OUT, help='the .glb written')
    ap.add_argument('--picture', help="also write the club's picture as a PNG here, for review")
    ap.add_argument('--motions', help="the disc's motion files (disc/MOTION): make the club's own "
                    'motions into --mod instead of its model')
    ap.add_argument('--mod', help='the kanabo mod (a copy of src/port/mods/kanabo/) --motions writes into')
    args = ap.parse_args()
    if args.motions:
        if not args.mod:
            ap.error('--motions needs --mod')
        make_motions(args.motions, args.mod)
        return 0
    bones, _ = fighter_export.source_tables()
    rgba = picture()
    image = fighter_export.png(SIDE, SIDE, rgba)
    vertices, triangles = vertices_of(kanabo())
    joints = fighter_export.BONES
    extras = {
        'rb_fighter': 1,
        'rb_weapon': 0,
        'rb_mod_weapon': 'kanabo',
        'model_file': 'kanabo',
        'units': 'game',
        'joints': ['bone%02d' % i for i in range(joints)],
        'bind_rotation': [[1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0] for _ in range(joints)],
        'bind_translation': [[0.0, 0.0, 0.0] for _ in range(joints)],
        'generator': 'tools/weapon_kanabo.py',
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    fighter_export.write_glb(args.out, 'kanabo', bones, [fighter_export.IDENTITY] * joints,
                             [[0.0, 0.0, 0.0]] * joints, vertices, triangles, image, extras)
    if args.picture:
        os.makedirs(os.path.dirname(os.path.abspath(args.picture)), exist_ok=True)
        with open(args.picture, 'wb') as f:
            f.write(image)
    print('weapon_kanabo: %s: %d vertices, %d triangles, %d bytes'
          % (os.path.relpath(args.out, ROOT), len(vertices), len(triangles), os.path.getsize(args.out)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
