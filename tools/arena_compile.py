#!/usr/bin/env python3
"""Compiles a custom arena (docs/MODS.md, "Custom arenas"): a scene made in
Blender (or any program that writes glTF or OBJ) and a text file of the
stage's tables, into the `.arena` file the port reads -- the game's own
ground file (docs/formats/GROUND.md) with the tables before it.
Standard library alone.

    python3 tools/arena_compile.py SOURCE_FOLDER OUT.arena

SOURCE_FOLDER has `arena.txt`:

    name = Stone courtyard
    scene = courtyard.gltf     # .gltf, .glb or .obj (with its .mtl)
    scale = 1000               # the game's units to the scene's one (a metre)
    stands_in_for = 25         # the stage it is fought in place of
    music = 2                  # and the stage's tables: port_arenas.h
    sounds = 21
    fog = on
    fog_span = 32000
    light = 0xC8C0B0 0x403C48 0xB8B0A0 0x383440 0x0CA20E9F 0x000000
    texture flagstone = flagstone.png   # a material's picture, if the
                                        # scene does not name it

and the scene, whose objects are told apart by their names:

    collision...   what the fighters stand on and walk into, each face's
                   surface its material's name: firm, firm2, sand, sand2,
                   wet, wet2, barrier, plain (a wall); anything after a `.`
                   or `_` is the author's own (`sand_yard`, `plain.wall`)
    start1a, start1b ... start4b
                   the two fighters' places of each of four pairs: an
                   object's middle; a pair not given is the first again
    obstacle...    a post a blade stops at: its middle, and its top
    anything else  what is drawn: each material's picture, 64 by 64 at
                   most, in 16 colours

The scene is Y up, metres, as glTF and Blender's exporters write it: the
game's x is the scene's x, its y the scene's -y (the game's y grows
downwards) and its z the scene's -z. A face is seen, and met, from its
front: counter-clockwise, as both formats have it.

What is made, within the game's own limits (32 by 32 cells of 2,000 units
centred on the origin, a ground no longer than the host's on the disc):
  - the meshes: one tile a cell (two or more where a cell has more than
    256 corners), each drawn face cut at the cell's edges (and 20 units
    over them, as the disc's tiles lie over their neighbours' edges);
  - the collision grid: each collision face cut to every cell it crosses,
    250 units over the cell's edges as the disc's are, its outline grown
    by 2 units so that two faces leave no seam;
  - the obstacles, the cells' tables and the textures (each with its
    smaller copies at half and a quarter of its place in the page, which
    the renderer reads far off).
The validator, tools/arena_validate.py, holds the result to what a fight
needs.
"""
import json
import math
import os
import struct
import sys
import zlib
import base64

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_ground  # noqa: E402
import ground_cast  # noqa: E402

CELLS = 32
CELL = 2000
HALF = 32000
COLLISION_MARGIN = 250   # how far a face is kept past its cell's edges
COLLISION_GROW = 2       # and how much its outline is grown
TILE_OVERLAP = 0         # how far a tile's faces lie over its neighbours'
TILE_CORNERS_MAX = 256   # a record names its corners by a byte
TILES_MAX = 500          # STAGE_MAX_TILES
RUN_ENTRIES_MAX = 1000   # STAGE_MAX_RUN_ENTRIES
ONE_GRID = 0x4000        # the collision grid's 1
ONE_MESH = 4096          # a mesh's normal's 1
PROJECTION_MAX = 8000    # what the GTE's projection on a face reaches

# the textures: 64 by 64 in a quarter of a page, four to a page, three
# pages (video memory's 640 to 831 across, 256 down: the disc's stages'
# own); the colour tables in row 480 (480 + 1 when loaded: the fog's)
TEXTURE_SIZE = 64
TEXTURE_PAGES = (10, 11, 12)
TEXTURE_SLOTS = [(128, 128), (192, 128), (128, 192), (192, 192)]
TEXTURES_MAX = len(TEXTURE_PAGES) * len(TEXTURE_SLOTS)
CLUT_ROW = 480
COLORS = 16
TPAGE_BLEND = 1 << 5     # the disc's stages' (unused: no face is see-through)
CODE_TRI = 0x24          # POLY_FT3's code
CODE_QUAD = 0x2C         # POLY_FT4's
TYPE_TRIS = 4
TYPE_QUADS = 12
QUAD_BITS = 2            # cut near the camera, as the disc's large quads
CUT_LONGER_THAN = 1200   # a face is cut only if an edge is longer

SURFACES = {name: i for i, name in enumerate(ground_cast.SURFACE_NAMES)}
SURFACES.update({'wall': 0, 'edge': 2})

# the text lines arena.txt may have that go into the .arena as they are
TABLE_NAMES = ('name', 'stands_in_for', 'start1', 'start2', 'start3', 'start4', 'light',
               'fog', 'fog_span', 'fog_farthest', 'music', 'sounds', 'chain', 'lights')


class ArenaError(Exception):
    pass


# ---- small arithmetic ----

def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def add(a, b):
    return [a[i] + b[i] for i in range(3)]


def mul(a, k):
    return [a[i] * k for i in range(3)]


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def norm(a):
    length = math.sqrt(dot(a, a))
    return [x / length for x in a] if length > 0 else [0.0, 0.0, 0.0]


def polygon_normal(points):
    """Newell's normal of a polygon, not made unit."""
    n = [0.0, 0.0, 0.0]
    for i in range(len(points)):
        a, b = points[i], points[(i + 1) % len(points)]
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    return n


# ---- PNG (8 bits a channel; grey, RGB, palette, with or without alpha) ----

def read_png(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ArenaError('a texture is not a PNG')
    at = 8
    idat = b''
    palette = None
    while at < len(data):
        length, tag = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if tag == b'IHDR':
            width, height, depth, kind, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif tag == b'PLTE':
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif tag == b'IDAT':
            idat += body
    if depth != 8 or interlace:
        raise ArenaError('a texture is not 8 bits a channel, not interlaced')
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[kind]
    raw = zlib.decompress(idat)
    stride = width * channels
    rows = []
    prev = bytearray(stride)
    at = 0
    for y in range(height):
        f = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append(line)
        prev = line
    pixels = []
    for line in rows:
        for x in range(width):
            px = line[x * channels:(x + 1) * channels]
            if kind == 0:
                pixels.append((px[0], px[0], px[0]))
            elif kind == 4:
                pixels.append((px[0], px[0], px[0]))
            elif kind == 3:
                pixels.append(palette[px[0]])
            else:
                pixels.append(tuple(px[:3]))
    return width, height, pixels


def resize(width, height, pixels, size):
    """A box filter to size by size."""
    out = []
    for y in range(size):
        y0, y1 = y * height // size, max(y * height // size + 1, (y + 1) * height // size)
        for x in range(size):
            x0, x1 = x * width // size, max(x * width // size + 1, (x + 1) * width // size)
            r = g = b = n = 0
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    p = pixels[yy * width + xx]
                    r += p[0]
                    g += p[1]
                    b += p[2]
                    n += 1
            out.append((r // n, g // n, b // n))
    return out


def median_cut(pixels, count):
    boxes = [list(pixels)]
    while len(boxes) < count:
        # the box with the widest channel
        best = None
        for i, box in enumerate(boxes):
            if len(box) < 2:
                continue
            for ch in range(3):
                values = [p[ch] for p in box]
                spread = max(values) - min(values)
                if best is None or spread > best[0]:
                    best = (spread, i, ch)
        if best is None or best[0] == 0:
            break
        _, i, ch = best
        box = sorted(boxes.pop(i), key=lambda p: p[ch])
        boxes += [box[:len(box) // 2], box[len(box) // 2:]]
    palette = []
    for box in boxes:
        n = len(box)
        palette.append(tuple(sum(p[c] for p in box) // n for c in range(3)))
    while len(palette) < count:
        palette.append(palette[-1])
    return palette


def nearest(palette, p):
    return min(range(len(palette)),
               key=lambda i: sum((palette[i][c] - p[c]) ** 2 for c in range(3)))


def color15(rgb):
    """BGR555; black is drawn with the top bit, which on an opaque face is
    black and not the clear colour 0."""
    r, g, b = (c >> 3 for c in rgb)
    word = r | (g << 5) | (b << 10)
    return word if word else 0x8000


def make_tim(indices, size, clut, image_xy, clut_xy):
    """A 4-bit TIM: its colour table and picture (tex_LoadStageGroup puts
    them where the record says)."""
    clut_data = struct.pack('<16H', *clut)
    clut_block = struct.pack('<I4H', 12 + len(clut_data), clut_xy[0], clut_xy[1], COLORS, 1) + clut_data
    image = bytearray()
    for y in range(size):
        for x in range(0, size, 2):
            image.append(indices[y * size + x] | (indices[y * size + x + 1] << 4))
    image_block = struct.pack('<I4H', 12 + len(image), image_xy[0], image_xy[1], size // 4, size) + bytes(image)
    return struct.pack('<II', 0x10, 8) + clut_block + image_block


# ---- the scene ----

class Scene:
    """Polygons in the game's own frame, by object: (object name,
    material name, [game point], [texture point or None])."""

    def __init__(self):
        self.polygons = []
        self.points = {}       # an object's name: its points (for its middle)
        self.textures = {}     # a material's name: the PNG's bytes


def to_game(p, scale):
    return [p[0] * scale, -p[1] * scale, -p[2] * scale]


def read_obj(path, scale):
    scene = Scene()
    folder = os.path.dirname(path)
    vs, vts = [], []
    obj = 'default'
    material = ''
    mtl_files = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            w = line.split()
            if not w or w[0].startswith('#'):
                continue
            if w[0] == 'v':
                vs.append([float(x) for x in w[1:4]])
            elif w[0] == 'vt':
                vts.append([float(w[1]), float(w[2])])
            elif w[0] in ('o', 'g'):
                obj = ' '.join(w[1:]) or 'default'
            elif w[0] == 'usemtl':
                material = ' '.join(w[1:])
            elif w[0] == 'mtllib':
                mtl_files.append(' '.join(w[1:]))
            elif w[0] == 'f':
                points, uvs = [], []
                for ref in w[1:]:
                    parts = ref.split('/')
                    vi = int(parts[0])
                    p = vs[vi - 1 if vi > 0 else vi]
                    points.append(to_game(p, scale))
                    if len(parts) > 1 and parts[1]:
                        ti = int(parts[1])
                        t = vts[ti - 1 if ti > 0 else ti]
                        uvs.append([t[0], 1.0 - t[1]])
                    else:
                        uvs.append(None)
                scene.polygons.append((obj, material, points, uvs))
                scene.points.setdefault(obj, []).extend(points)
    for name in mtl_files:
        current = None
        with open(os.path.join(folder, name), encoding='utf-8') as f:
            for line in f:
                w = line.split()
                if not w:
                    continue
                if w[0] == 'newmtl':
                    current = ' '.join(w[1:])
                elif w[0] == 'map_Kd' and current is not None:
                    with open(os.path.join(folder, w[-1]), 'rb') as png:
                        scene.textures[current] = png.read()
    return scene


def quat_matrix(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def node_matrix(node):
    """A node's transform as a 4 by 4 (rows)."""
    if 'matrix' in node:
        m = node['matrix']  # column-major
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    t = node.get('translation', [0, 0, 0])
    r = quat_matrix(node.get('rotation', [0, 0, 0, 1]))
    s = node.get('scale', [1, 1, 1])
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def read_gltf(path, scale):
    scene = Scene()
    folder = os.path.dirname(path)
    with open(path, 'rb') as f:
        data = f.read()
    chunks = []
    if data[:4] == b'glTF':
        at = 12
        while at < len(data):
            length, kind = struct.unpack('<II', data[at:at + 8])
            chunks.append(data[at + 8:at + 8 + length])
            at += 8 + length
        doc = json.loads(chunks[0])
    else:
        doc = json.loads(data.decode('utf-8'))
    buffers = []
    for i, b in enumerate(doc.get('buffers', [])):
        uri = b.get('uri')
        if uri is None:
            buffers.append(chunks[1])
        elif uri.startswith('data:'):
            buffers.append(base64.b64decode(uri.split(',', 1)[1]))
        else:
            with open(os.path.join(folder, uri), 'rb') as f:
                buffers.append(f.read())

    def view_bytes(index):
        view = doc['bufferViews'][index]
        buf = buffers[view['buffer']]
        start = view.get('byteOffset', 0)
        return buf[start:start + view['byteLength']], view.get('byteStride')

    def accessor(index):
        acc = doc['accessors'][index]
        blob, stride = view_bytes(acc['bufferView'])
        comps = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[acc['type']]
        fmt = {5126: 'f', 5123: 'H', 5125: 'I', 5121: 'B'}[acc['componentType']]
        size = struct.calcsize(fmt)
        stride = stride or size * comps
        start = acc.get('byteOffset', 0)
        out = []
        for i in range(acc['count']):
            vals = struct.unpack_from('<%d%s' % (comps, fmt), blob, start + i * stride)
            out.append(vals if comps > 1 else vals[0])
        return out

    # the materials' pictures
    for mat in doc.get('materials', []):
        tex = mat.get('pbrMetallicRoughness', {}).get('baseColorTexture')
        if tex is None:
            continue
        image = doc['images'][doc['textures'][tex['index']]['source']]
        if 'uri' in image:
            uri = image['uri']
            if uri.startswith('data:'):
                scene.textures[mat['name']] = base64.b64decode(uri.split(',', 1)[1])
            else:
                with open(os.path.join(folder, uri), 'rb') as f:
                    scene.textures[mat['name']] = f.read()
        else:
            scene.textures[mat['name']] = view_bytes(image['bufferView'])[0]

    def walk(index, parent):
        node = doc['nodes'][index]
        m = mat_mul(parent, node_matrix(node))
        name = node.get('name', 'node%d' % index)
        # an object with no mesh is a point (an empty): its place
        place = [m[0][3], m[1][3], m[2][3]]
        scene.points.setdefault(name, [])
        if 'mesh' not in node:
            scene.points[name].append(to_game(place, scale))
        else:
            for prim in doc['meshes'][node['mesh']]['primitives']:
                if prim.get('mode', 4) != 4:
                    continue
                pos = accessor(prim['attributes']['POSITION'])
                uv = accessor(prim['attributes']['TEXCOORD_0']) if 'TEXCOORD_0' in prim['attributes'] else None
                idx = accessor(prim['indices']) if 'indices' in prim else list(range(len(pos)))
                mname = doc['materials'][prim['material']]['name'] if 'material' in prim else ''
                world = [to_game([sum(m[r][k] * p[k] for k in range(3)) + m[r][3] for r in range(3)], scale)
                         for p in pos]
                for t in range(0, len(idx) - 2, 3):
                    tri = idx[t:t + 3]
                    points = [world[i] for i in tri]
                    uvs = [list(uv[i]) for i in tri] if uv else [None] * 3
                    scene.polygons.append((name, mname, points, uvs))
                    scene.points[name].extend(points)
        for child in node.get('children', []):
            walk(child, m)

    identity = [[1 if i == j else 0 for j in range(4)] for i in range(4)]
    roots = doc['scenes'][doc.get('scene', 0)]['nodes'] if 'scenes' in doc else range(len(doc['nodes']))
    for r in roots:
        walk(r, identity)
    return scene


def merge_coplanar_triangles(polygons):
    """glTF gives triangles: two that share an edge, lie in one plane and
    make a convex quad with one material are one quad, which is fewer
    records drawn and fewer faces cast against."""
    out = []
    used = [False] * len(polygons)
    by_edge = {}
    keyed = []
    for i, (obj, mat, pts, uvs) in enumerate(polygons):
        key = [tuple(round(c, 3) for c in p) for p in pts]
        keyed.append(key)
        if len(pts) == 3:
            for e in range(3):
                a, b = key[e], key[(e + 1) % 3]
                by_edge.setdefault((b, a), []).append((i, e))
    for i, (obj, mat, pts, uvs) in enumerate(polygons):
        if used[i]:
            continue
        used[i] = True
        if len(pts) != 3:
            out.append((obj, mat, pts, uvs))
            continue
        merged = None
        for e in range(3):
            a, b = keyed[i][e], keyed[i][(e + 1) % 3]
            for j, f in by_edge.get((a, b), []):
                if used[j] or polygons[j][0] != obj or polygons[j][1] != mat:
                    continue
                # quad: a(=i[e]) third_of_j b(=i[e+1]) third_of_i, in i's order
                o = polygons[j]
                third_j = (f + 2) % 3
                quad = [pts[e], o[2][third_j], pts[(e + 1) % 3], pts[(e + 2) % 3]]
                quv = [uvs[e], o[3][third_j], uvs[(e + 1) % 3], uvs[(e + 2) % 3]]
                n1 = norm(polygon_normal(pts))
                n2 = norm(polygon_normal(o[2]))
                if dot(n1, n2) < 0.9999 or not convex(quad, n1):
                    continue
                if uvs[e] is not None and o[3][(f + 1) % 3] is not None:
                    if any(abs(uvs[e][k] - o[3][(f + 1) % 3][k]) > 1e-4 for k in range(2)) or \
                            any(abs(uvs[(e + 1) % 3][k] - o[3][f][k]) > 1e-4 for k in range(2)):
                        continue
                merged = (j, quad, quv)
                break
            if merged:
                break
        if merged:
            used[merged[0]] = True
            out.append((obj, mat, merged[1], merged[2]))
        else:
            out.append((obj, mat, pts, uvs))
    return out


def convex(points, n):
    sign = 0
    for i in range(len(points)):
        a, b, c = points[i], points[(i + 1) % len(points)], points[(i + 2) % len(points)]
        s = dot(cross(sub(b, a), sub(c, b)), n)
        if abs(s) < 1e-6:
            continue
        if sign == 0:
            sign = 1 if s > 0 else -1
        elif (s > 0) != (sign > 0):
            return False
    return True


def ear_clip(points, uvs, n):
    """A concave polygon as triangles."""
    idx = list(range(len(points)))
    tris = []
    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = points[i0], points[i1], points[i2]
            if dot(cross(sub(b, a), sub(c, a)), n) <= 0:
                continue
            if any(inside_tri(points[j], a, b, c, n) for j in idx if j not in (i0, i1, i2)):
                continue
            tris.append(([a, b, c], [uvs[i0], uvs[i1], uvs[i2]]))
            idx.pop(k)
            break
        else:
            break
    tris.append(([points[i] for i in idx], [uvs[i] for i in idx]))
    return tris


def inside_tri(p, a, b, c, n):
    for x, y in ((a, b), (b, c), (c, a)):
        if dot(cross(sub(y, x), sub(p, x)), n) < 0:
            return False
    return True


def convex_pieces(points, uvs):
    """A face as convex, planar pieces, each counter-clockwise about its
    front normal (the game's frame)."""
    n = polygon_normal(points)
    if dot(n, n) < 1e-9:
        return []
    n = norm(n)
    if len(points) == 3 or convex(points, n):
        return [(points, uvs, n)]
    return [(p, u, n) for p, u in ear_clip(points, uvs, n)]


# ---- cutting at the cells ----

def clip(points, attrs, axis, value, keep_above):
    """Sutherland-Hodgman against the plane `axis` = value."""
    out_p, out_a = [], []
    count = len(points)
    for i in range(count):
        p, q = points[i], points[(i + 1) % count]
        a, b = attrs[i], attrs[(i + 1) % count]
        pin = p[axis] >= value if keep_above else p[axis] <= value
        qin = q[axis] >= value if keep_above else q[axis] <= value
        if pin:
            out_p.append(p)
            out_a.append(a)
        if pin != qin:
            t = (value - p[axis]) / (q[axis] - p[axis])
            out_p.append([p[k] + (q[k] - p[k]) * t for k in range(3)])
            out_a.append(None if a is None else [a[k] + (b[k] - a[k]) * t for k in range(2)])
    return out_p, out_a


def clip_to_square(points, attrs, x0, x1, z0, z1):
    for axis, value, above in ((0, x0, True), (0, x1, False), (2, z0, True), (2, z1, False)):
        points, attrs = clip(points, attrs, axis, value, above)
        if len(points) < 3:
            return [], []
    return points, attrs


def cells_crossed(points, margin):
    xs = [p[0] for p in points]
    zs = [p[2] for p in points]
    cx0 = max(0, int(math.floor((min(xs) - margin + HALF) / CELL)))
    cx1 = min(CELLS - 1, int(math.floor((max(xs) + margin + HALF) / CELL)))
    cz0 = max(0, int(math.floor((min(zs) - margin + HALF) / CELL)))
    cz1 = min(CELLS - 1, int(math.floor((max(zs) + margin + HALF) / CELL)))
    for cz in range(cz0, cz1 + 1):
        for cx in range(cx0, cx1 + 1):
            yield cx, cz


def area(points, n):
    total = [0.0, 0.0, 0.0]
    for i in range(1, len(points) - 1):
        total = add(total, cross(sub(points[i], points[0]), sub(points[i + 1], points[0])))
    return dot(total, n) / 2


# ---- the collision grid ----

def grow_outline(outline, by):
    """A convex outline (u, v points, the inside on the side where the
    game's test is not positive) moved `by` out along each edge."""
    count = len(outline)
    lines = []
    for i in range(count):
        a, b = outline[i], outline[(i + 1) % count]
        du, dv = b[0] - a[0], b[1] - a[1]
        length = math.hypot(du, dv)
        if length < 1e-9:
            continue
        # outward: the side where (p - a) x (b - a) > 0
        ou, ov = dv / length, -du / length
        lines.append(((a[0] + ou * by, a[1] + ov * by), (du, dv)))
    out = []
    for i in range(len(lines)):
        (p1, d1), (p2, d2) = lines[i - 1], lines[i]
        den = d1[0] * d2[1] - d1[1] * d2[0]
        if abs(den) < 1e-9:
            out.append(p2)
            continue
        t = ((p2[0] - p1[0]) * d2[1] - (p2[1] - p1[1]) * d2[0]) / den
        out.append((p1[0] + d1[0] * t, p1[1] + d1[1] * t))
    return out


def collision_face(points, n, surface, cx, cz):
    """One face of one cell (collision.c): the plane, the frame, the
    outline, all from the cell's corner."""
    corner = [cx * CELL - HALF, 0, cz * CELL - HALF]
    local = [sub(p, corner) for p in points]
    centre = [sum(p[k] for p in local) / len(local) for k in range(3)]
    origin = [int(round(c)) for c in centre]
    ni = [int(round(c * ONE_GRID)) for c in n]
    d = -(ni[0] * origin[0] + ni[1] * origin[1] + ni[2] * origin[2])
    # the axes: the longest edge's way, and across it in the plane
    longest = max(range(len(local)), key=lambda i: dot(sub(local[(i + 1) % len(local)], local[i]),
                                                     sub(local[(i + 1) % len(local)], local[i])))
    u = norm(sub(local[(longest + 1) % len(local)], local[longest]))
    u = norm(sub(u, mul(n, dot(u, n))))
    v = norm(cross(n, u))
    ui = [int(round(c * ONE_GRID)) for c in u]
    vi = [int(round(c * ONE_GRID)) for c in v]
    outline = [(dot(sub(p, origin), u), dot(sub(p, origin), v)) for p in local]
    # the game's inside: (p - from) x (to - from) not positive for every edge
    cu = sum(o[0] for o in outline) / len(outline)
    cv = sum(o[1] for o in outline) / len(outline)
    a, b = outline[0], outline[1]
    if (cu - a[0]) * (b[1] - a[1]) - (cv - a[1]) * (b[0] - a[0]) > 0:
        outline.reverse()
    grown = grow_outline(outline, COLLISION_GROW)
    pts = []
    for gu, gv in grown:
        pu, pv = int(round(gu)), int(round(gv))
        if abs(pu) > PROJECTION_MAX or abs(pv) > PROJECTION_MAX:
            raise ArenaError('a collision face reaches %d units from its middle in cell (%d, %d): '
                             'the game projects on a face no farther than %d; cut it smaller'
                             % (max(abs(pu), abs(pv)), cx, cz, PROJECTION_MAX))
        if not pts or pts[-1] != [pu, pv]:
            pts.append([pu, pv])
    if pts[0] == pts[-1]:
        pts.pop()
    if len(pts) < 3:
        return None
    pts.append(list(pts[0]))
    if len(pts) - 1 > 0xFF:
        raise ArenaError('a collision face of more than 255 edges')
    return {'normal': ni, 'd': d, 'origin': origin, 'u': ui, 'v': vi,
            'surface': surface, 'outline': pts}


def surface_of(material):
    key = material.lower().replace('-', '_').split('.')[0].split('_')[0]
    if key not in SURFACES:
        raise ArenaError('the collision material %r is not a surface: %s'
                         % (material, ', '.join(ground_cast.SURFACE_NAMES)))
    return SURFACES[key]


# ---- the meshes ----

def tile_record(quad, page, slot_clut, uvs, corners, normal, cut):
    tpage = (page & 0xF) | 0x10 | TPAGE_BLEND
    clut = (slot_clut & 0x3F) | (CLUT_ROW << 6)
    if quad:
        rec = struct.pack('<HBBH', tpage, QUAD_BITS if cut else 0, CODE_QUAD, clut)
        rec += struct.pack('<4H', *uvs)
        rec += bytes(corners)
        rec += struct.pack('<hhh', normal[2], normal[0], normal[1])
    else:
        rec = struct.pack('<HBBH', tpage, 0, CODE_TRI, clut)
        rec += struct.pack('<3H', *uvs)
        rec += bytes(corners) + b'\x00'
        rec += struct.pack('<3h', *normal) + b'\x00\x00'
    assert len(rec) == stage_ground.TILE_RECORD_BYTES
    return rec


# ---- the whole ----

def read_source(folder):
    path = os.path.join(folder, 'arena.txt')
    settings = {'texture': {}, 'lines': []}
    with open(path, encoding='utf-8') as f:
        for number, line in enumerate(f, 1):
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            if '=' not in line:
                raise ArenaError('arena.txt line %d: no =' % number)
            name, value = (s.strip() for s in line.split('=', 1))
            key = name.lower().replace('-', '_')
            if key.startswith('texture '):
                settings['texture'][name.split(None, 1)[1].strip()] = value
            elif key in ('scene', 'scale'):
                settings[key] = value
            elif key in TABLE_NAMES:
                settings['lines'].append((key, value))
            else:
                raise ArenaError('arena.txt line %d: %s is not a name of an arena (%s)'
                                 % (number, name, ', '.join(TABLE_NAMES + ('scene', 'scale', 'texture NAME'))))
    if 'scene' not in settings:
        raise ArenaError('arena.txt names no scene')
    return settings


def compile_arena(folder, report=print):
    settings = read_source(folder)
    scale = float(settings.get('scale', 1000))
    scene_path = os.path.join(folder, settings['scene'])
    if scene_path.lower().endswith('.obj'):
        scene = read_obj(scene_path, scale)
    else:
        scene = read_gltf(scene_path, scale)
    for material, png in settings['texture'].items():
        with open(os.path.join(folder, png), 'rb') as f:
            scene.textures[material] = f.read()

    def kind_of(obj):
        o = obj.lower()
        if o.startswith('collision'):
            return 'collision'
        if o.startswith('obstacle'):
            return 'obstacle'
        if o.startswith('start') and len(o) >= 7 and o[5] in '1234' and o[6] in 'ab':
            return 'start'
        return 'render'

    polygons = merge_coplanar_triangles(scene.polygons)
    render = [p for p in polygons if kind_of(p[0]) == 'render']
    collision = [p for p in polygons if kind_of(p[0]) == 'collision']
    if not collision:
        raise ArenaError('the scene has no collision object')

    # ---- textures ----
    materials = []
    for _, mat, _, _ in render:
        if mat not in materials:
            materials.append(mat)
    if len(materials) > TEXTURES_MAX:
        raise ArenaError('%d materials drawn; the stage has room for %d textures'
                         % (len(materials), TEXTURES_MAX))
    tims = []
    records = []
    slots = {}
    for i, mat in enumerate(materials):
        if mat not in scene.textures:
            raise ArenaError('the material %r has no picture (a texture in the scene, '
                             'or `texture %s = FILE.png` in arena.txt)' % (mat, mat))
        w, h, pixels = read_png(scene.textures[mat])
        page = TEXTURE_PAGES[i // len(TEXTURE_SLOTS)]
        su, sv = TEXTURE_SLOTS[i % len(TEXTURE_SLOTS)]
        full = resize(w, h, pixels, TEXTURE_SIZE)
        palette = median_cut(full, COLORS)
        clut = [color15(c) for c in palette]
        slots[mat] = (page, i, su, sv)
        for level in range(3):
            size = TEXTURE_SIZE >> level
            picture = full if level == 0 else resize(TEXTURE_SIZE, TEXTURE_SIZE, full, size)
            indices = [nearest(palette, p) for p in picture]
            u, v = su >> level, sv >> level
            image_xy = ((page * 64) + u // 4, 256 + v)
            tims.append(make_tim(indices, size, clut, image_xy, (16 * i, CLUT_ROW)))
            records.append({'tim': len(tims) - 1, 'image': list(image_xy), 'clut': [16 * i, CLUT_ROW]})

    # ---- the tiles ----
    by_cell = {}
    for obj, mat, pts, uvs in render:
        for piece, piece_uv, n in convex_pieces(pts, uvs):
            # each face's texture points moved to the first repeat
            if piece_uv[0] is not None:
                mu = math.floor(min(t[0] for t in piece_uv) + 1e-6)
                mv = math.floor(min(t[1] for t in piece_uv) + 1e-6)
                piece_uv = [[t[0] - mu, t[1] - mv] for t in piece_uv]
                if max(max(t[0], t[1]) for t in piece_uv) > 1.0001:
                    report('arena_compile: a face of %s (%s) spans more than its texture once; '
                           'squeezed into one' % (obj, mat))
                    top = max(max(t[0], t[1]) for t in piece_uv)
                    piece_uv = [[t[0] / top, t[1] / top] for t in piece_uv]
            else:
                piece_uv = [[0.0, 0.0]] * len(piece)
            for cx, cz in cells_crossed(piece, TILE_OVERLAP):
                x0 = cx * CELL - HALF
                z0 = cz * CELL - HALF
                cp, ca = clip_to_square(piece, piece_uv, x0 - TILE_OVERLAP, x0 + CELL + TILE_OVERLAP,
                                        z0 - TILE_OVERLAP, z0 + CELL + TILE_OVERLAP)
                if len(cp) < 3 or area(cp, n) < 1.0:
                    continue
                by_cell.setdefault(cz * CELLS + cx, []).append((mat, cp, ca, n))

    meshes = []
    tiles = []
    runs = []
    for cell in sorted(by_cell):
        cx, cz = cell % CELLS, cell // CELLS
        corner = [cx * CELL - HALF, 0, cz * CELL - HALF]
        current = None
        cell_tiles = []

        def new_mesh():
            return {'vertices': [], 'index': {}, 'tris': [], 'quads': []}

        current = new_mesh()
        mesh_list = [current]
        for mat, cp, ca, n in by_cell[cell]:
            page, slot_index, su, sv = slots[mat]
            local = [[int(round(c)) for c in sub(p, corner)] for p in cp]
            for p in local:
                if any(abs(c) > 32767 for c in p):
                    raise ArenaError('a face is beyond a halfword of its cell (%d, %d)' % (cx, cz))
            texel = [(min(su + 63, su + int(round(t[0] * 63))), min(sv + 63, sv + int(round(t[1] * 63))))
                     for t in ca]
            normal = [int(round(c * ONE_MESH)) for c in n]
            # a fan of quads and a last triangle
            pieces = []
            k = 1
            while k + 2 < len(local):
                pieces.append([0, k, k + 1, k + 2])
                k += 2
            if k + 1 < len(local):
                pieces.append([0, k, k + 1])
            for piece in pieces:
                if len(current['vertices']) + len(piece) > TILE_CORNERS_MAX:
                    current = new_mesh()
                    mesh_list.append(current)
                corners = []
                for j in piece:
                    key = tuple(local[j])
                    if key not in current['index']:
                        current['index'][key] = len(current['vertices'])
                        current['vertices'].append(list(key))
                    corners.append(current['index'][key])
                uv = [texel[j][0] | (texel[j][1] << 8) for j in piece]
                # cut near the camera only if it is large: every cut is
                # four primitives where there was one, and the frame's
                # room for them is the game's
                cut = max(math.dist(local[piece[j]], local[piece[j - 1]])
                          for j in range(len(piece))) > CUT_LONGER_THAN
                if len(piece) == 4:
                    # the record's order: the polygon's 0, 1, 3, 2
                    order = [0, 1, 3, 2]
                    current['quads'].append(tile_record(True, page, slot_index, [uv[o] for o in order],
                                                        [corners[o] for o in order], normal, cut))
                else:
                    current['tris'].append(tile_record(False, page, slot_index, uv, corners, normal, cut))
        for m in mesh_list:
            if not m['vertices']:
                continue
            groups = []
            if m['tris']:
                groups.append({'type': TYPE_TRIS, 'records': m['tris']})
            if m['quads']:
                groups.append({'type': TYPE_QUADS, 'records': m['quads']})
            meshes.append({'vertices': m['vertices'], 'groups': groups})
            cell_tiles.append(len(tiles))
            tiles.append({'cell': cell, 'mesh': len(meshes) - 1, 'can_open': False})
        runs.append({'cell': cell, 'draws': cell_tiles})
    if len(tiles) > TILES_MAX:
        raise ArenaError('%d tiles; the game has room for %d' % (len(tiles), TILES_MAX))
    if sum(len(r['draws']) for r in runs) > RUN_ENTRIES_MAX:
        raise ArenaError('the cells\' lists have more than %d entries' % RUN_ENTRIES_MAX)

    # ---- the collision grid ----
    grid = {}
    for obj, mat, pts, uvs in collision:
        surface = surface_of(mat)
        for piece, _, n in convex_pieces(pts, [None] * len(pts)):
            for cx, cz in cells_crossed(piece, COLLISION_MARGIN):
                x0 = cx * CELL - HALF
                z0 = cz * CELL - HALF
                cp, _ = clip_to_square(piece, [None] * len(piece), x0 - COLLISION_MARGIN,
                                       x0 + CELL + COLLISION_MARGIN, z0 - COLLISION_MARGIN,
                                       z0 + CELL + COLLISION_MARGIN)
                if len(cp) < 3 or area(cp, n) < 1.0:
                    continue
                face = collision_face(cp, n, surface, cx, cz)
                if face:
                    grid.setdefault(cz * CELLS + cx, []).append(face)

    # ---- obstacles and starts ----
    obstacles = []
    starts = {}
    for name, pts in sorted(scene.points.items()):
        if not pts:
            continue
        kind = kind_of(name)
        middle = [sum(p[k] for p in pts) / len(pts) for k in range(3)]
        if kind == 'obstacle':
            top = min(p[1] for p in pts)
            obstacles.append({'type': 1, 'used': 0, 'at': [int(round(middle[0])), int(round(top)),
                                                           int(round(middle[2]))]})
        elif kind == 'start':
            starts[name.lower()[:7]] = middle

    ground = {'sections': [
        {'kind': 'meshes', 'meshes': meshes},
        {'kind': 'collision', 'cells': grid},
        {'kind': 'unused', 'records': []},
        {'kind': 'obstacles', 'records': obstacles, 'end': 0},
        {'kind': 'cells', 'tiles': tiles, 'pieces': [], 'runs': runs},
        {'kind': 'textures', 'groups': [records] + [[] for _ in range(stage_ground.TEXTURE_GROUPS - 1)],
         'tims': tims},
    ], 'tail': ''}
    data = stage_ground.write(ground, sector=2048, align=True)

    # the starts stood on the floor under them
    cast = ground_cast.Grid(ground['sections'][1])
    lines = list(settings['lines'])
    given = {k for k, _ in lines}
    for pair in '1234':
        a, b = starts.get('start%sa' % pair), starts.get('start%sb' % pair)
        if ('start' + pair) in given or a is None or b is None:
            continue
        stood = []
        for p in (a, b):
            x, z = int(round(p[0])), int(round(p[2]))
            floor = cast.floor_under(x, z, int(round(p[1])) - 500, 4000)
            y = floor[0] if floor else int(round(p[1]))
            stood += [x, y, z]
        lines.append(('start' + pair, ' '.join(str(c) for c in stood)))
    text = 'RBARENA 1\n# made by tools/arena_compile.py from %s\n' % os.path.basename(os.path.abspath(folder))
    for key, value in lines:
        text += '%s = %s\n' % (key, value)
    text += 'ground = %d\n' % len(data)
    faces = sum(len(f) for f in grid.values())
    report('arena_compile: %d tiles in %d cells, %d records; %d collision faces in %d cells; '
           '%d textures; %d obstacles; ground %d bytes'
           % (len(tiles), len(runs), sum(len(g['records']) for m in meshes for g in m['groups']),
              faces, len(grid), len(materials), len(obstacles), len(data)))
    return text.encode('utf-8') + data


def read_arena(data):
    """(the table lines as {name: [values]}, the ground's bytes) of an
    .arena file."""
    at = 0
    lines = {}
    first = True
    while True:
        end = data.index(b'\n', at)
        line = data[at:end].decode('utf-8')
        at = end + 1
        if first:
            if not line.startswith('RBARENA'):
                raise ArenaError('not an arena file')
            first = False
            continue
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        name, value = (s.strip() for s in line.split('=', 1))
        if name == 'ground':
            return lines, data[at:at + int(value)]
        lines.setdefault(name, []).append(value)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    try:
        data = compile_arena(argv[1])
    except ArenaError as e:
        print('arena_compile: %s' % e, file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(os.path.abspath(argv[2])), exist_ok=True)
    with open(argv[2], 'wb') as f:
        f.write(data)
    print('arena_compile: wrote %s (%d bytes)' % (argv[2], len(data)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
