#!/usr/bin/env python3
"""Rebuilds the fighters and their weapons at a higher definition, from the
player's own disc image, into a mod the native port reads (docs/PORT.md,
"The fighters' own models"; docs/MODS.md).

    python tools/fighter_hd.py --image "Bushido Blade 2 (USA).cue"
            [--out tmp/mods/hd] [--models all|M100,W00,...]
            [--recipe tools/fighter_hd.json] [--sheet FOLDER]

It writes `<out>/models/<name>.glb` for every model the recipe names
(`M000`..`M306`, the weapons `W00`..`W57`) and `<out>/mod.txt`. Nothing of
the game is in the repository: the recipe (tools/fighter_hd.json) is
numbers, and what is written goes into a folder git ignores.

What is done to a model, each step faithful to the game's own:

  - read as the game builds it (tools/fighter_export.py, read_model): every
    vertex as its exact terms, the sum of each bone's world matrix times a
    vector, which the port draws as they are;
  - welded: pieces that meet at one place are made one vertex (`weld`, in
    the game's units), their terms averaged, so that a seam the game draws
    as two pieces is one surface that bends as both; and the points of the
    pieces' open edges within `seam` of each other the same, which closes
    the seams where two pieces nearly meet;
  - creased where faces meet at more than `crease` degrees, and along open
    edges, so that a sleeve's hem, a sash's edge or a blade's edge stays
    sharp;
  - subdivided (Catmull-Clark, `levels` times, the creases kept), the
    terms and the weights with the places: the subdivision is linear in
    them, so the new surface is at every pose exactly the subdivision of
    the game's own posed mesh;
  - fitted (`fit` rounds): the surface is made to pass through the game's
    own vertices, so that the silhouette is the game's and not shrunk
    inside it;
  - its normals made smooth across the surface, and hard across a crease;
  - the texture made `scale` times larger (`upscale`), the colour tables'
    picture (rb_palette) kept as it is.

No hitbox is a model's: the bones, the motions, the body points and the
weapons' points are the game's own and untouched.

Needs numpy and Pillow.
"""
import argparse
import io
import json
import math
import os
import sys
import time
from collections import defaultdict

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import fighter_export as fe  # noqa: E402

BONES = fe.BONES
# a point's numbers: its place, each bone's term (x, y, z, h) and each
# bone's weight of the linear blend
PLACE = slice(0, 3)
TERMS = slice(3, 3 + 4 * BONES)
WEIGHTS = slice(3 + 4 * BONES, 3 + 5 * BONES)
WIDTH = 3 + 5 * BONES

DEFAULTS = {'weld': 0.5, 'seam': 1.5, 'crease': 50.0, 'levels': 2, 'fit': 12, 'scale': 4, 'upscale': 'scale2x'}


# ---- the mesh ----

class Mesh:
    """Points (rows of numbers: PLACE, TERMS, WEIGHTS), faces as lists of
    points round them, each corner's texture point, whether each face is
    lit, and the edges that are sharp."""

    def __init__(self, X, faces, uvs, lit, sharp):
        self.X = X
        self.faces = faces
        self.uvs = uvs
        self.lit = lit
        self.sharp = sharp


def mesh_of(md):
    X = np.zeros((len(md.points), WIDTH))
    for i, (place, terms, weights) in enumerate(md.points):
        X[i, PLACE] = place
        for bone, t in terms.items():
            X[i, 3 + 4 * bone:7 + 4 * bone] = t
        for bone, w in weights:
            X[i, 3 + 4 * BONES + bone] += w
    faces = [list(f['points']) for f in md.faces]
    uvs = [[tuple(uv) for uv in f['uvs']] for f in md.faces]
    lit = [any(any(c != 0 for c in n) for n in f['normals']) for f in md.faces]
    return Mesh(X, faces, uvs, lit, set())


def edge(a, b):
    return (a, b) if a < b else (b, a)


def open_points(mesh):
    """The points on an open edge: where two of the game's pieces meet
    without sharing their points, or a piece ends."""
    out = set()
    for e, fs in edge_faces(mesh).items():
        if len(fs) == 1:
            out.update(e)
    return out


def weld(mesh, dist, only=None):
    """Points within `dist` of each other made one, their numbers averaged
    (with `only`, of those points alone); faces left with fewer than three
    points, and a face that is another's again (the game's two-sided faces
    are drawn so), dropped."""
    n = len(mesh.X)
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    cell = defaultdict(list)
    size = max(dist, 1e-6)
    P = mesh.X[:, PLACE]
    for i in range(n):
        if only is not None and i not in only:
            continue
        key = tuple(np.floor(P[i] / size).astype(int))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for j in cell.get((key[0] + dx, key[1] + dy, key[2] + dz), ()):
                        if np.abs(P[i] - P[j]).max() <= dist:
                            parent[find(i)] = find(j)
        cell[key].append(i)
    roots = sorted({find(i) for i in range(n)})
    index = {r: k for k, r in enumerate(roots)}
    X = np.zeros((len(roots), WIDTH))
    count = np.zeros(len(roots))
    for i in range(n):
        k = index[find(i)]
        X[k] += mesh.X[i]
        count[k] += 1
    X /= count[:, None]
    faces, uvs, lit, seen = [], [], [], set()
    for f, uv, li in zip(mesh.faces, mesh.uvs, mesh.lit):
        pts, cuv = [], []
        for p, t in zip(f, uv):
            q = index[find(p)]
            if pts and pts[-1] == q:
                continue
            pts.append(q)
            cuv.append(t)
        if len(pts) > 1 and pts[0] == pts[-1]:
            pts.pop()
            cuv.pop()
        if len(pts) < 3 or len(set(pts)) < len(pts):
            continue
        key = tuple(sorted(pts))
        if key in seen:
            continue
        seen.add(key)
        faces.append(pts)
        uvs.append(cuv)
        lit.append(li)
    return Mesh(X, faces, uvs, lit, set()), n - len(roots)


def face_normal(P, f):
    """Newell's normal of a face, as long as its area times two."""
    n = np.zeros(3)
    for a, b in zip(f, f[1:] + f[:1]):
        pa, pb = P[a], P[b]
        n += np.array([(pa[1] - pb[1]) * (pa[2] + pb[2]),
                       (pa[2] - pb[2]) * (pa[0] + pb[0]),
                       (pa[0] - pb[0]) * (pa[1] + pb[1])])
    return n


def edge_faces(mesh):
    out = defaultdict(list)
    for fi, f in enumerate(mesh.faces):
        for a, b in zip(f, f[1:] + f[:1]):
            out[edge(a, b)].append(fi)
    return out


def crease(mesh, degrees):
    """The sharp edges: open ones, ones of more than two faces, and ones
    whose faces meet at more than `degrees`."""
    P = mesh.X[:, PLACE]
    normals = []
    for f in mesh.faces:
        n = face_normal(P, f)
        length = np.linalg.norm(n)
        normals.append(n / length if length > 0 else n)
    limit = math.cos(math.radians(degrees))
    sharp = set()
    for e, fs in edge_faces(mesh).items():
        if len(fs) != 2 or float(np.dot(normals[fs[0]], normals[fs[1]])) < limit:
            sharp.add(e)
    mesh.sharp = sharp
    return len(sharp)


class Subdivision:
    """One round of Catmull-Clark on a mesh's shape: the new faces and
    sharp edges, and the new points as a sparse sum of the old (rows,
    columns, weights), so that it can be applied to any numbers the
    points carry. The first points of the new mesh are the old points' own,
    in their order."""

    def __init__(self, mesh):
        n = len(mesh.X)
        efaces = edge_faces(mesh)
        edges = sorted(efaces)
        eindex = {e: k for k, e in enumerate(edges)}
        nf = len(mesh.faces)
        first_edge = n
        first_face = n + len(edges)
        rows, cols, vals = [], [], []

        def put(row, col, w):
            rows.append(row)
            cols.append(col)
            vals.append(w)

        # face points
        for fi, f in enumerate(mesh.faces):
            for p in f:
                put(first_face + fi, p, 1.0 / len(f))
        # edge points
        sharp = set(mesh.sharp)
        for e in edges:
            fs = efaces[e]
            row = first_edge + eindex[e]
            if e in sharp or len(fs) != 2:
                sharp.add(e)
                put(row, e[0], 0.5)
                put(row, e[1], 0.5)
            else:
                put(row, e[0], 0.25)
                put(row, e[1], 0.25)
                for fi in fs:
                    for p in mesh.faces[fi]:
                        put(row, p, 0.25 / len(mesh.faces[fi]))
        # vertex points
        vfaces = defaultdict(list)
        vedges = defaultdict(list)
        for fi, f in enumerate(mesh.faces):
            for p in f:
                vfaces[p].append(fi)
        for e in edges:
            vedges[e[0]].append(e)
            vedges[e[1]].append(e)
        for v in range(n):
            es = vedges.get(v, [])
            hard = [e for e in es if e in sharp]
            if not es or len(hard) > 2:
                put(v, v, 1.0)
            elif len(hard) == 2:
                put(v, v, 0.75)
                for e in hard:
                    put(v, e[0] if e[1] == v else e[1], 0.125)
            else:
                k = len(es)
                fs = vfaces[v]
                # (Q + 2R + (k - 3) V) / k
                for fi in fs:
                    for p in mesh.faces[fi]:
                        put(v, p, 1.0 / len(fs) / len(mesh.faces[fi]) / k)
                for e in es:
                    put(v, e[0], 1.0 / k / k)
                    put(v, e[1], 1.0 / k / k)
                put(v, v, (k - 3.0) / k)
        self.rows = np.array(rows)
        self.cols = np.array(cols)
        self.vals = np.array(vals)
        self.count = first_face + nf
        faces, uvs, lit, new_sharp = [], [], [], set()
        for fi, f in enumerate(mesh.faces):
            k = len(f)
            uv = [np.array(t) for t in mesh.uvs[fi]]
            centre = sum(uv) / k
            for i in range(k):
                a, b, z = f[i], f[(i + 1) % k], f[i - 1]
                faces.append([a, first_edge + eindex[edge(a, b)], first_face + fi,
                              first_edge + eindex[edge(z, a)]])
                uvs.append([tuple(uv[i]), tuple((uv[i] + uv[(i + 1) % k]) / 2), tuple(centre),
                            tuple((uv[i - 1] + uv[i]) / 2)])
                lit.append(mesh.lit[fi])
        for e in sharp:
            mid = first_edge + eindex[e]
            new_sharp.add(edge(e[0], mid))
            new_sharp.add(edge(mid, e[1]))
        self.faces, self.uvs, self.lit, self.sharp = faces, uvs, lit, new_sharp

    def apply(self, X):
        out = np.zeros((self.count, X.shape[1]))
        np.add.at(out, self.rows, self.vals[:, None] * X[self.cols])
        return out

    def mesh(self, X):
        return Mesh(self.apply(X), self.faces, self.uvs, self.lit, self.sharp)


def rebuild(mesh, levels, fit):
    """Subdivided `levels` times; with `fit`, the coarse points moved so
    that the subdivided surface passes through the game's own vertices."""
    steps = []
    m = mesh
    for _ in range(levels):
        s = Subdivision(m)
        steps.append(s)
        m = Mesh(np.zeros((s.count, 1)), s.faces, s.uvs, s.lit, s.sharp)
    n = len(mesh.X)

    def run(X):
        for s in steps:
            X = s.apply(X)
        return X

    target = mesh.X
    C = target.copy()
    for _ in range(fit):
        C = C + (target - run(C)[:n])
    X = run(C)
    last = steps[-1] if steps else None
    if last is None:
        return mesh, 0.0
    miss = float(np.abs(X[:n, PLACE] - target[:, PLACE]).max())
    return Mesh(X, last.faces, last.uvs, last.lit, last.sharp), miss


def corner_normals(mesh):
    """Each corner's normal: the faces about its point that meet it across
    no sharp edge, their normals summed by area; nought for a face the
    game does not light."""
    P = mesh.X[:, PLACE]
    fn = [face_normal(P, f) for f in mesh.faces]
    parent = {}

    def find(c):
        while parent[c] != c:
            parent[c] = parent[parent[c]]
            c = parent[c]
        return c

    by_edge = defaultdict(list)
    for fi, f in enumerate(mesh.faces):
        k = len(f)
        for i in range(k):
            parent[(fi, i)] = (fi, i)
        for i in range(k):
            a, b = f[i], f[(i + 1) % k]
            by_edge[edge(a, b)].append((fi, i, (i + 1) % k))
    for e, uses in by_edge.items():
        if e in mesh.sharp or len(uses) != 2:
            continue
        (f0, i0, j0), (f1, i1, j1) = uses
        if mesh.lit[f0] != mesh.lit[f1]:
            continue
        # the corners of each face at each end of the edge
        ends0 = {mesh.faces[f0][i0]: (f0, i0), mesh.faces[f0][j0]: (f0, j0)}
        ends1 = {mesh.faces[f1][i1]: (f1, i1), mesh.faces[f1][j1]: (f1, j1)}
        for p in ends0:
            if p in ends1:
                parent[find(ends0[p])] = find(ends1[p])
    total = defaultdict(lambda: np.zeros(3))
    for fi, f in enumerate(mesh.faces):
        for i in range(len(f)):
            total[find((fi, i))] += fn[fi]
    out = []
    for fi, f in enumerate(mesh.faces):
        row = []
        for i in range(len(f)):
            n = total[find((fi, i))]
            length = np.linalg.norm(n)
            row.append(tuple(n / length) if mesh.lit[fi] and length > 0 else (0.0, 0.0, 0.0))
        out.append(row)
    return out


def model_of(md, mesh, max_terms=8):
    """The rebuilt mesh as a ModelData the exporter writes: each point's
    terms cut to the `max_terms` that weigh most (what is cut is put back
    at the bind pose onto the one kept that weighs most), its weights to
    four."""
    out = fe.ModelData(md.name)
    out.__dict__.update({k: v for k, v in md.__dict__.items() if k not in ('points', 'faces')})
    normals = corner_normals(mesh)
    rot = np.array(md.bind_rot, float)
    pos = np.array(md.bind_pos, float)
    worst = 0.0
    for row in mesh.X:
        terms = row[TERMS].reshape(BONES, 4)
        size = np.abs(terms[:, :3]).sum(1) + np.abs(terms[:, 3]) * 1000.0
        keep = [b for b in np.argsort(-size)[:max_terms] if size[b] > 1e-9]
        dropped = [b for b in range(BONES) if b not in keep and size[b] > 1e-9]
        kept = {int(b): [float(c) for c in terms[b]] for b in keep}
        if dropped and keep:
            # what is cut, as it is at the bind pose, onto the first kept
            lost = np.zeros(3)
            for b in dropped:
                lost += rot[b] @ terms[b, :3] + terms[b, 3] * pos[b]
            b0 = int(keep[0])
            share = sum(terms[b, 3] for b in dropped)
            kept[b0][3] += share
            local = rot[b0].T @ (lost - share * pos[b0])
            for k in range(3):
                kept[b0][k] += float(local[k])
            worst = max(worst, float(np.abs(lost).max()))
        w = row[WEIGHTS]
        order = [int(b) for b in np.argsort(-w)[:4] if w[b] > 1e-6]
        total = sum(w[b] for b in order) or 1.0
        out.point(tuple(row[PLACE]), kept, [(b, float(w[b] / total)) for b in order])
    for f, uv, nrm in zip(mesh.faces, mesh.uvs, normals):
        out.faces.append({'points': list(f), 'uvs': [tuple(t) for t in uv], 'normals': nrm})
    out.cut = worst
    # what the cut costs away from the bind pose: every bone turned half a
    # radian about an axis of its own, eight times
    full = mesh.X[:, TERMS].reshape(-1, BONES, 4)
    cut = np.zeros_like(full)
    for i, (_, kept, _) in enumerate(out.points):
        for b, t in kept.items():
            cut[i, b] = t
    rng = np.random.default_rng(1)
    out.cut_moved = 0.0
    for _ in range(8):
        turned = np.array([r @ np.array(fe.axis_angle(rng.uniform(-1, 1, 3), 0.5)) for r in rot])
        diff = full - cut
        moved = np.einsum('bij,nbj->ni', turned, diff[:, :, :3]) + diff[:, :, 3] @ pos
        out.cut_moved = max(out.cut_moved, float(np.abs(moved).max()))
    return out


# ---- the texture ----

def scale2x(a):
    """Scale2x (EPX) of an image array (h, w, c): each texel four, an edge
    between two colours kept sharp along diagonals."""
    p = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode='edge')
    A = p[:-2, 1:-1]
    B = p[1:-1, 2:]
    C = p[1:-1, :-2]
    D = p[2:, 1:-1]
    E = a

    def eq(x, y):
        return np.all(x == y, axis=2)

    out = np.zeros((a.shape[0] * 2, a.shape[1] * 2, a.shape[2]), a.dtype)
    cond = ~eq(A, D) & ~eq(C, B)
    e0 = np.where((cond & eq(C, A))[..., None], C, E)
    e1 = np.where((cond & eq(A, B))[..., None], B, E)
    e2 = np.where((cond & eq(D, C))[..., None], C, E)
    e3 = np.where((cond & eq(B, D))[..., None], B, E)
    out[0::2, 0::2] = e0
    out[0::2, 1::2] = e1
    out[1::2, 0::2] = e2
    out[1::2, 1::2] = e3
    return out


def upscale(picture, scale, how):
    """The atlas `scale` times larger. `scale2x`: Scale2x as often as it
    takes, then a light smoothing of the colours inside each colour's
    area, so that the steps of a diagonal are softened and no colour runs
    into another; `lanczos`: resampled. Alpha stays as hard as the
    game's: a texel is drawn or not."""
    a = np.asarray(picture.convert('RGBA'))
    if how == 'lanczos':
        big = np.asarray(picture.convert('RGBA').resize(
            (picture.width * scale, picture.height * scale), Image.LANCZOS))
        alpha = np.asarray(picture.convert('RGBA').getchannel('A').resize(
            (picture.width * scale, picture.height * scale), Image.NEAREST))
        out = big.copy()
        out[..., 3] = alpha
        return Image.fromarray(out, 'RGBA')
    big = a
    done = 1
    while done < scale:
        big = scale2x(big)
        done *= 2
    rgb = big[..., :3].astype(float)
    alpha = big[..., 3]
    # a box of 3 texels of the big picture, weighted to drawn texels
    w = (alpha > 127).astype(float)
    acc = np.zeros_like(rgb)
    wacc = np.zeros_like(w)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            k = 2.0 if dx == 0 and dy == 0 else (1.0 if dx == 0 or dy == 0 else 0.5)
            sw = np.roll(np.roll(w, dy, 0), dx, 1) * k
            acc += np.roll(np.roll(rgb, dy, 0), dx, 1) * sw[..., None]
            wacc += sw
    smooth = np.where(wacc[..., None] > 0, acc / np.maximum(wacc, 1e-9)[..., None], rgb)
    out = np.dstack([np.clip(smooth + 0.5, 0, 255).astype(np.uint8), alpha])
    return Image.fromarray(out, 'RGBA')


def png_bytes(image):
    buf = io.BytesIO()
    image.save(buf, 'PNG', optimize=True)
    return buf.getvalue()


# ---- a model, start to end ----

def recipe_of(recipe, name):
    out = dict(DEFAULTS)
    out.update(recipe.get('default', {}))
    kind = 'weapons' if name.startswith('W') else 'fighters'
    out.update(recipe.get(kind, {}))
    out.update(recipe.get('models', {}).get(name, {}))
    return out


def build(md, params, bones, out_dir):
    started = time.time()
    mesh = mesh_of(md)
    mesh, welded = weld(mesh, params['weld'])
    # the seams: open edges of pieces that come within `seam` units of
    # each other are closed into one surface
    mesh, sewn = weld(mesh, params['seam'], open_points(mesh))
    welded += sewn
    creased = crease(mesh, params['crease'])
    rebuilt, miss = rebuild(mesh, params['levels'], params['fit'])
    hd = model_of(md, rebuilt)
    atlas = Image.frombytes('RGBA', (md.atlas.w, md.atlas.h), bytes(md.atlas.pixels))
    texture = upscale(atlas, params['scale'], params['upscale'])
    hd.extras = dict(md.extras, rb_hd=dict(params))
    nv, nt = fe.write_model(hd, os.path.join(out_dir, md.name + '.glb'), bones, png_bytes(texture))
    print('%s: %d points (%d welded), %d faces, %d creased -> %d vertices, %d triangles; '
          'through the game\'s vertices within %.2f; terms cut by %.2f (%.2f turned); texture %dx%d; %.1f s'
          % (md.name, len(mesh.X), welded, len(mesh.faces), creased, nv, nt, miss, hd.cut, hd.cut_moved,
             texture.width, texture.height, time.time() - started))
    return hd


def names_of(spec):
    fighters = list(fe.MODEL_NAMES)
    weapons = ['W%02d' % i for i in range(fe.WEAPON_MODELS)]
    if spec == 'all':
        return fighters + weapons
    if spec == 'fighters':
        return fighters
    if spec == 'weapons':
        return weapons
    return spec.split(',')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', default=os.path.join(ROOT, 'Bushido Blade 2 (USA).cue'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'tmp', 'mods', 'hd'))
    ap.add_argument('--models', default='all', help='all, fighters, weapons, or names (M100,W00)')
    ap.add_argument('--recipe', default=os.path.join(ROOT, 'tools', 'fighter_hd.json'))
    ap.add_argument('--pose', help='a bind pose the port wrote, for the fighters (fighter_export --pose)')
    args = ap.parse_args()
    with open(args.recipe, encoding='utf-8') as f:
        recipe = json.load(f)
    models_dir = os.path.join(args.out, 'models')
    os.makedirs(models_dir, exist_ok=True)
    nd = fe.Ndata(args.image)
    bones, scales = fe.source_tables()
    good = True
    for name in names_of(args.models):
        params = recipe_of(recipe, name)
        if params.get('skip'):
            continue
        if name.startswith('W'):
            md = fe.read_weapon(nd, int(name[1:]))
        else:
            md = fe.read_model(nd, fe.MODEL_NAMES.index(name), bones, scales, args.pose, 24)
        if md.problems:
            print('%s: %s' % (name, '; '.join(md.problems[:3])))
            good = False
            continue
        build(md, params, bones, models_dir)
    with open(os.path.join(args.out, 'mod.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(recipe.get('mod', 'name = HD fighters\n'))
    return 0 if good else 1


if __name__ == '__main__':
    sys.exit(main())
