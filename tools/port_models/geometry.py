#!/usr/bin/env python3
"""A fighter's geometry-only pass (the owner, 2026-11-19): the game's own
model with nothing changed but what the new renderer shows wrong in it.
Its texture, its texels, its colour tables, its NEAREST filtering, its
skeleton, its cloths and its normals stay the game's; no upscaling and no
maps for the lighting (tools/port_models/README.md, "The geometry-only
pass").

    python tools/port_models/geometry.py REF.glb OUT.glb [--passes 2]

What it changes, and only there:

- **the layers of one rigid piece that cross** (`separate_layers`): a
  lock of hair laid over the hair's cap with an edge inside it is drawn
  by a depth buffer in stripes the PlayStation never showed; the lock is
  put in front of the cap by a unit (a finger lying across the fist at
  a shallow angle the same), no vertex more than four units in all.
- **the joints**: the game joins two bones' rigid pieces by a band of
  triangles stretched between them (a shoulder's ring to the upper arm's,
  the chest's to the neck's), and a deep bend folds the band over
  itself: its faces turn their backs to the camera, which the renderer
  leaves out, and the crease opens a crack or passes through the piece
  beside it. Each edge across such a band is cut, `--passes` times in
  two, and each new vertex is a mix of how the two ends are carried: the
  point carried rigidly the way one end is, and the way the other end
  is, mixed by how far along the edge it lies. At the bind pose every
  new vertex lies on the game's own face, exactly; in a bend the band
  curves from one piece into the other instead of folding. The game's
  own vertices are not moved.

Every vertex keeps the game's own sum of bones (the file's terms: each
bone's world matrix times a vector and a share of its place), so the
port draws the band from the same bones the game posed.
"""
import argparse
import copy
import os
import sys

# numpy's BLAS on one kernel, whatever the PC: OpenBLAS picks one by the
# processor, and the kernels round alike only mostly, so the pages made on
# two PCs differed in a few texels (docs/MODS.md, "The same bytes on every
# PC"). Nehalem's runs on every processor numpy runs on.
os.environ['OPENBLAS_CORETYPE'] = 'Nehalem'

import numpy as np  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fighter_glb as fg  # noqa: E402
from meshes import K, Mesh  # noqa: E402

EPS = 1e-6


def signature(terms, tjoints):
    """How a vertex is carried, as a share a bone (21,): its bones that
    turn a vector, alike."""
    sig = np.zeros(fg.BONES)
    vec = np.abs(terms[:, :3]).sum(1) > EPS
    bones = tjoints[vec] if vec.any() else tjoints[np.abs(terms[:, 3]) > EPS]
    for b in bones:
        sig[b] = 1.0
    return sig / max(sig.sum(), 1.0)


def carry_bone(terms, tjoints):
    """The bone that turns a vertex's own offset: the one whose term is a
    vector with no share of the bone's place (the game's joint vertex: a
    place on the parent, an offset turned by the child), else the one
    with the largest share of its place."""
    vec = np.linalg.norm(terms[:, :3], axis=1)
    free = (np.abs(terms[:, 3]) < EPS) & (vec > EPS)
    if free.any():
        k = int(np.argmax(np.where(free, vec, -1)))
    else:
        k = int(np.argmax(terms[:, 3]))
    return int(tjoints[k])


class Carrier:
    """A vertex as a mix of the game's own vertices, each carried rigidly
    to another point the way it is carried itself: its terms at a point
    are the mix of each one's terms with the offset from it to that
    point turned by its carrying bone."""

    def __init__(self, m):
        self.m = m
        self.R0, _ = m.bind()
        self.carry = np.array([carry_bone(m.terms[v], m.tjoints[v]) for v in range(len(m.pos))])

    def terms_at(self, mix, p):
        """`mix` {game vertex: share}; the terms (dict bone -> 4-vector)
        of point `p`."""
        out = {}
        for v, share in mix.items():
            m = self.m
            for k in range(m.terms.shape[1]):
                t = m.terms[v, k]
                if np.abs(t).sum() < EPS:
                    continue
                b = int(m.tjoints[v, k])
                out[b] = out.get(b, np.zeros(4)) + share * t
            b = int(self.carry[v])
            local = self.R0[b].T @ (p - m.pos[v])
            out[b] = out.get(b, np.zeros(4)) + share * np.concatenate([local, [0.0]])
        return out


def split_bands(m, passes=2, threshold=0.6, joints=None):
    """The band edges cut `passes` times in two (module doc). Returns a
    Mesh and, for each of its vertices, the mix of game vertices it is."""
    carrier = Carrier(m)
    n0 = len(m.pos)
    pos = [p for p in m.pos]
    nrm = [p for p in m.nrm]
    uv = [p for p in m.uv]
    mix = [{v: 1.0} for v in range(n0)]
    sig = [signature(m.terms[v], m.tjoints[v]) for v in range(n0)]
    tris = [tuple(t) for t in m.tris]
    for _ in range(passes):
        made = {}

        def middle(a, b):
            key = (min(a, b), max(a, b))
            if key in made:
                return made[key]
            p = (pos[a] + pos[b]) / 2
            nn = nrm[a] + nrm[b]
            ln = np.linalg.norm(nn)
            mx = {}
            for src, share in list(mix[a].items()) + list(mix[b].items()):
                mx[src] = mx.get(src, 0.0) + share / 2
            pos.append(p)
            nrm.append(nn / ln if ln > EPS else nn)
            uv.append((uv[a] + uv[b]) / 2)
            mix.append(mx)
            sig.append((sig[a] + sig[b]) / 2)
            made[key] = len(pos) - 1
            return made[key]

        def cut(a, b):
            if np.abs(sig[a] - sig[b]).sum() <= threshold or np.linalg.norm(pos[a] - pos[b]) <= 1.0:
                return False
            if joints is None:
                return True
            bones = set(np.nonzero(sig[a] + sig[b])[0].tolist())
            return any(set(pair) <= bones for pair in joints)

        out = []
        for (a, b, c) in tris:
            e = [cut(a, b), cut(b, c), cut(c, a)]
            k = sum(e)
            if k == 0:
                out.append((a, b, c))
                continue
            # rotate so that the cut edges come first
            while not e[0] or (k == 2 and not e[1]):
                a, b, c = b, c, a
                e = e[1:] + e[:1]
            if k == 1:
                ab = middle(a, b)
                out += [(a, ab, c), (ab, b, c)]
            elif k == 2:
                ab, bc = middle(a, b), middle(b, c)
                # the quad a-ab-bc-c split along its shorter diagonal
                out.append((ab, b, bc))
                if np.linalg.norm(pos[ab] - pos[c]) < np.linalg.norm(pos[a] - pos[bc]):
                    out += [(a, ab, c), (ab, bc, c)]
                else:
                    out += [(a, ab, bc), (a, bc, c)]
            else:
                ab, bc, ca = middle(a, b), middle(b, c), middle(c, a)
                out += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
        tris = out
    # the new vertices' terms, joints and weights
    n = len(pos)
    terms = np.zeros((n, K, 4))
    tj = np.zeros((n, K), np.int64)
    joints = np.zeros((n, 4), np.int64)
    weights = np.zeros((n, 4))
    width = m.terms.shape[1]
    terms[:n0, :width] = m.terms
    tj[:n0, :width] = m.tjoints
    joints[:n0] = m.joints
    weights[:n0] = m.weights
    worst = 0
    for v in range(n0, n):
        t = carrier.terms_at(mix[v], pos[v])
        if len(t) > K:
            raise ValueError('vertex %d has %d bones' % (v, len(t)))
        for k, (b, vec) in enumerate(sorted(t.items())):
            terms[v, k] = vec
            tj[v, k] = b
        worst = max(worst, len(t))
        # the linear blend a viewer without the terms draws: the mix of
        # the ends' own
        w = np.zeros(fg.BONES)
        for src, share in mix[v].items():
            np.add.at(w, m.joints[src], share * m.weights[src])
        top = np.argsort(w)[::-1][:4]
        joints[v] = top
        weights[v] = w[top] / max(w[top].sum(), EPS)
    mesh = Mesh(pos=np.array(pos), nrm=np.array(nrm), uv=np.array(uv), joints=joints, weights=weights,
                terms=terms, tjoints=tj, tris=np.array(tris, np.int64))
    return mesh, mix


def rigid_bone(m):
    """Each vertex's bone where it is carried by one bone alone, else -1."""
    used = np.abs(m.terms).sum(2) > EPS
    one = used.sum(1) == 1
    k = np.argmax(used, 1)
    return np.where(one, m.tjoints[np.arange(len(m.pos)), k], -1)


def crossings(m, P):
    """The pairs of triangles that pass through one another sharing no
    corner (check.py's test), whatever surface they are of."""
    import check as ck
    key = np.round(m.pos * 4).astype(np.int64)
    _, weld = np.unique(key, axis=0, return_inverse=True)
    weld = weld.ravel()
    tris = m.tris
    A, B, C = P[tris[:, 0]], P[tris[:, 1]], P[tris[:, 2]]
    lo = np.minimum(np.minimum(A, B), C)
    hi = np.maximum(np.maximum(A, B), C)
    out = []
    for i in range(len(tris)):
        j = np.arange(i + 1, len(tris))
        j = j[np.all((lo[i] <= hi[j]) & (lo[j] <= hi[i]), 1)]
        if not len(j):
            continue
        j = j[~(weld[tris[i]][None, :, None] == weld[tris[j]][:, None, :]).any((1, 2))]
        if not len(j):
            continue
        Ti = np.repeat(tris[i:i + 1], len(j), 0)
        Tj = tris[j]
        hit = np.zeros(len(j), bool)
        for (a, b) in ((0, 1), (1, 2), (2, 0)):
            hit |= ck.segment_hits(P[Ti[:, a]], P[Ti[:, b]], P[Tj[:, 0]], P[Tj[:, 1]], P[Tj[:, 2]])
            hit |= ck.segment_hits(P[Tj[:, a]], P[Tj[:, b]], P[Ti[:, 0]], P[Ti[:, 1]], P[Ti[:, 2]])
        out += [(i, int(x)) for x in j[hit]]
    return out


def separate_layers(m, clearance=1.0, rounds=8, shallow=0.9, most=4.0):
    """The layers of one rigid piece that pass through one another (a lock
    of hair laid over the hair's cap with an edge a unit or two inside
    it): the PlayStation drew one of the two whole, by its order, and a
    depth buffer draws them interleaved in stripes that crawl as he
    moves. The one that crosses more of the other's faces (the lock) is
    the layer laid over: each of its corners behind one of those faces,
    or within `clearance` of it, is put `clearance` in front of it along
    its normal, it and every vertex of the game's at its place, if that
    moves it no more than `most` in all. Only a pair carried by the same one bone and
    crossing at a shallow angle (normals within 25 degrees) is touched, so
    it holds in every pose and no piece the game made to pass through
    another (a finger through the fist) is moved. Changes m.pos and m.terms; returns the corners moved."""
    R0, T0 = m.bind()
    bone = rigid_bone(m)
    moved = {}
    for _ in range(rounds):
        P = m.pose(R0, T0)
        A, B, C = (P[m.tris[:, k]] for k in range(3))
        fn = np.cross(B - A, C - A)
        fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), EPS)
        # pieces that cross steeply (a finger through the fist, a lock
        # through the cap) are drawn as their union, which is the game's
        # shape; only faces crossing at a shallow angle interleave
        pairs = [(i, j) for i, j in crossings(m, P)
                 if len(set(bone[m.tris[i]]) | set(bone[m.tris[j]])) == 1 and bone[m.tris[i, 0]] >= 0
                 and fn[i] @ fn[j] > shallow]
        if not pairs:
            break
        count = {}
        for i, j in pairs:
            count[i] = count.get(i, 0) + 1
            count[j] = count.get(j, 0) + 1
        for i, j in pairs:
            over, base = (i, j) if count[i] >= count[j] else (j, i)
            a, b, c = P[m.tris[base]]
            n = np.cross(b - a, c - a)
            n /= np.linalg.norm(n)
            for v in m.tris[over]:
                d = (P[v] - a) @ n
                if d >= clearance or moved.get(int(v), 0.0) + clearance - d > most:
                    continue
                delta = (clearance - d) * n
                same = np.nonzero(np.abs(m.pos - m.pos[v]).max(1) < 0.25)[0]
                for u in same:
                    if bone[u] != bone[v]:
                        continue
                    k = int(np.argmax(np.abs(m.terms[u]).sum(1) > EPS))
                    m.terms[u, k, :3] += R0[bone[u]].T @ delta
                    m.pos[u] = m.pos[u] + delta
                    moved[int(u)] = moved.get(int(u), 0.0) + float(np.linalg.norm(delta))
    return moved


def posed(mesh, R, T):
    Rj = R[mesh.tjoints]
    v = np.einsum('nkij,nkj->nki', Rj, mesh.terms[:, :, :3])
    v += mesh.terms[:, :, 3:4] * T[mesh.tjoints]
    return v.sum(1)


def write(path, mesh, template):
    """`mesh` in the place of the template's (the game's own file): its
    skin, material, images, NEAREST sampler and extras (the colour tables'
    picture, the cloths) as they are."""
    g = template.gltf
    w = fg.GlbWriter()
    width = 8 if np.abs(mesh.terms[:, 4:]).sum() > 0 else 4
    attributes = {
        'POSITION': w.array(mesh.pos, 5126, 'VEC3', 34962, minmax=True),
        'NORMAL': w.array(mesh.nrm, 5126, 'VEC3', 34962),
        'TEXCOORD_0': w.array(mesh.uv, 5126, 'VEC2', 34962),
        'JOINTS_0': w.array(mesh.joints, 5121, 'VEC4', 34962),
        'WEIGHTS_0': w.array(mesh.weights, 5126, 'VEC4', 34962),
        '_RB_TJOINTS': w.array(mesh.tjoints[:, :4], 5121, 'VEC4', 34962),
    }
    if width == 8:
        attributes['_RB_TJOINTS1'] = w.array(mesh.tjoints[:, 4:], 5121, 'VEC4', 34962)
    for k in range(width):
        attributes['_RB_TERM%d' % k] = w.array(mesh.terms[:, k], 5126, 'VEC4', 34962)
    indices = w.array(mesh.tris.reshape(-1, 1), 5125, 'SCALAR', 34963)
    ibm = np.stack([template.ibm[b].T.reshape(16) for b in range(fg.BONES)])
    a_ibm = w.array(ibm, 5126, 'MAT4')
    images = []
    for i, img in enumerate(g.get('images', [])):
        entry = {k: v for k, v in img.items() if k not in ('bufferView', 'uri')}
        entry['bufferView'] = w.view(fg.image_bytes(g, template.blob, i))
        entry.setdefault('mimeType', 'image/png')
        images.append(entry)
    gltf = copy.deepcopy({k: v for k, v in g.items()
                          if k not in ('accessors', 'bufferViews', 'buffers', 'images', 'meshes', 'skins')})
    skin = copy.deepcopy(g['skins'][0])
    skin['inverseBindMatrices'] = a_ibm
    prim = {k: v for k, v in g['meshes'][0]['primitives'][0].items() if k not in ('attributes', 'indices')}
    prim.update(attributes=attributes, indices=indices)
    gltf.update(skins=[skin], images=images,
                meshes=[dict(g['meshes'][0], primitives=[prim])])
    gltf['asset'] = {'version': '2.0', 'generator': 'ReBushido tools/port_models/geometry.py'}
    w.write(path, gltf)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('ref')
    ap.add_argument('out')
    ap.add_argument('--passes', type=int, default=2)
    ap.add_argument('--joints', default=None, help='the joints cut, as bone pairs: 4-5,5-6 (all if not said)')
    args = ap.parse_args()
    m = fg.Model(args.ref)
    joints = [tuple(int(b) for b in j.split('-')) for j in args.joints.split(',')] if args.joints else None
    moved = separate_layers(m)
    if moved:
        print('geometry: layers parted: %d vertices moved, the most %.2f units'
              % (len(moved), max(moved.values())))
    mesh, _ = split_bands(m, args.passes, joints=joints)
    # at the bind pose every vertex is where the game's face has it
    R0, T0 = m.bind()
    miss = np.abs(posed(mesh, R0, T0) - mesh.pos).max()
    if miss > 0.05:
        sys.exit('geometry: a vertex is %.3f units from its place at the bind pose' % miss)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    write(args.out, mesh, m)
    print('geometry: %s: %d triangles (the game\'s %d), %d vertices (%d), bind pose within %.4f units'
          % (args.out, len(mesh.tris), len(m.tris), len(mesh.pos), len(m.pos), miss))


if __name__ == '__main__':
    main()
