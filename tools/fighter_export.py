#!/usr/bin/env python3
"""Exports the fighters' models from the player's own disc image as glTF 2.0,
for the native port's replacement models (docs/PORT.md, "The fighters'
own models").

    python3 tools/fighter_export.py --image "Bushido Blade 2 (USA).cue"
            [--models 3,4] [--out tmp/fighter_models] [--pose POSE.json]

For each model file (`M000`..`M306`, NDATA ids 0..36; docs/formats/NDATA.md)
it writes `<out>/<name>.glb`: the body, which the game draws as one mesh
skinned across thirteen bones (render_DrawSkinnedMesh), and the rigid parts
the bones carry (the head's pieces, the hands, the feet), as one mesh skinned
on the 21 bones of the skeleton every model shares (g_model_bones), with its
textures decoded from the file's TIMs into one picture. The cloth, which the
game moves itself (render_DrawCloth), and the shadows are not in it.

Nothing of the game is in this repository: the data is read from the image
the player gives, and what is written goes into a folder git ignores. The
skeleton's offsets and the models' sizes are read from the decompilation's
own source (src/main/model.c), which is where the executable has them.

How the body's mesh is read, and why a vertex can be weighted to two bones:
the game builds a joint's vertices as the sum of two bones' turns of two
vectors (a point of one bone's section, and a step from it turned by the
next bone), which is exact at any pose but is not the linear blend a glTF
skin is. Each such vertex is given the two bones and the weights that come
nearest to the game's sum over a spread of poses about the bind pose
(least squares), and the error of that is printed.

The bind pose is the skeleton with every bone turned as `--pose` says, or a
standing pose of its own if none is given. The vertices are written where
the game would draw them in that pose, in the game's own units and axes
(y down), so that the port can use them as they are; a node above the
skeleton turns them to glTF's y up and metres for anything else that opens
the file. The file's `extras` say which bone each joint is and give the
bind pose again (`rb_fighter`), which tools/fighter_conform.py and the port
read.
"""
import argparse
import json
import math
import os
import random
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import build_disc  # noqa: E402

# DATA0's order (docs/formats/NDATA.md): the model file of each id.
MODEL_NAMES = (['M000', 'M001', 'M002'] + ['M1%02d' % i for i in range(12)] +
               ['M003', 'M004', 'M005'] + ['M2%02d' % i for i in range(12)] +
               ['M301', 'M302', 'M303', 'M304', 'M305', 'M300', 'M306'])
BONES = 21
SECTIONS = 13
# model_SetMatrixTables: the bone of each of the body's matrices, in the
# order its sections and its normals name them (skin_matrices).
SKIN_BONES = [6, 5, 4, 10, 9, 8, 3, 2, 17, 16, 14, 13, 12, 1, 7, 11, 18, 15, 19, 20]
SCRATCH_POINTS = 512
VRAM_W = 1024
VRAM_H = 512


# ---- the disc ----

def bin_of(image):
    """The .bin a .cue names, or the image itself."""
    if image.lower().endswith('.cue'):
        with open(image, encoding='latin-1') as f:
            m = re.search(r'FILE\s+"([^"]+)"', f.read())
        if not m:
            raise SystemExit('fighter_export: %s names no file' % image)
        return os.path.join(os.path.dirname(os.path.abspath(image)), m.group(1))
    return image


class Ndata:
    def __init__(self, image):
        self.img = build_disc.Image(bin_of(image))
        _, files = build_disc.walk(self.img)
        inf = files['NDATA/NDATA.INF']
        raw = self.img.data(inf['lba'], (inf['size'] + 2047) // 2048)[:inf['size']]
        self.dat = files['NDATA/NDATA.DAT']['lba']
        self.entries = [struct.unpack_from('<HH', raw, i * 4) for i in range(len(raw) // 4)]

    def read(self, i):
        start, length = self.entries[i]
        return self.img.data(self.dat + start, length)


# ---- what the decompilation's source says ----

def source_tables():
    """g_model_bones and g_model_scales, from src/main/model.c."""
    with open(os.path.join(ROOT, 'src', 'main', 'model.c'), encoding='utf-8') as f:
        text = f.read()
    m = re.search(r'ModelBoneTable g_model_bones = \{(.*?)\n\};', text, re.S)
    bones = [(int(a), (int(b), int(c), int(d)), int(e)) for a, b, c, d, e in
             re.findall(r'\{\s*(-?\d+),\s*\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\},\s*(-?\d+)\s*\}',
                        m.group(1))]
    if len(bones) != BONES:
        raise SystemExit('fighter_export: g_model_bones has %d bones' % len(bones))
    m = re.search(r'u16 g_model_scales\[\d+\] = \{(.*?)\};', text, re.S)
    scales = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]+)', m.group(1))]
    return bones, scales


# ---- small matrix arithmetic: 3x3 as lists of rows, and 4x4 ----

def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def mat_vec(a, v):
    return [a[i][0] * v[0] + a[i][1] * v[1] + a[i][2] * v[2] for i in range(3)]


def transpose(a):
    return [[a[j][i] for j in range(3)] for i in range(3)]


def rot_zyx(rx, ry, rz):
    """RotMatrixZYX's turn, of angles in radians: the matrix is Rx Ry Rz,
    as the game's rotation order ZYX builds it (math.c)."""
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    mx = [[1, 0, 0], [0, cx, -sx], [0, sx, cx]]
    my = [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]
    mz = [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]
    return mat_mul(mat_mul(mx, my), mz)


def axis_angle(axis, angle):
    x, y, z = axis
    n = math.sqrt(x * x + y * y + z * z) or 1.0
    x, y, z = x / n, y / n, z / n
    c, s, t = math.cos(angle), math.sin(angle), 1 - math.cos(angle)
    return [[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
            [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
            [t * x * z - s * y, t * y * z + s * x, t * z * z + c]]


IDENTITY = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]

# A standing pose, as local turns of the bones (radians about x, y, z, in
# the order rot_zyx makes them): the trunk and the hips stand along the
# world's up (-y), the legs hang, the arms hang a little out from the body.
# The skeleton's every bone runs along its own +x and the body's sides are
# its z (the shoulders and the thighs are at z = +-100).
STAND = {
    2: (0.0, 0.0, -math.pi / 2),        # the trunk: +x up
    12: (0.0, 0.0, math.pi / 2),        # the hips: +x down
    4: (0.0, -2.6, 0.0),                # one shoulder: down and out
    8: (0.0, 2.6, 0.0),                 # the other
}


def world_pose(bones, scale, local_turns):
    """Every bone's world matrix (rotation, translation) with the body's
    root at the origin, turned as `local_turns` says (bone -> 3x3)."""
    rot = [None] * BONES
    pos = [None] * BONES
    for i, (parent, offset, _) in enumerate(bones):
        turn = local_turns.get(i, IDENTITY)
        t = [(o * scale) >> 12 for o in offset]
        if parent < 0:
            rot[i], pos[i] = turn, [0.0, 0.0, 0.0]
            continue
        rot[i] = mat_mul(rot[parent], turn)
        step = mat_vec(rot[parent], t)
        pos[i] = [pos[parent][k] + step[k] for k in range(3)]
    return rot, pos


def stand_pose(bones, scale):
    turns = {i: rot_zyx(*a) for i, a in STAND.items()}
    return world_pose(bones, scale, turns)


def pose_from_file(path):
    """A pose the port wrote (`--fighter-pose`): 21 world matrices of 1.12
    numbers and translations, the body's root then put at the origin."""
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    rot = [[[v / 4096.0 for v in m[r * 3:r * 3 + 3]] for r in range(3)] for m in data['rotation']]
    base = data['translation'][1]
    root_t = transpose(rot[1])
    rot = [mat_mul(root_t, r) for r in rot]
    pos = [mat_vec(root_t, [t[k] - base[k] for k in range(3)]) for t in data['translation']]
    return rot, pos


# ---- a mesh's records ----

def s16(b, o):
    return struct.unpack_from('<h', b, o)[0]


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


class Face:
    """One face: its corners' vertices, texture points, the page and the
    colour table, and for the lighting a normal of each corner."""
    __slots__ = ('verts', 'uvs', 'tpage', 'clut', 'normals', 'kind', 'lit', 'group', 'corners')

    def __init__(self):
        self.lit = True


def texture_words(b, o, quad):
    """A compact record's page, colour table and texture points."""
    tpage = u16(b, o + 0x00)
    clut = u16(b, o + 0x04)
    uvs = [(b[o + 6 + 2 * k], b[o + 7 + 2 * k]) for k in range(4 if quad else 3)]
    return tpage, clut, uvs


def skin_texture_words(b, o, quad):
    """A body record's: (u0, v0, clut), (u1, v1, page), u2 v2 [u3 v3]."""
    uvs = [(b[o], b[o + 1]), (b[o + 4], b[o + 5]), (b[o + 8], b[o + 9])]
    if quad:
        uvs.append((b[o + 12], b[o + 13]))
    return u16(b, o + 4 + 2), u16(b, o + 2), uvs


# Compact records by handler type (render.c, g_render_handlers): its size,
# whether it has four corners, where its corners are, how its normals are
# kept, and whether it is lit.
COMPACT = {
    4: (0x18, False, 0x0C, 'flat3', True),    # FT3
    5: (0x10, False, 0x0C, None, False),      # Unlit FT3
    6: (0x18, False, 0x0C, 'flat3', True),    # TwoSided FT3
    12: (0x18, True, 0x0E, 'flat4', True),    # FT4
    13: (0x14, True, 0x0E, None, False),      # Unlit FT4
    14: (0x18, True, 0x0E, 'flat4', True),    # TwoSided FT4
    20: (0x24, False, 0x0C, 'gouraud3', True),
    22: (0x24, False, 0x0C, 'gouraud3', True),
    28: (0x2C, True, 0x10, 'gouraud4', True),
    30: (0x2C, True, 0x10, 'gouraud4', True),
    42: (0x18, False, 0x0C, 'flat3', True),   # Tinted FT3
    43: (0x18, True, 0x0E, 'flat4', True),    # Tinted FT4
}


def compact_normals(b, o, how):
    if how == 'flat3':
        n = (s16(b, o + 0x10), s16(b, o + 0x12), s16(b, o + 0x14))
        return [n, n, n]
    if how == 'flat4':
        n = (s16(b, o + 0x14), s16(b, o + 0x16), s16(b, o + 0x12))
        return [n, n, n, n]
    if how == 'gouraud3':
        n0 = (s16(b, o + 0x10), s16(b, o + 0x12), s16(b, o + 0x14))
        n1 = (s16(b, o + 0x18), s16(b, o + 0x1A), s16(b, o + 0x16))
        n2 = (s16(b, o + 0x1C), s16(b, o + 0x1E), s16(b, o + 0x20))
        return [n0, n1, n2]
    if how == 'gouraud4':
        nf = (s16(b, o + 0x14), s16(b, o + 0x16), s16(b, o + 0x18))
        n0 = (s16(b, o + 0x1C), s16(b, o + 0x1E), s16(b, o + 0x1A))
        n1 = (s16(b, o + 0x20), s16(b, o + 0x22), s16(b, o + 0x24))
        n2 = (s16(b, o + 0x28), s16(b, o + 0x2A), s16(b, o + 0x26))
        return [nf, n0, n1, n2]
    return [(0, 0, 0)] * 4


def read_compact(b, at, problems):
    """A part's mesh (MESH_COMPACT): its vertices, bone-local, and its
    faces with indices into them. Several parts may follow each other."""
    verts = []
    faces = []
    cursor = at
    while True:
        word = u32(b, cursor)
        cursor += 4
        count, layout = word & 0xFFFF, word >> 16
        if count == 0:
            break
        if layout != 1:
            problems.append('a part of layout %d at 0x%x' % (layout, cursor - 4))
            break
        base = len(verts)
        for k in range(count):
            verts.append((s16(b, cursor + 6 * k), s16(b, cursor + 6 * k + 2), s16(b, cursor + 6 * k + 4)))
        # read in threes, the cursor put back to the last vertex
        cursor += 6 * count
        cursor += cursor & 3
        while True:
            n = u16(b, cursor)
            cursor += 2
            if n == 0:
                break
            kind = u16(b, cursor)
            cursor += 2
            if kind not in COMPACT:
                problems.append('a group of type %d in a part' % kind)
                return verts, faces, cursor
            size, quad, corners_at, how, lit = COMPACT[kind]
            for k in range(n):
                o = cursor + k * size
                f = Face()
                f.tpage, f.clut, f.uvs = texture_words(b, o, quad)
                f.verts = [base + b[o + corners_at + c] for c in range(4 if quad else 3)]
                f.normals = compact_normals(b, o, how)[:4 if quad else 3]
                f.kind = kind
                f.lit = lit
                faces.append(f)
            cursor += n * size
        cursor += 2
        if u16(b, cursor - 2) == 0:
            break
    return verts, faces, cursor


class SkinVertex:
    """A vertex of the body as the game builds it: a sum of bones' turns
    of vectors, one bone's place, and a number added as it is."""
    __slots__ = ('terms', 'place', 'const', 'final')

    def __init__(self, terms, place, const, final):
        self.terms = terms
        self.place = place
        self.const = const
        self.final = final


def read_skinned(b, at, problems):
    """The body (MESH_SKINNED, render_DrawSkinnedMesh): its vertices as
    SkinVertex, its faces, and its normals (bone, vector)."""
    normals = []
    cursor = at + 2
    while True:
        bone = s16(b, cursor)
        cursor += 2
        if bone == -1:
            break
        count = s16(b, cursor)
        cursor += 2
        for k in range(count):
            normals.append((bone, (s16(b, cursor), s16(b, cursor + 2), s16(b, cursor + 4))))
            cursor += 6
    cursor -= 2
    slots = [None] * SCRATCH_POINTS
    verts = []
    faces = []
    point = 0
    for section in range(SECTIONS):
        bone = section  # skin_matrices[section]
        cursor += 2
        # turned by the bone and moved: kept as it comes out
        count = s16(b, cursor)
        cursor += 2
        for k in range(count):
            v = (s16(b, cursor), s16(b, cursor + 2), s16(b, cursor + 4))
            m = (s16(b, cursor + 6), s16(b, cursor + 8), s16(b, cursor + 10))
            slots[point] = SkinVertex([(bone, v)], bone, m, False)
            point += 1
            cursor += 12
        # built on a point: projected in its place
        count = s16(b, cursor)
        cursor += 2
        for k in range(count):
            idx = u16(b, cursor)
            v = (s16(b, cursor + 2), s16(b, cursor + 4), s16(b, cursor + 6))
            add = (s16(b, cursor + 8), s16(b, cursor + 10), s16(b, cursor + 12))
            on = slots[idx]
            if on is None or on.final:
                problems.append('section %d builds on point %d, which is %s' %
                                (section, idx, 'nothing' if on is None else 'a vertex'))
                on = SkinVertex([], bone, (0, 0, 0), False)
            sv = SkinVertex(on.terms + [(bone, v)], on.place,
                            tuple(on.const[j] + add[j] for j in range(3)), True)
            verts.append(sv)
            slots[idx] = sv
            cursor += 14
        # turned by the bone and the view: kept
        count = s16(b, cursor)
        cursor += 2
        for k in range(count):
            v = (s16(b, cursor), s16(b, cursor + 2), s16(b, cursor + 4))
            slots[point] = SkinVertex([(bone, v)], None, (0, 0, 0), False)
            point += 1
            cursor += 6
        # projected from a point
        count = s16(b, cursor)
        cursor += 2
        for k in range(count):
            idx = u16(b, cursor)
            v = (s16(b, cursor + 2), s16(b, cursor + 4), s16(b, cursor + 6))
            on = slots[idx]
            if on is None or on.final or on.place is not None:
                problems.append('section %d projects from point %d, which is not a turned one' %
                                (section, idx))
                on = SkinVertex([], None, (0, 0, 0), False)
            sv = SkinVertex(on.terms + [(bone, v)], bone, on.const, True)
            verts.append(sv)
            slots[idx] = sv
            cursor += 8
        # projected, three at a time
        count = s16(b, cursor)
        cursor += 2
        if count:
            for k in range(count):
                v = (s16(b, cursor + 6 * k), s16(b, cursor + 6 * k + 2), s16(b, cursor + 6 * k + 4))
                sv = SkinVertex([(bone, v)], bone, (0, 0, 0), True)
                verts.append(sv)
                slots[point + k] = sv
            # the listing reads whole threes and then puts the cursor back
            cursor += 6 * count
        # the groups
        cursor += 2
        cursor += cursor & 3
        while True:
            n = s16(b, cursor)
            cursor += 2
            if n == 0:
                break
            kind = u16(b, cursor)
            cursor += 2
            if kind not in (38, 39, 40, 41):
                problems.append('section %d has a group of type %d' % (section, kind))
                return normals, verts, faces
            quad = kind in (39, 41)
            size = 0x18 if quad else 0x14
            for k in range(n):
                o = cursor + k * size
                f = Face()
                f.tpage, f.clut, f.uvs = skin_texture_words(b, o, quad)
                corners = u32(b, o + (0x10 if quad else 0x0C))
                f.group = cursor - at
                f.corners = corners
                colors = u32(b, o + (0x14 if quad else 0x10))
                idx = [(corners >> (8 * c)) & 0xFF for c in range(4 if quad else 3)]
                f.verts = []
                for c in idx:
                    sv = slots[c]
                    if sv is None or not sv.final:
                        problems.append('section %d: a face names point %d, which is not a vertex' %
                                        (section, c))
                        f.verts = None
                        break
                    f.verts.append(id(sv))
                if f.verts is None:
                    continue
                c3 = [(colors >> (10 * j)) & 0x3FF for j in range(3)]
                if quad:
                    f.normals = [c3[0], c3[1], c3[2], u16(b, o + 0x0E)]
                else:
                    f.normals = [c3[2], c3[1], c3[0]]
                f.kind = kind
                faces.append(f)
            cursor += n * size
        if section == SECTIONS - 1:
            break
        # the carries: runs copied, read backwards and written forwards
        while u16(b, cursor) != 0xFFFF:
            frm, to, n = u16(b, cursor), u16(b, cursor + 2), s16(b, cursor + 4)
            cursor += 6
            while True:
                slots[to] = slots[frm]
                frm -= 1
                to += 1
                n -= 1
                if n == 0:
                    break
        cursor += 2
        point = u16(b, cursor)
        cursor += 2
    # faces name vertices by object; make them indices
    index = {id(v): i for i, v in enumerate(verts)}
    for f in faces:
        f.verts = [index[k] for k in f.verts]
    return normals, verts, faces


def skin_position(sv, rot, pos):
    """Where the game puts a body vertex for bones' world matrices `rot`,
    `pos` (by skin matrix number)."""
    p = [float(c) for c in sv.const]
    for bone, v in sv.terms:
        r = mat_vec(rot[SKIN_BONES[bone]], v)
        for k in range(3):
            p[k] += r[k]
    if sv.place is not None:
        for k in range(3):
            p[k] += pos[SKIN_BONES[sv.place]][k]
    return p


# ---- the textures ----

def load_vram(b, tex_section):
    """Video memory as the model's first texture group leaves it, the model
    in the first slot (tex_LoadGroup): a list of 16-bit words."""
    vram = [0] * (VRAM_W * VRAM_H)
    group = tex_section + (u32(b, tex_section) & ~3)
    count = u32(b, group)
    for k in range(count):
        tim_off, ix, iy, cx, cy = struct.unpack_from('<I4h', b, group + 4 + 12 * k)
        tim = tex_section + (tim_off & ~3)
        if b[tim] != 0x10:
            continue
        flags = u32(b, tim + 4)
        at = tim + 8
        if flags & 8:
            size = u32(b, at)
            w, h = u16(b, at + 8), u16(b, at + 10)
            put(vram, cx, cy, w, h, b, at + 12)
            at += size & ~3
        w, h = u16(b, at + 8), u16(b, at + 10)
        put(vram, ix, iy, w, h, b, at + 12)
    return vram


def put(vram, x, y, w, h, b, at):
    for row in range(h):
        line = struct.unpack_from('<%dH' % w, b, at + 2 * w * row)
        start = ((y + row) % VRAM_H) * VRAM_W
        for col in range(w):
            vram[start + (x + col) % VRAM_W] = line[col]


def texel(vram, tpage, clut, u, v):
    """The 16-bit colour a primitive's texture point reads."""
    page_x = (tpage & 0xF) * 64
    page_y = ((tpage >> 4) & 1) * 256
    depth = (tpage >> 7) & 3
    cx, cy = (clut & 0x3F) * 16, (clut >> 6) & 0x1FF
    y = (page_y + v) % VRAM_H
    if depth == 0:
        word = vram[y * VRAM_W + (page_x + (u >> 2)) % VRAM_W]
        index = (word >> ((u & 3) * 4)) & 15
        return vram[cy * VRAM_W + cx + index]
    if depth == 1:
        word = vram[y * VRAM_W + (page_x + (u >> 1)) % VRAM_W]
        index = (word >> ((u & 1) * 8)) & 255
        return vram[cy * VRAM_W + (cx + index) % VRAM_W]
    return vram[y * VRAM_W + (page_x + u) % VRAM_W]


def texel_index(vram, tpage, clut, u, v):
    """The colour table's entry a texture point reads, or -1 where the
    page is of colours, not of entries."""
    page_x = (tpage & 0xF) * 64
    page_y = ((tpage >> 4) & 1) * 256
    depth = (tpage >> 7) & 3
    y = (page_y + v) % VRAM_H
    if depth == 0:
        word = vram[y * VRAM_W + (page_x + (u >> 2)) % VRAM_W]
        return (word >> ((u & 3) * 4)) & 15
    if depth == 1:
        word = vram[y * VRAM_W + (page_x + (u >> 1)) % VRAM_W]
        return (word >> ((u & 1) * 8)) & 255
    return -1


def rgba_of(word):
    """A 15-bit colour as the GPU shows it; 0 is a texel of nothing."""
    if word == 0:
        return (0, 0, 0, 0)
    r = (word & 31) * 255 // 31
    g = ((word >> 5) & 31) * 255 // 31
    b = ((word >> 10) & 31) * 255 // 31
    return (r, g, b, 255)


class Atlas:
    """The textures the faces use, each (page, colour table) once, cut to
    the rectangle of texture points its faces reach, packed into one
    picture in rows."""
    PAD = 2

    def __init__(self, vram):
        self.vram = vram
        self.areas = {}

    def note(self, face):
        key = (face.tpage & 0x1FF, face.clut)
        us = [uv[0] for uv in face.uvs]
        vs = [uv[1] for uv in face.uvs]
        a = self.areas.get(key)
        box = [min(us), min(vs), max(us), max(vs)]
        if a is None:
            self.areas[key] = box
        else:
            self.areas[key] = [min(a[0], box[0]), min(a[1], box[1]), max(a[2], box[2]), max(a[3], box[3])]

    def pack(self, width=512):
        order = sorted(self.areas, key=lambda k: -(self.areas[k][3] - self.areas[k][1]))
        x = y = row_h = 0
        self.place = {}
        for key in order:
            u0, v0, u1, v1 = self.areas[key]
            w = u1 - u0 + 1 + 2 * self.PAD
            h = v1 - v0 + 1 + 2 * self.PAD
            if x + w > width:
                x, y, row_h = 0, y + row_h, 0
            self.place[key] = (x + self.PAD - u0, y + self.PAD - v0)
            x += w
            row_h = max(row_h, h)
        self.w = width
        self.h = 1
        while self.h < y + row_h:
            self.h *= 2
        pixels = bytearray(self.w * self.h * 4)
        # which entry of which colour table each texel is (rb_palette): red
        # the entry, green the table's number in self.cluts, alpha 255
        # where the texel is of a table at all
        index = bytearray(self.w * self.h * 4)
        tables = sorted({(key[1], (key[0] >> 7) & 3) for key in self.place if (key[0] >> 7) & 3 < 2})
        self.cluts = [((clut & 0x3F) * 16, (clut >> 6) & 0x1FF, 16 if depth == 0 else 256)
                      for clut, depth in tables]
        row_of = {t: k for k, t in enumerate(tables)}
        for key, (dx, dy) in self.place.items():
            u0, v0, u1, v1 = self.areas[key]
            tpage, clut = key
            row = row_of.get((clut, (tpage >> 7) & 3))
            # the area and a border of its own edge texels, so that a
            # filter that reads past an edge reads the edge
            for v in range(v0 - self.PAD, v1 + self.PAD + 1):
                for u in range(u0 - self.PAD, u1 + self.PAD + 1):
                    su = min(max(u, u0), u1)
                    sv = min(max(v, v0), v1)
                    c = rgba_of(texel(self.vram, tpage, clut, su & 255, sv & 255))
                    at = ((dy + v) * self.w + dx + u) * 4
                    pixels[at:at + 4] = bytes(c)
                    if row is not None:
                        entry = texel_index(self.vram, tpage, clut, su & 255, sv & 255)
                        index[at:at + 4] = bytes((entry, row, 0, 255))
        self.pixels = pixels
        self.index = index
        return self

    def palette(self):
        """The colour tables the texels are entries of, as the file has
        them: rb_palette's tables and colours."""
        return {'cluts': [list(c) for c in self.cluts],
                'colors': [[self.vram[y * VRAM_W + (x + k) % VRAM_W] for k in range(n)]
                           for x, y, n in self.cluts]}

    def uv(self, face, k):
        """The atlas point for corner `k`: the middle of the texel, as the
        chip reads a texel by its whole number."""
        dx, dy = self.place[(face.tpage & 0x1FF, face.clut)]
        u, v = face.uvs[k]
        return ((u + dx + 0.5) / self.w, (v + dy + 0.5) / self.h)


def png(w, h, rgba):
    def chunk(kind, data):
        c = struct.pack('>I', len(data)) + kind + data
        return c + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b''.join(b'\0' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


# ---- the weights ----

def fit_weights(sv, bones, scale, bind_rot, bind_pos, samples, rng):
    """A body vertex as a linear blend of at most two bones: the bones its
    sum names and the weight between them that comes nearest the game's
    sum over `samples` poses about the bind pose. Returns
    [(bone, weight)...], the worst miss in units."""
    names = sorted({SKIN_BONES[t[0]] for t in sv.terms} |
                   ({SKIN_BONES[sv.place]} if sv.place is not None else set()))
    rest = skin_position(sv, bind_rot, bind_pos)
    if len(names) == 1:
        return [(names[0], 1.0)], 0.0
    # each bone's own blend of the vertex: R_now R_bind^T (p - t_bind) + t_now
    rows = []
    for pose in samples:
        rot, pos = pose
        truth = skin_position(sv, rot, pos)
        each = []
        for bone in names:
            d = [rest[k] - bind_pos[bone][k] for k in range(3)]
            local = mat_vec(transpose(bind_rot[bone]), d)
            w = mat_vec(rot[bone], local)
            each.append([w[k] + pos[bone][k] for k in range(3)])
        rows.append((truth, each))
    # least squares for weights summing to one; two bones at a time
    best = None
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            num = den = 0.0
            for truth, each in rows:
                a, c = each[i], each[j]
                for k in range(3):
                    d = a[k] - c[k]
                    num += (truth[k] - c[k]) * d
                    den += d * d
            w = min(max(num / den if den > 1e-9 else 0.5, 0.0), 1.0)
            worst = 0.0
            for truth, each in rows:
                miss = math.sqrt(sum((w * each[i][k] + (1 - w) * each[j][k] - truth[k]) ** 2 for k in range(3)))
                worst = max(worst, miss)
            if best is None or worst < best[0]:
                best = (worst, [(names[i], w), (names[j], 1 - w)])
    return [p for p in best[1] if p[1] > 0], best[0]


def sample_poses(bones, scale, bind_turns, count, rng, spread=0.5):
    """Poses about the bind pose: every bone turned a little more about an
    axis of its own."""
    poses = []
    for _ in range(count):
        turns = {}
        for i in range(BONES):
            base = bind_turns.get(i, IDENTITY)
            axis = (rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))
            turns[i] = mat_mul(base, axis_angle(axis, rng.uniform(-spread, spread)))
        poses.append(world_pose(bones, scale, turns))
    return poses


# ---- glTF ----

class Glb:
    def __init__(self):
        self.blob = bytearray()
        self.views = []
        self.accessors = []

    def view(self, data, target=None):
        while len(self.blob) % 4:
            self.blob.append(0)
        v = {'buffer': 0, 'byteOffset': len(self.blob), 'byteLength': len(data)}
        if target:
            v['target'] = target
        self.blob += data
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, fmt, values, comp, kind, target=None, minmax=False):
        flat = [x for v in values for x in (v if isinstance(v, (list, tuple)) else (v,))]
        data = struct.pack('<%d%s' % (len(flat), fmt), *flat)
        a = {'bufferView': self.view(data, target), 'componentType': comp,
             'count': len(values), 'type': kind}
        if minmax:
            n = len(values[0])
            a['min'] = [min(v[k] for v in values) for k in range(n)]
            a['max'] = [max(v[k] for v in values) for k in range(n)]
        self.accessors.append(a)
        return len(self.accessors) - 1

    def write(self, path, gltf):
        gltf['buffers'] = [{'byteLength': len(self.blob)}]
        gltf['bufferViews'] = self.views
        gltf['accessors'] = self.accessors
        text = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
        while len(text) % 4:
            text += b' '
        while len(self.blob) % 4:
            self.blob.append(0)
        total = 12 + 8 + len(text) + 8 + len(self.blob)
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, total))
            f.write(struct.pack('<II', len(text), 0x4E4F534A) + text)
            f.write(struct.pack('<II', len(self.blob), 0x004E4942) + bytes(self.blob))


def quat_of(m):
    """A rotation matrix's quaternion (x, y, z, w)."""
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        return ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        return (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    if m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        return ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
    return ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)


def mat4_cols(rot, pos):
    """A glTF matrix: sixteen numbers, a column after a column."""
    return [rot[0][0], rot[1][0], rot[2][0], 0.0,
            rot[0][1], rot[1][1], rot[2][1], 0.0,
            rot[0][2], rot[1][2], rot[2][2], 0.0,
            pos[0], pos[1], pos[2], 1.0]


def inverse_of(rot, pos):
    rt = transpose(rot)
    p = mat_vec(rt, pos)
    return rt, [-p[0], -p[1], -p[2]]


def write_glb(path, name, bones, bind_rot, bind_pos, vertices, triangles, image_png, extras,
              index_png=None, terms=None):
    """vertices: (position, normal, uv, [(bone, weight)]) in game units.

    `index_png` is the picture of which colour table's entry each texel
    is (Atlas.index), written as the second image and named by
    extras.rb_palette. `terms` is, for each vertex, up to eight (bone,
    (x, y, z, h)): the vertex exactly as the game builds it, the sum of
    each bone's world matrix times its vector, h being how much of the
    bone's place is in it (the attributes _RB_TERM0 to _RB_TERM3, and
    _RB_TJOINTS the bones, which are the skin's joints' own numbers; up to
    eight, _RB_TERM4 to _RB_TERM7 and _RB_TJOINTS1, where a vertex has
    more than four)."""
    g = Glb()
    positions = [v[0] for v in vertices]
    normals = []
    for v in vertices:
        n = v[1]
        length = math.sqrt(sum(c * c for c in n)) or 1.0
        normals.append([c / length for c in n])
    uvs = [v[2] for v in vertices]
    joints = []
    weights = []
    for v in vertices:
        pairs = sorted(v[3], key=lambda p: -p[1])[:4]
        total = sum(p[1] for p in pairs) or 1.0
        js = [p[0] for p in pairs] + [0] * (4 - len(pairs))
        ws = [p[1] / total for p in pairs] + [0.0] * (4 - len(pairs))
        joints.append(js)
        weights.append(ws)
    flat_idx = [i for t in triangles for i in t]
    a_pos = g.accessor('f', positions, 5126, 'VEC3', 34962, minmax=True)
    a_nrm = g.accessor('f', normals, 5126, 'VEC3', 34962)
    a_uv = g.accessor('f', uvs, 5126, 'VEC2', 34962)
    a_jnt = g.accessor('B', joints, 5121, 'VEC4', 34962)
    a_wgt = g.accessor('f', weights, 5126, 'VEC4', 34962)
    a_idx = g.accessor('I', flat_idx, 5125, 'SCALAR', 34963)
    attributes = {'POSITION': a_pos, 'NORMAL': a_nrm, 'TEXCOORD_0': a_uv,
                  'JOINTS_0': a_jnt, 'WEIGHTS_0': a_wgt}
    if terms is not None:
        # four bones a vertex, or eight where one needs more
        width = 8 if any(len(t) > 4 for t in terms) else 4
        padded = [(list(t) + [(0, (0.0, 0.0, 0.0, 0.0))] * width)[:width] for t in terms]
        for half in range(width // 4):
            name = '_RB_TJOINTS' if half == 0 else '_RB_TJOINTS%d' % half
            attributes[name] = g.accessor('B', [[b for b, _ in t[4 * half:4 * half + 4]] for t in padded],
                                          5121, 'VEC4', 34962)
        for k in range(width):
            attributes['_RB_TERM%d' % k] = g.accessor('f', [list(t[k][1]) for t in padded], 5126, 'VEC4', 34962)
    ibm = []
    for i in range(BONES):
        r, p = inverse_of(bind_rot[i], bind_pos[i])
        ibm.append(mat4_cols(r, p))
    a_ibm = g.accessor('f', ibm, 5126, 'MAT4')
    image_view = g.view(image_png)
    images = [{'bufferView': image_view, 'mimeType': 'image/png'}]
    if index_png is not None:
        images.append({'bufferView': g.view(index_png), 'mimeType': 'image/png', 'name': 'rb_palette_index'})
        extras = dict(extras)
        extras['rb_palette'] = dict(extras.get('rb_palette', {}), image=1)
    nodes = []
    # node 0: glTF's y up and metres, the game's y down and its units
    nodes.append({'name': 'rb_game_space', 'children': [1, 1 + BONES],
                  'rotation': [1.0, 0.0, 0.0, 0.0], 'scale': [0.001, 0.001, 0.001]})
    # node 1..21: the bones, as the skeleton nests them
    for i, (parent, _, _) in enumerate(bones):
        if parent < 0:
            lr, lp = bind_rot[i], bind_pos[i]
        else:
            pr, pp = inverse_of(bind_rot[parent], bind_pos[parent])
            lr = mat_mul(pr, bind_rot[i])
            lp = [mat_vec(pr, bind_pos[i])[k] + pp[k] for k in range(3)]
        node = {'name': 'bone%02d' % i, 'translation': [float(c) for c in lp],
                'rotation': list(quat_of(lr))}
        kids = [1 + j for j, b in enumerate(bones) if b[0] == i]
        if kids:
            node['children'] = kids
        nodes.append(node)
    nodes.append({'name': name, 'mesh': 0, 'skin': 0})
    gltf = {
        'asset': {'version': '2.0', 'generator': 'ReBushido tools/fighter_export.py'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': nodes,
        'skins': [{'joints': [1 + i for i in range(BONES)], 'skeleton': 1, 'inverseBindMatrices': a_ibm}],
        'meshes': [{'name': name, 'primitives': [{
            'attributes': attributes,
            'indices': a_idx, 'material': 0}]}],
        'materials': [{'name': name, 'pbrMetallicRoughness': {
            'baseColorTexture': {'index': 0}, 'metallicFactor': 0.0, 'roughnessFactor': 1.0},
            'alphaMode': 'MASK', 'alphaCutoff': 0.5}],
        'textures': [{'source': 0, 'sampler': 0}],
        'samplers': [{'magFilter': 9728, 'minFilter': 9728, 'wrapS': 33071, 'wrapT': 33071}],
        'images': images,
        'extras': extras,
    }
    g.write(path, gltf)


# ---- a model ----

class ModelData:
    """A model as the game builds it, before it is written as a file:

    - points: each a place at the bind pose, `terms` (bone -> [x, y, z, h],
      the vertex at any pose being the sum over them of the bone's world
      rotation times (x, y, z) plus h times its place) and `weights` (the
      linear blend nearest to that, [(bone, weight)]);
    - faces: each the points of its corners in order round the face, and
      for each corner its point in the atlas and its normal at the bind
      pose ((0, 0, 0) where the face is not lit);
    - the atlas and its colour tables, and what the file's extras say."""

    def __init__(self, name):
        self.name = name
        self.points = []
        self.faces = []
        self.problems = []
        self.worst_fit = 0.0
        self.constants = 0

    def point(self, place, terms, weights):
        self.points.append((tuple(place), terms, weights))
        return len(self.points) - 1


def add_term(terms, bone, vector, h):
    t = terms.setdefault(bone, [0.0, 0.0, 0.0, 0.0])
    for k in range(3):
        t[k] += vector[k]
    t[3] += h


def place_of_terms(terms, rot, pos):
    p = [0.0, 0.0, 0.0]
    for bone, t in terms.items():
        r = mat_vec(rot[bone], t[:3])
        for k in range(3):
            p[k] += r[k] + t[3] * pos[bone][k]
    return p


def corner_order(n):
    """The corners of a face in order round it: the chip draws a quad's
    corners as two triangles, 0 1 2 and 1 3 2."""
    return [0, 1, 3, 2] if n == 4 else [0, 1, 2]


def read_model(nd, model_id, bones, scales, pose_path, samples_count):
    name = MODEL_NAMES[model_id]
    b = nd.read(model_id)
    n = u32(b, 0)
    offs = struct.unpack_from('<%dI' % (n + 1), b, 4)
    md = ModelData(name)
    problems = md.problems
    # the model's id in the game: the third set's files are read from 27
    # NDATA ids below their own (ndata_ReadModel)
    asset = model_id if model_id < 31 else model_id + 27
    scale = scales[asset] if asset < len(scales) and scales[asset] != 0xFFFF else 4096
    mesh_table = offs[0]
    mesh_count = u32(b, mesh_table)
    mesh_at = [mesh_table + o for o in struct.unpack_from('<%dI' % mesh_count, b, mesh_table + 4)]
    # which bone draws each mesh (model_AssignMeshes: bones[node + 1])
    pairs = []
    p = offs[1]
    while u16(b, p) != 0xFFFF:
        pairs.append((u16(b, p), u16(b, p + 2) + 1))
        p += 4
    cloth = cloth_meshes(b, offs[2])
    if pose_path:
        bind_rot, bind_pos = pose_from_file(pose_path)
        # each bone's own turn from its parent's, for the poses the
        # weights are fitted over
        bind_turns = {i: mat_mul(transpose(bind_rot[parent]), bind_rot[i])
                      for i, (parent, _, _) in enumerate(bones) if parent >= 0}
        bind_turns[0] = bind_rot[0]
    else:
        bind_rot, bind_pos = stand_pose(bones, scale)
        bind_turns = {i: rot_zyx(*a) for i, a in STAND.items()}
    md.bind_rot, md.bind_pos, md.asset, md.scale = bind_rot, bind_pos, asset, scale

    vram = load_vram(b, offs[n - 1])
    atlas = Atlas(vram)
    skin_rot = [bind_rot[SKIN_BONES[i]] for i in range(len(SKIN_BONES))]

    if u16(b, mesh_at[0]) & 0x8000 == 0:
        raise SystemExit('fighter_export: %s: mesh 0 is not a skinned body' % name)
    normals, skin_verts, skin_faces = read_skinned(b, mesh_at[0], problems)
    rng = random.Random(model_id)
    samples = sample_poses(bones, scale, bind_turns, samples_count, rng)
    for sv in skin_verts:
        terms = {}
        for bone, v in sv.terms:
            add_term(terms, SKIN_BONES[bone], v, 0.0)
        if sv.place is not None:
            add_term(terms, SKIN_BONES[sv.place], (0.0, 0.0, 0.0), 1.0)
        if any(sv.const):
            # a number added as it is, the same at every pose: kept on the
            # bone the vertex is placed by, which is exact at the bind pose
            md.constants += 1
            bone = SKIN_BONES[sv.place] if sv.place is not None else 0
            add_term(terms, bone, mat_vec(transpose(bind_rot[bone]), sv.const), 0.0)
        w, miss = fit_weights(sv, bones, scale, bind_rot, bind_pos, samples, rng)
        md.worst_fit = max(md.worst_fit, miss)
        md.point(skin_position(sv, bind_rot, bind_pos), terms, w)
    for f in skin_faces:
        atlas.note(f)
    part_meshes = []
    for mesh, bone in pairs:
        if mesh in cloth:
            continue
        verts, faces, _ = read_compact(b, mesh_at[mesh], problems)
        part_meshes.append((mesh, bone, verts, faces))
        for f in faces:
            atlas.note(f)
    atlas.pack()

    def skin_normal(f, c):
        bone, nv = normals[f.normals[c]]
        return mat_vec(skin_rot[bone], nv)

    for f in skin_faces:
        md.faces.append(face_of(atlas, f, f.verts, skin_normal))
    for mesh, bone, verts, faces in part_meshes:
        r, t = bind_rot[bone], bind_pos[bone]
        first = len(md.points)
        for v in verts:
            q = mat_vec(r, v)
            md.point([q[k] + t[k] for k in range(3)], {bone: [float(c) for c in v] + [1.0]},
                     [(bone, 1.0)])
        for f in faces:
            md.faces.append(face_of(atlas, f, [first + i for i in f.verts],
                                    lambda f, c, r=r: mat_vec(r, f.normals[c])))
    md.atlas = atlas
    md.extras = {
        'rb_fighter': 1,
        'model_id': asset,
        'model_file': name,
        'scale': scale,
        'units': 'game',
        'joints': ['bone%02d' % i for i in range(BONES)],
        'bind_rotation': [[round(x, 7) for row in bind_rot[i] for x in row] for i in range(BONES)],
        'bind_translation': [[round(x, 4) for x in bind_pos[i]] for i in range(BONES)],
        'rb_palette': atlas.palette(),
        'parts': [[m, bone] for m, bone, _, _ in part_meshes],
    }
    # the terms are the game's own sum: held to it at the samples
    worst = 0.0
    for rot, pos in samples[:4]:
        for sv, (_, terms, _) in zip(skin_verts, md.points):
            if any(sv.const):
                continue
            a = skin_position(sv, rot, pos)
            q = place_of_terms(terms, rot, pos)
            worst = max(worst, max(abs(a[k] - q[k]) for k in range(3)))
    if worst > 0.01:
        problems.append('the terms miss the game\'s sum by %.3f' % worst)
    md.summary = ('body %d vertices %d faces; %d parts; atlas %dx%d, %d colour tables; '
                  'linear blend within %.2f units; %d with a constant'
                  % (len(skin_verts), len(skin_faces), len(part_meshes), atlas.w, atlas.h,
                     len(atlas.cluts), md.worst_fit, md.constants))
    return md


def face_of(atlas, f, points, normal_of):
    """A face of the game as ModelData keeps it: corners round the face."""
    order = corner_order(len(f.verts))
    lit = f.lit
    return {'points': [points[c] for c in order],
            'uvs': [atlas.uv(f, c) for c in order],
            'normals': [tuple(normal_of(f, c)) if lit else (0.0, 0.0, 0.0) for c in order]}


# The weapons: the files of each set hold every model of it, two meshes a
# model (the thing and its shadow), and the textures in groups
# (resource_LoadWeaponFile). The file read is the set's last, which has no
# sounds.
WEAPON_FILES = {0: 0x83 + 136, 1: 0x10C + 36}
WEAPON_SET_1_FIRST = 51
WEAPON_MODELS = 58


def weapon_tex_groups():
    """g_resource_weapon_tex_groups, from src/main/resource.c."""
    with open(os.path.join(ROOT, 'src', 'main', 'resource.c'), encoding='utf-8') as f:
        text = f.read()
    m = re.search(r'g_resource_weapon_tex_groups\[[^\]]*\] = \{(.*?)\};', text, re.S)
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    return [int(v) for v in re.findall(r'-?\d+', body)]


def load_group(b, tex_section, group):
    """Video memory with one texture group of a file loaded where the file
    says (tex_LoadGroup)."""
    vram = [0] * (VRAM_W * VRAM_H)
    at = tex_section + (u32(b, tex_section + 4 * group) & ~3)
    count = u32(b, at)
    for k in range(count):
        tim_off, ix, iy, cx, cy = struct.unpack_from('<I4h', b, at + 4 + 12 * k)
        tim = tex_section + (tim_off & ~3)
        if b[tim] != 0x10:
            continue
        flags = u32(b, tim + 4)
        p = tim + 8
        if flags & 8:
            size = u32(b, p)
            w, h = u16(b, p + 8), u16(b, p + 10)
            put(vram, cx, cy, w, h, b, p + 12)
            p += size & ~3
        w, h = u16(b, p + 8), u16(b, p + 10)
        put(vram, ix, iy, w, h, b, p + 12)
    return vram


def read_weapon(nd, weapon):
    """A weapon model (enum WeaponModel) as ModelData: its mesh is drawn on
    the weapon's own world matrix (weapon_Queue), which the port gives as
    bone 0's; every other bone is left at the bind pose."""
    weapon_set = 1 if weapon >= WEAPON_SET_1_FIRST else 0
    k = weapon - (WEAPON_SET_1_FIRST if weapon_set else 0)
    b = nd.read(WEAPON_FILES[weapon_set])
    n = u32(b, 0)
    offs = struct.unpack_from('<%dI' % (n + 1), b, 4)
    md = ModelData('W%02d' % weapon)
    mesh_table = offs[0]
    at = mesh_table + u32(b, mesh_table + 4 + 4 * (2 * k))
    # the group is looked up by the model's place in its set, as the game
    # does (resource_LoadWeaponFile)
    vram = load_group(b, offs[n - 1], weapon_tex_groups()[k])
    atlas = Atlas(vram)
    verts, faces, _ = read_compact(b, at, md.problems)
    for f in faces:
        atlas.note(f)
    atlas.pack(width=256)
    for v in verts:
        md.point([float(c) for c in v], {0: [float(c) for c in v] + [1.0]}, [(0, 1.0)])
    for f in faces:
        md.faces.append(face_of(atlas, f, f.verts, lambda f, c: f.normals[c]))
    md.bind_rot = [IDENTITY] * BONES
    md.bind_pos = [[0.0, 0.0, 0.0]] * BONES
    md.atlas = atlas
    md.asset = weapon
    md.extras = {
        'rb_fighter': 1,
        'rb_weapon': weapon,
        'model_file': md.name,
        'units': 'game',
        'joints': ['bone%02d' % i for i in range(BONES)],
        'bind_rotation': [[round(x, 7) for row in md.bind_rot[i] for x in row] for i in range(BONES)],
        'bind_translation': [[0.0, 0.0, 0.0] for _ in range(BONES)],
        'rb_palette': atlas.palette(),
    }
    md.summary = '%d vertices %d faces; atlas %dx%d, %d colour tables' % (
        len(verts), len(faces), atlas.w, atlas.h, len(atlas.cluts))
    return md


def triangles_of(md):
    """The model's faces as the file's vertices and triangles: a vertex
    for each point, texture point and normal; a face of n corners as
    n - 2 triangles."""
    vertices, triangles, terms, lookup = [], [], [], {}
    for face in md.faces:
        idx = []
        for p, uv, nrm in zip(face['points'], face['uvs'], face['normals']):
            key = (p, tuple(round(c, 7) for c in uv), tuple(round(c, 5) for c in nrm))
            if key not in lookup:
                lookup[key] = len(vertices)
                place, t, w = md.points[p]
                vertices.append((place, nrm, uv, w))
                terms.append(sorted(((bone, tuple(v)) for bone, v in t.items()),
                                    key=lambda bv: -(abs(bv[1][3]) * 1e6 + sum(abs(c) for c in bv[1][:3]))))
            idx.append(lookup[key])
        for c in range(1, len(idx) - 1):
            triangles.append((idx[0], idx[c], idx[c + 1]))
    return vertices, triangles, terms


def write_model(md, path, bones, image_png=None):
    vertices, triangles, terms = triangles_of(md)
    write_glb(path, md.name, bones, md.bind_rot, md.bind_pos, vertices, triangles,
              image_png or png(md.atlas.w, md.atlas.h, md.atlas.pixels), md.extras,
              index_png=png(md.atlas.w, md.atlas.h, md.atlas.index), terms=terms)
    return len(vertices), len(triangles)


def export_model(nd, model_id, out, bones, scales, pose_path, samples_count, quiet=False):
    md = read_model(nd, model_id, bones, scales, pose_path, samples_count)
    return export_data(md, out, bones, quiet)


def export_data(md, out, bones, quiet=False):
    nv, nt = write_model(md, os.path.join(out, md.name + '.glb'), bones)
    with open(os.path.join(out, md.name + '.png'), 'wb') as f:
        f.write(png(md.atlas.w, md.atlas.h, md.atlas.pixels))
    if not quiet:
        print('%s: %s; %d vertices, %d triangles written; %s'
              % (md.name, md.summary, nv, nt,
                 'ok' if not md.problems else '%d problems: %s' % (len(md.problems), '; '.join(md.problems[:5]))))
    return not md.problems


def cloth_meshes(b, at):
    """The meshes the cloth section names as cloth (cloth_SetupModel)."""
    meshes = set()
    p = at
    mesh = 0
    while True:
        v = s16(b, p)
        if v == -3:
            break
        if v == -2:
            p += 2
            mesh += 1
            continue
        meshes.add(mesh)
        # a cloth record: groups up to -1, then colliders up to -2
        while s16(b, p) != -2:
            p += 2
        p += 2
        mesh += 1
    return meshes


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', default=os.path.join(ROOT, 'Bushido Blade 2 (USA).cue'))
    ap.add_argument('--models', help='model ids (0..36), or "all"; 3 if neither this nor --weapons')
    ap.add_argument('--out', default=os.path.join(ROOT, 'tmp', 'fighter_models'))
    ap.add_argument('--pose', help='a bind pose the port wrote (--fighter-pose)')
    ap.add_argument('--samples', type=int, default=24)
    ap.add_argument('--weapons', help='weapon models (0..57), or "all"')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    nd = Ndata(args.image)
    bones, scales = source_tables()
    models = args.models or ('' if args.weapons else '3')
    ids = range(len(MODEL_NAMES)) if models == 'all' else [int(x) for x in models.split(',') if x]
    good = True
    if args.weapons:
        weapons = range(WEAPON_MODELS) if args.weapons == 'all' else [int(x) for x in args.weapons.split(',')]
        for weapon in weapons:
            good &= export_data(read_weapon(nd, weapon), args.out, bones)
    for model_id in ids:
        good &= export_model(nd, model_id, args.out, bones, scales, args.pose, args.samples)
    return 0 if good else 1


if __name__ == '__main__':
    sys.exit(main())
