#!/usr/bin/env python3
"""Makes a fighter's glTF that Blender (or another tool) wrote into one the
port reads (src/port/gl/gl_fighter_file.h; docs/PORT.md, "The fighters'
own models").

    python tools/fighter_conform.py IN.glb OUT.glb --original M100.glb

The port's rule is simple and strict: vertices in the game's own units and
axes at the bind pose, the 21 bones as joints named bone00 to bone20, an
inverse bind matrix for each, one mesh of triangles. A tool that has
opened and saved the file may have put the vertices in its own space,
renamed nothing but reordered the joints, moved the scale onto the
armature, or turned the bones: so each vertex is taken where glTF itself
would draw it at the bind pose (its joints' global matrices times their
inverse bind matrices, weighted), brought back from glTF's y up and metres
into the game's space, and written with the bind pose of `--original`
(the file tools/fighter_export.py wrote, whose `extras` say it). The
picture of the first material is kept as it is.

Pure standard library.
"""
import argparse
import json
import math
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import fighter_export as fe  # noqa: E402

COMPONENTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
FORMATS = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}


def read_glb(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic, version, _ = struct.unpack_from('<III', data, 0)
    if magic != 0x46546C67 or version != 2:
        raise SystemExit('fighter_conform: %s is not a glTF binary file' % path)
    length, _ = struct.unpack_from('<II', data, 12)
    gltf = json.loads(data[20:20 + length])
    at = 20 + ((length + 3) & ~3)
    blob = b''
    if at + 8 <= len(data):
        blob_length, _ = struct.unpack_from('<II', data, at)
        blob = data[at + 8:at + 8 + blob_length]
    return gltf, blob


def accessor(gltf, blob, index):
    a = gltf['accessors'][index]
    view = gltf['bufferViews'][a['bufferView']]
    n = COMPONENTS[a['type']]
    fmt = FORMATS[a['componentType']]
    size = struct.calcsize(fmt)
    stride = view.get('byteStride', size * n)
    start = view.get('byteOffset', 0) + a.get('byteOffset', 0)
    out = []
    for i in range(a['count']):
        v = struct.unpack_from('<%d%s' % (n, fmt), blob, start + i * stride)
        if a.get('normalized'):
            top = {'B': 255.0, 'H': 65535.0, 'b': 127.0, 'h': 32767.0}[fmt]
            v = tuple(x / top for x in v)
        out.append(v)
    return out


def mat_mul4(a, b):
    """Two 4x4 matrices of lists of rows."""
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def rows_of(columns):
    """A glTF matrix (sixteen numbers, a column after a column) as rows."""
    return [[columns[c * 4 + r] for c in range(4)] for r in range(4)]


def local_of(node):
    if 'matrix' in node:
        return rows_of(node['matrix'])
    t = node.get('translation', [0, 0, 0])
    x, y, z, w = node.get('rotation', [0, 0, 0, 1])
    s = node.get('scale', [1, 1, 1])
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def globals_of(gltf):
    nodes = gltf['nodes']
    parent = {}
    for i, node in enumerate(nodes):
        for c in node.get('children', []):
            parent[c] = i
    out = {}

    def get(i):
        if i not in out:
            m = local_of(nodes[i])
            out[i] = mat_mul4(get(parent[i]), m) if i in parent else m
        return out[i]

    for i in range(len(nodes)):
        get(i)
    return out


def apply(m, v, w):
    return [m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] + m[i][3] * w for i in range(3)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('src')
    ap.add_argument('out')
    ap.add_argument('--original', required=True, help='the file fighter_export.py wrote')
    args = ap.parse_args()
    original, _ = read_glb(args.original)
    extras = original['extras']
    bind_rot = [[r[i * 3:i * 3 + 3] for i in range(3)] for r in extras['bind_rotation']]
    bind_pos = extras['bind_translation']
    bones, _ = fe.source_tables()

    gltf, blob = read_glb(args.src)
    g = globals_of(gltf)
    skin = gltf['skins'][0]
    joint_bone = []
    for j in skin['joints']:
        name = gltf['nodes'][j].get('name', '')
        joint_bone.append(int(name[4:]) if name.startswith('bone') and name[4:].isdigit() else None)
    if sorted(b for b in joint_bone if b is not None) != list(range(fe.BONES)):
        raise SystemExit('fighter_conform: the skin\'s joints are not bone00 to bone20: %s'
                         % [gltf['nodes'][j].get('name') for j in skin['joints']])
    ibm = [rows_of(m) for m in accessor(gltf, blob, skin['inverseBindMatrices'])]
    joint_matrix = [mat_mul4(g[j], ibm[k]) for k, j in enumerate(skin['joints'])]

    mesh_node = next(i for i, n in enumerate(gltf['nodes']) if 'mesh' in n and 'skin' in n)
    mesh = gltf['meshes'][gltf['nodes'][mesh_node]['mesh']]
    vertices = []
    triangles = []
    image = None
    for prim in mesh['primitives']:
        attrs = prim['attributes']
        pos = accessor(gltf, blob, attrs['POSITION'])
        nrm = accessor(gltf, blob, attrs['NORMAL']) if 'NORMAL' in attrs else [(0, 0, 0)] * len(pos)
        uvs = accessor(gltf, blob, attrs['TEXCOORD_0'])
        jnt = accessor(gltf, blob, attrs['JOINTS_0'])
        wgt = accessor(gltf, blob, attrs['WEIGHTS_0'])
        base = len(vertices)
        for p, n, uv, js, ws in zip(pos, nrm, uvs, jnt, wgt):
            q = [0.0, 0.0, 0.0]
            m = [0.0, 0.0, 0.0]
            pairs = []
            for j, w in zip(js, ws):
                if w <= 0:
                    continue
                a = apply(joint_matrix[j], p, 1.0)
                b = apply(joint_matrix[j], n, 0.0)
                for k in range(3):
                    q[k] += w * a[k]
                    m[k] += w * b[k]
                pairs.append((joint_bone[j], w))
            # glTF's y up and metres into the game's y down and units: the
            # inverse of the exporter's node above the skeleton
            game = (q[0] * 1000.0, -q[1] * 1000.0, -q[2] * 1000.0)
            normal = (m[0], -m[1], -m[2])
            length = math.sqrt(sum(c * c for c in normal)) or 1.0
            vertices.append((game, tuple(c / length for c in normal), tuple(uv), pairs))
        idx = accessor(gltf, blob, prim['indices'])
        flat = [i[0] for i in idx]
        for t in range(0, len(flat), 3):
            triangles.append((base + flat[t], base + flat[t + 1], base + flat[t + 2]))
        if image is None and 'material' in prim:
            mat = gltf['materials'][prim['material']]
            tex = mat.get('pbrMetallicRoughness', {}).get('baseColorTexture')
            if tex is not None:
                img = gltf['images'][gltf['textures'][tex['index']]['source']]
                view = gltf['bufferViews'][img['bufferView']]
                image = blob[view.get('byteOffset', 0):view.get('byteOffset', 0) + view['byteLength']]
    if image is None:
        raise SystemExit('fighter_conform: no picture in %s' % args.src)
    extras = dict(extras)
    extras['conformed_from'] = os.path.basename(args.src)
    fe.write_glb(args.out, extras['model_file'], bones, bind_rot, bind_pos, vertices, triangles, image, extras)
    print('fighter_conform: %d vertices, %d triangles -> %s' % (len(vertices), len(triangles), args.out))


if __name__ == '__main__':
    main()
