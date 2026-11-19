"""A fighter's model file (docs/PORT.md, "The fighters' own models";
src/port/gl/gl_fighter_file.h) read into numpy arrays, posed on the game's
bones as the port poses it, and written back: the shared ground of the
model pipeline (tools/port_models/README.md).

Needs numpy (Windows Python, as tools/fighter_hd.py).
"""
import io
import json
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)

BONES = 21
COMPONENTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
DTYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16,
          5125: np.uint32, 5126: np.float32}


def read_glb(path):
    """The JSON and the binary chunk of a .glb."""
    with open(path, 'rb') as f:
        data = f.read()
    magic, version, _ = struct.unpack_from('<III', data, 0)
    if magic != 0x46546C67 or version != 2:
        raise ValueError('%s is not a glTF binary file' % path)
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
    dtype = np.dtype(DTYPES[a['componentType']])
    stride = view.get('byteStride', dtype.itemsize * n)
    start = view.get('byteOffset', 0) + a.get('byteOffset', 0)
    count = a['count']
    if stride == dtype.itemsize * n:
        out = np.frombuffer(blob, dtype, count * n, start).reshape(count, n)
    else:
        rows = [np.frombuffer(blob, dtype, n, start + i * stride) for i in range(count)]
        out = np.array(rows).reshape(count, n)
    if a.get('normalized'):
        out = out.astype(np.float64) / float(np.iinfo(dtype).max)
    return out


def image_bytes(gltf, blob, index):
    view = gltf['bufferViews'][gltf['images'][index]['bufferView']]
    start = view.get('byteOffset', 0)
    return bytes(blob[start:start + view['byteLength']])


class Model:
    """A model file as arrays, in the game's own units and axes (y down):

    pos, nrm (N, 3); uv (N, 2); joints (N, 4) the game's bone numbers;
    weights (N, 4); terms (N, K, 4) and tjoints (N, K): the vertex as the
    sum of each bone's world matrix times (x, y, z, h), from the file's
    _RB_TERM attributes or worked out of the linear blend; tris (M, 3);
    part (M,) the primitive of each triangle; ibm (21, 4, 4) the inverse
    bind matrices (row-major here); has_joint (21,); the JSON and blob."""

    def __init__(self, path):
        self.path = path
        gltf, blob = read_glb(path)
        self.gltf, self.blob = gltf, blob
        skin = gltf['skins'][0]
        names = [gltf['nodes'][j].get('name', '') for j in skin['joints']]
        bone_of = []
        for name in names:
            bone_of.append(int(name[4:]) if name.startswith('bone') and name[4:].isdigit() else -1)
        ibm_cols = accessor(gltf, blob, skin['inverseBindMatrices']).astype(np.float64)
        self.ibm = np.tile(np.eye(4), (BONES, 1, 1))
        self.has_joint = np.zeros(BONES, bool)
        for j, b in enumerate(bone_of):
            if 0 <= b < BONES:
                self.ibm[b] = ibm_cols[j].reshape(4, 4).T
                self.has_joint[b] = True
        bone_of = np.array(bone_of + [-1] * 256)
        pos, nrm, uv, jnt, wgt, terms, tj, tris, part = [], [], [], [], [], [], [], [], []
        base = 0
        mesh = gltf['meshes'][0]
        for p, prim in enumerate(mesh['primitives']):
            at = prim['attributes']
            P = accessor(gltf, blob, at['POSITION']).astype(np.float64)
            n = len(P)
            pos.append(P)
            nrm.append(accessor(gltf, blob, at['NORMAL']).astype(np.float64)
                       if 'NORMAL' in at else np.zeros((n, 3)))
            uv.append(accessor(gltf, blob, at['TEXCOORD_0']).astype(np.float64)
                      if 'TEXCOORD_0' in at else np.zeros((n, 2)))
            J = bone_of[accessor(gltf, blob, at['JOINTS_0']).astype(np.int64)]
            W = accessor(gltf, blob, at['WEIGHTS_0']).astype(np.float64)
            jnt.append(J)
            wgt.append(W)
            if '_RB_TERM0' in at:
                width = 8 if '_RB_TERM4' in at else 4
                T = np.stack([accessor(gltf, blob, at['_RB_TERM%d' % k]).astype(np.float64)
                              for k in range(width)], 1)
                TJ = bone_of[accessor(gltf, blob, at['_RB_TJOINTS']).astype(np.int64)]
                if width == 8:
                    TJ = np.concatenate([TJ, bone_of[accessor(gltf, blob, at['_RB_TJOINTS1'])
                                                     .astype(np.int64)]], 1)
            else:
                T, TJ = self.terms_of_blend(P, J, W)
            if T.shape[1] < 8:
                T = np.concatenate([T, np.zeros((n, 8 - T.shape[1], 4))], 1)
                TJ = np.concatenate([TJ, np.zeros((n, 8 - TJ.shape[1]), np.int64)], 1)
            terms.append(T)
            tj.append(np.where(TJ < 0, 0, TJ))
            I = accessor(gltf, blob, prim['indices']).reshape(-1, 3).astype(np.int64) + base
            tris.append(I)
            part.append(np.full(len(I), p))
            base += n
        self.pos = np.concatenate(pos)
        self.nrm = np.concatenate(nrm)
        self.uv = np.concatenate(uv)
        self.joints = np.concatenate(jnt)
        self.weights = np.concatenate(wgt)
        self.terms = np.concatenate(terms)
        self.tjoints = np.concatenate(tj)
        self.tris = np.concatenate(tris)
        self.part = np.concatenate(part)
        self.extras = gltf.get('extras', {})

    def terms_of_blend(self, P, J, W):
        """The terms of a linear blend: each joint's weight times its
        inverse bind matrix times the place, and the weight as h."""
        n = len(P)
        T = np.zeros((n, 4, 4))
        ph = np.concatenate([P, np.ones((n, 1))], 1)
        for k in range(4):
            m = self.ibm[np.clip(J[:, k], 0, BONES - 1)]
            local = np.einsum('nij,nj->ni', m, ph)[:, :3]
            T[:, k, :3] = local * W[:, k:k + 1]
            T[:, k, 3] = W[:, k]
        return T, np.clip(J, 0, BONES - 1)

    def bind(self):
        """Each bone's world rotation and place at the file's bind pose."""
        R = np.zeros((BONES, 3, 3))
        T = np.zeros((BONES, 3))
        for b in range(BONES):
            m = np.linalg.inv(self.ibm[b])
            R[b], T[b] = m[:3, :3], m[:3, 3]
        return R, T

    def pose(self, R, T):
        """Every vertex where the port draws it on bones (R, T)."""
        Rj = R[self.tjoints]                       # (N, K, 3, 3)
        Tj = T[self.tjoints]                       # (N, K, 3)
        v = np.einsum('nkij,nkj->nki', Rj, self.terms[:, :, :3])
        v += self.terms[:, :, 3:4] * Tj
        return v.sum(1)

    def images(self):
        return [image_bytes(self.gltf, self.blob, i) for i in range(len(self.gltf.get('images', [])))]

    def base_color_png(self, part=0):
        prim = self.gltf['meshes'][0]['primitives'][part]
        mat = self.gltf['materials'][prim['material']]
        tex = self.gltf['textures'][mat['pbrMetallicRoughness']['baseColorTexture']['index']]
        return image_bytes(self.gltf, self.blob, tex['source'])


def read_motion(path):
    """A motion the port wrote (--gl-fighter-motion): for each frame the 21
    bones' world rotations (1.0 = 4096) and places."""
    frames = []
    with open(path, encoding='ascii') as f:
        for line in f:
            v = line.split()
            if len(v) != 1 + BONES * 12:
                continue
            a = np.array(v[1:], np.float64).reshape(BONES, 12)
            R = a[:, :9].reshape(BONES, 3, 3) / 4096.0
            frames.append((int(v[0]), R, a[:, 9:]))
    return frames


def motion_files(folder, name):
    """The motions of model `name` in `folder` (one a slot and a run)."""
    if not os.path.isdir(folder):
        return []
    out = []
    for run in sorted(os.listdir(folder)):
        sub = os.path.join(folder, run)
        for f in sorted(os.listdir(sub)) if os.path.isdir(sub) else []:
            if f.startswith(name + '.') and f.endswith('.motion.txt'):
                out.append(os.path.join(sub, f))
    return out


# ---- writing ----

class GlbWriter:
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

    def array(self, a, comp, kind, target=None, minmax=False):
        dtype = DTYPES[comp]
        a = np.ascontiguousarray(a, dtype)
        acc = {'bufferView': self.view(a.tobytes(), target), 'componentType': comp,
               'count': int(a.shape[0]), 'type': kind}
        if minmax:
            acc['min'] = [float(x) for x in a.min(0)]
            acc['max'] = [float(x) for x in a.max(0)]
        self.accessors.append(acc)
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


def png_bytes(rgba):
    from PIL import Image
    buf = io.BytesIO()
    Image.fromarray(np.ascontiguousarray(rgba)).save(buf, 'PNG', optimize=False, compress_level=6)
    return buf.getvalue()


def png_array(data):
    from PIL import Image
    return np.array(Image.open(io.BytesIO(data)).convert('RGBA'))
