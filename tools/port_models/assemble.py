#!/usr/bin/env python3
"""A fighter's remade model put together as the port's file
(src/port/gl/gl_fighter_file.h), with its levels of detail and the
pictures the lighting reads (tools/port_models/README.md):

- the body: the hd mod's (tools/fighter_hd.py: the game's own mesh,
  subdivided, every vertex carrying the game's own sum of bones, so that
  it bends at a joint exactly as the game's), less the pieces made anew,
  and pulled in wherever its smoothing swelled it beyond the game's own
  surface by more than the recipe's `pull_in_units`;
- the new pieces (tools/blender/fighter_parts.py), each wholly on its bone;
- one atlas: the hd mod's texture above, the new pieces' below, the
  colour tables' picture (rb_palette) grown to match so that the game's
  tints and fades reach the new pieces too (each of their texels takes
  the entry of the game's own nearest in colour on the piece it replaces);
- beside the colour, in the same layout, a normal map and an
  occlusion-roughness-metal map (glTF's normalTexture, occlusionTexture
  and metallicRoughnessTexture: R occlusion, G roughness, B metal), which
  the port does not read yet (the lighting's, ROADMAP 14).

    python tools/port_models/assemble.py --name M101 [--work tmp/models/work]
            [--hd tmp/models/ref/hd2/models] [--lods tmp/models/ref/hd1/models,tmp/models/ref]
            [--out tmp/mods/remade/models]

Writes OUT/NAME.glb, and OUT/../lod/NAME.lod1.glb and .lod2.glb.
"""
import argparse
import copy
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fighter_glb as fg  # noqa: E402
from check import dominant  # noqa: E402
from recipe import recipe_of  # noqa: E402

K = 8  # terms a vertex


class Mesh:
    """Arrays of a model to be written: pos, nrm, uv, joints, weights,
    terms (N, 8, 4), tjoints (N, 8), tris."""

    def __init__(self, **kw):
        self.__dict__.update(kw)

    @staticmethod
    def of_model(m):
        return Mesh(pos=m.pos.copy(), nrm=m.nrm.copy(), uv=m.uv.copy(), joints=m.joints.copy(),
                    weights=m.weights.copy(), terms=m.terms.copy(), tjoints=m.tjoints.copy(),
                    tris=m.tris.copy())

    def keep(self, tri_mask):
        """Only the triangles asked, and the vertices they use."""
        tris = self.tris[tri_mask]
        used = np.unique(tris)
        index = -np.ones(len(self.pos), np.int64)
        index[used] = np.arange(len(used))
        out = Mesh(**{k: v[used] for k, v in self.__dict__.items() if k != 'tris'})
        out.tris = index[tris]
        return out, used

    def plus(self, other):
        n = len(self.pos)
        out = Mesh(**{k: np.concatenate([v, getattr(other, k)]) for k, v in self.__dict__.items() if k != 'tris'})
        out.tris = np.concatenate([self.tris, other.tris + n])
        return out


def rigid_piece(piece, bone, R, T):
    """A new piece wholly on its bone: each vertex's one term is its place
    in the bone's frame, the whole of the bone's place in it."""
    p = np.array(piece['points'], float)
    n = np.array(piece['normals'], float)
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    count = len(p)
    terms = np.zeros((count, K, 4))
    terms[:, 0, :3] = p
    terms[:, 0, 3] = 1.0
    tj = np.zeros((count, K), np.int64)
    tj[:, 0] = bone
    joints = np.zeros((count, 4), np.int64)
    joints[:, 0] = bone
    weights = np.zeros((count, 4))
    weights[:, 0] = 1.0
    return Mesh(pos=p @ R[bone].T + T[bone], nrm=n @ R[bone].T, uv=np.array(piece['uvs'], float),
                joints=joints, weights=weights, terms=terms, tjoints=tj,
                tris=np.array(piece['triangles'], np.int64))


def outward(mesh, sign):
    """Each triangle turned so that its corners go round its vertices'
    normals as the game's own files' do (`sign` of the reference)."""
    a, b, c = (mesh.pos[mesh.tris[:, k]] for k in range(3))
    face = np.cross(b - a, c - a)
    nsum = mesh.nrm[mesh.tris].sum(1)
    wrong = np.sign((face * nsum).sum(1)) != sign
    mesh.tris[wrong] = mesh.tris[wrong][:, [0, 2, 1]]
    return int(wrong.sum())


def winding_sign(m):
    a, b, c = (m.pos[m.tris[:, k]] for k in range(3))
    face = np.cross(b - a, c - a)
    d = (face * m.nrm[m.tris].sum(1)).sum(1)
    return 1 if (d > 0).sum() >= (d < 0).sum() else -1


def nearest_on_triangles(points, A, B, C):
    """For each point the nearest point of any triangle, and that
    triangle's number (brute force, in blocks)."""
    best_d = np.full(len(points), np.inf)
    best_p = np.zeros_like(points)
    best_t = np.zeros(len(points), np.int64)
    for s in range(0, len(points), 512):
        P = points[s:s + 512, None, :]
        ab, ac = B - A, C - A
        ap = P - A
        d1, d2 = (ab * ap).sum(2), (ac * ap).sum(2)
        bp = P - B
        d3, d4 = (ab * bp).sum(2), (ac * bp).sum(2)
        cp = P - C
        d5, d6 = (ab * cp).sum(2), (ac * cp).sum(2)
        va = d3 * d6 - d5 * d4
        vb = d5 * d2 - d1 * d6
        vc = d1 * d4 - d3 * d2
        denom = np.where(np.abs(va + vb + vc) < 1e-12, 1e-12, va + vb + vc)
        v = vb / denom
        w = vc / denom
        q_face = A + ab * v[..., None] + ac * w[..., None]
        # outside the triangle: the nearest point of its three edges
        q_edge, d_edge = None, None
        for (E0, E1) in ((A, B), (B, C), (C, A)):
            e = E1 - E0
            t = np.clip(((P - E0) * e).sum(2) / np.maximum((e * e).sum(1), 1e-12), 0, 1)
            qe = E0 + e * t[..., None]
            de = ((P - qe) ** 2).sum(2)
            if q_edge is None:
                q_edge, d_edge = qe, de
            else:
                take = de < d_edge
                q_edge = np.where(take[..., None], qe, q_edge)
                d_edge = np.minimum(de, d_edge)
        inside = (va >= 0) & (vb >= 0) & (vc >= 0)
        q = np.where(inside[..., None], q_face, q_edge)
        dist = ((P - q) ** 2).sum(2)
        k = dist.argmin(1)
        rows = np.arange(len(k))
        best_d[s:s + 512] = dist[rows, k]
        best_p[s:s + 512] = q[rows, k]
        best_t[s:s + 512] = k
    return best_p, best_t, np.sqrt(best_d)


def pull_in(mesh, ref, R, limit):
    """Where the smoothed body swells beyond the game's own surface by more
    than `limit` units, its vertex is put back to that much outside it,
    the change carried by the term that weighs most, in its bone's frame
    at the bind pose."""
    sign = winding_sign(ref)
    A, B, C = (ref.pos[ref.tris[:, k]] for k in range(3))
    N = np.cross(B - A, C - A)
    # outward by the face's own normals where it is lit (a mirrored piece
    # goes round the other way), by the file's winding where it is not
    lit = ref.nrm[ref.tris].sum(1)
    turn = np.sign((N * lit).sum(1))
    turn[np.abs((N * lit).sum(1)) < 1e-9] = sign
    N = N * turn[:, None]
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
    # each vertex held to the game's own surface of the same bone: at the
    # bind pose the pieces of two bones can cross (the feet), and the
    # nearest surface would be the other's
    q = np.zeros_like(mesh.pos)
    t = np.zeros(len(mesh.pos), np.int64)
    share = np.abs(mesh.terms[:, :, 3])
    top = share.max(1) / np.maximum(share.sum(1), 1e-9)
    own = np.where(top >= 0.95, mesh.tjoints[np.arange(len(mesh.pos)), share.argmax(1)], -1)
    ref_share = np.abs(ref.terms[:, :, 3])
    carries = np.zeros((len(ref.pos), fg.BONES), bool)
    for k in range(ref.terms.shape[1]):
        carries[np.arange(len(ref.pos)), ref.tjoints[:, k]] |= ref_share[:, k] > 0.05
    for b in np.unique(own):
        sel = own == b
        faces = np.arange(len(ref.tris)) if b < 0 else np.nonzero(carries[ref.tris, b].any(1))[0]
        if len(faces) == 0:
            faces = np.arange(len(ref.tris))
        qb, tb, _ = nearest_on_triangles(mesh.pos[sel], A[faces], B[faces], C[faces])
        q[sel], t[sel] = qb, faces[tb]
    out = ((mesh.pos - q) * N[t]).sum(1)
    move = out > limit
    target = q + N[t] * limit
    delta = np.where(move[:, None], target - mesh.pos, 0.0)
    share = np.abs(mesh.terms[:, :, 3])
    k = share.argmax(1)
    rows = np.arange(len(mesh.pos))
    b = mesh.tjoints[rows, k]
    local = np.einsum('nji,nj->ni', R[b], delta)  # R^T delta
    mesh.terms[rows, k, :3] += local
    mesh.pos = mesh.pos + delta
    return int(move.sum()), float(out.max())


# ---- the face ----

def subdivide(mesh, tri_mask):
    """The triangles asked each made four, a new vertex at the middle of
    each of their edges carrying the mean of its two ends' everything
    (the terms are linear in the place, so the new vertex bends with its
    ends)."""
    edges = {}
    new_rows = []
    n = len(mesh.pos)
    out_tris = [mesh.tris[~tri_mask]]
    made = []
    for a, b, c in mesh.tris[tri_mask]:
        mids = []
        for x, y in ((a, b), (b, c), (c, a)):
            key = (min(x, y), max(x, y))
            if key not in edges:
                edges[key] = n + len(new_rows)
                new_rows.append(key)
            mids.append(edges[key])
        ab, bc, ca = mids
        made += [[a, ab, ca], [ab, b, bc], [ca, bc, c], [ab, bc, ca]]
    if not new_rows:
        return mesh, np.zeros(0, bool)
    i, j = np.array(new_rows).T
    for k in ('pos', 'nrm', 'uv', 'weights', 'terms'):
        v = getattr(mesh, k)
        setattr(mesh, k, np.concatenate([v, (v[i] + v[j]) / 2]))
    for k in ('joints', 'tjoints'):
        v = getattr(mesh, k)
        setattr(mesh, k, np.concatenate([v, v[i]]))
    made = np.array(made, np.int64)
    mesh.tris = np.concatenate(out_tris + [made])
    mask = np.zeros(len(mesh.tris), bool)
    mask[-len(made):] = True
    return mesh, mask


def face_pass(mesh, ref, R, recipe, atlas_size):
    """The face's relief (face.py) on the front of the head."""
    import face as fc
    f = fc.features(ref, recipe, atlas_size)
    side, up, out = fc.axes(f)
    width = np.linalg.norm(f['eye_left'] - f['eye_right'])
    bs = fc.bumps(f, recipe)
    ibm = ref.ibm[fc.HEAD]
    rows = np.arange(len(mesh.pos))

    def head_frame(m):
        return (np.concatenate([m.pos, np.ones((len(m.pos), 1))], 1) @ ibm.T)[:, :3]

    def front(m):
        share = np.abs(m.terms[:, :, 3])
        on_head = m.tjoints[np.arange(len(m.pos)), share.argmax(1)] == fc.HEAD
        p = head_frame(m)
        n = m.nrm @ ibm[:3, :3].T
        centre = (f['eye_left'] + f['eye_right'] + f['mouth']) / 3
        d = p - centre
        inside = (((d @ up) / (width * 1.15)) ** 2 + ((d @ side) / (width * 0.95)) ** 2) < 1.0
        inside &= (d @ out) > -width * 0.35
        facing = (n @ out) > 0.1
        return on_head & inside & facing
    for _ in range(recipe.get('subdivide', 2)):
        sel = front(mesh)
        mesh, _ = subdivide(mesh, np.all(sel[mesh.tris], 1) | (sel[mesh.tris].sum(1) >= 2))
    sel = front(mesh)
    p = head_frame(mesh)
    n = mesh.nrm @ ibm[:3, :3].T
    h = np.where(sel, fc.relief(p, bs, side, up), 0.0)
    # the normals turned by the relief's slope
    eps = 0.5
    grad = np.zeros_like(p)
    for k in range(3):
        e = np.zeros(3)
        e[k] = eps
        grad[:, k] = (fc.relief(p + e, bs, side, up) - fc.relief(p - e, bs, side, up)) / (2 * eps)
    grad = np.where(sel[:, None], grad, 0.0)
    gt = grad - (grad * n).sum(1, keepdims=True) * n
    n2 = n - gt
    n2 /= np.maximum(np.linalg.norm(n2, axis=1, keepdims=True), 1e-9)
    lit = np.linalg.norm(mesh.nrm, axis=1) > 1e-6
    head_to_world = np.linalg.inv(ibm)[:3, :3]
    delta = (n * h[:, None]) @ head_to_world.T
    mesh.nrm = np.where((sel & lit)[:, None], n2 @ head_to_world.T, mesh.nrm)
    share = np.abs(mesh.terms[:, :, 3])
    k = share.argmax(1)
    rows = np.arange(len(mesh.pos))
    b = mesh.tjoints[rows, k]
    mesh.terms[rows, k, :3] += np.einsum('nji,nj->ni', R[b], delta)
    mesh.pos = mesh.pos + delta
    return int(sel.sum()), float(np.abs(h).max())


# ---- the pictures ----

def place_parts(atlas, parts_img):
    """The atlas above and the parts' square below, the whole twice as
    tall: the body's texture points' v halved, the parts' put below."""
    h, w = atlas.shape[:2]
    side = min(w, h)
    from PIL import Image
    parts = np.array(Image.fromarray(parts_img).resize((side, side), Image.LANCZOS))
    out = np.zeros((h + side, w, 4), np.uint8)
    out[:h] = atlas
    out[h:h + side, :side] = parts
    return out, h / (h + side), side / w


def body_maps(atlas):
    """The body's normal and occlusion-roughness-metal maps from its
    colours: a fine relief from the texture's own light and dark, and each
    texel's roughness and metal by what it looks like it is (the gold of
    the trim, the dark of leather and hair, cloth, skin)."""
    rgb = atlas[..., :3].astype(float) / 255.0
    lum = rgb @ np.array([0.299, 0.587, 0.114])
    # relief: the luminance less its blur, as heights
    blur = lum.copy()
    for _ in range(3):
        blur = (np.roll(blur, 1, 0) + np.roll(blur, -1, 0) + np.roll(blur, 1, 1) + np.roll(blur, -1, 1) + blur) / 5
    height = lum - blur
    gx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 2.0
    gy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 2.0
    n = np.stack([-gx, gy, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    normal = ((n * 0.5 + 0.5) * 255).round().astype(np.uint8)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx, mn = rgb.max(-1), rgb.min(-1)
    sat = (mx - mn) / np.maximum(mx, 1e-6)
    # gold: yellow, not the tan of skin (whose green is well under its red)
    gold = (r > 0.35) & (g > r * 0.72) & (b < g * 0.55) & (sat > 0.35)
    skin = (r > g) & (g > b) & (r > 0.45) & ~gold
    dark = mx < 0.18
    rough = np.full(lum.shape, 0.82)
    rough[skin] = 0.55
    rough[dark] = 0.5
    rough[gold] = 0.35
    metal = np.where(gold, 0.85, 0.0)
    orm = np.stack([np.ones_like(rough), rough, metal], -1)
    orm = (orm * 255).round().astype(np.uint8)
    alpha = np.full(lum.shape + (1,), 255, np.uint8)
    return np.concatenate([normal, alpha], -1), np.concatenate([orm, alpha], -1)


def palette_index(index_img, ref, dom, parts_albedo, atlas_h, index_scale, bones):
    """The colour tables' picture grown below by the parts' square: each of
    its texels the entry, of the game's own on the pieces replaced, whose
    colour now is nearest the new texel's."""
    from PIL import Image
    ih, iw = index_img.shape[:2]
    side = int(round(iw * index_scale)) if index_scale < 1 else iw
    side = min(side, iw)
    grown_h = int(round(ih / atlas_h))
    out = np.zeros((grown_h, iw, 4), np.uint8)
    out[:ih] = index_img
    # the game's entries on the replaced pieces, and their colours
    gatlas = fg.png_array(ref.base_color_png()).astype(float)
    gh, gw = gatlas.shape[:2]
    sel = np.isin(dom, bones)
    tri = np.all(sel[ref.tris], 1)
    rng = np.random.default_rng(2)
    entries = {}
    for t in ref.tris[tri]:
        for _ in range(16):
            a, b = rng.random(2)
            if a + b > 1:
                a, b = 1 - a, 1 - b
            p = ref.uv[t[0]] + a * (ref.uv[t[1]] - ref.uv[t[0]]) + b * (ref.uv[t[2]] - ref.uv[t[0]])
            x, y = min(gw - 1, int(p[0] * gw)), min(gh - 1, int(p[1] * gh))
            e = index_img[min(ih - 1, int(p[1] * ih)), min(iw - 1, int(p[0] * iw))]
            if e[3] == 0:
                continue
            entries[(int(e[0]), int(e[1]))] = gatlas[y, x, :3]
    if not entries:
        return out
    keys = list(entries)
    cols = np.array([entries[k] for k in keys])
    small = np.array(Image.fromarray(parts_albedo).resize((side, side), Image.BOX)).astype(float)
    d = ((small[..., None, :3] - cols[None, None]) ** 2).sum(-1)
    k = d.argmin(-1)
    pick = np.array(keys)[k]
    region = out[ih:ih + side, :side]
    region[..., 0] = pick[..., 0]
    region[..., 1] = pick[..., 1]
    region[..., 3] = np.where(small[..., 3] > 0, 255, 0)
    return out


# ---- writing ----

def write(path, mesh, template, images, extras, parts_uv_note=None):
    """The file: one skin of the 21 bones (the template's nodes and inverse
    bind matrices), one primitive, its material's colour, normals and
    occlusion-roughness-metal, and the colour tables' picture."""
    g = template.gltf
    w = fg.GlbWriter()
    nrm = mesh.nrm / np.maximum(np.linalg.norm(mesh.nrm, axis=1, keepdims=True), 1e-9)
    nrm[np.linalg.norm(mesh.nrm, axis=1) < 1e-6] = 0
    attributes = {
        'POSITION': w.array(mesh.pos, 5126, 'VEC3', 34962, minmax=True),
        'NORMAL': w.array(nrm, 5126, 'VEC3', 34962),
        'TEXCOORD_0': w.array(mesh.uv, 5126, 'VEC2', 34962),
        'JOINTS_0': w.array(mesh.joints, 5121, 'VEC4', 34962),
        'WEIGHTS_0': w.array(mesh.weights, 5126, 'VEC4', 34962),
        '_RB_TJOINTS': w.array(mesh.tjoints[:, :4], 5121, 'VEC4', 34962),
        '_RB_TJOINTS1': w.array(mesh.tjoints[:, 4:], 5121, 'VEC4', 34962),
    }
    for k in range(K):
        attributes['_RB_TERM%d' % k] = w.array(mesh.terms[:, k], 5126, 'VEC4', 34962)
    indices = w.array(mesh.tris.reshape(-1, 1), 5125, 'SCALAR', 34963)
    ibm = np.stack([template.ibm[b].T.reshape(16) for b in range(fg.BONES)])
    a_ibm = w.array(ibm, 5126, 'MAT4')
    imgs = []
    for name, data in images:
        entry = {'bufferView': w.view(data), 'mimeType': 'image/png'}
        if name:
            entry['name'] = name
        imgs.append(entry)
    names = [n for n, _ in images]
    tex = lambda name: names.index(name)  # noqa: E731
    material = {'name': 'rb_remade', 'pbrMetallicRoughness': {
        'baseColorTexture': {'index': tex('rb_albedo')}, 'metallicFactor': 1.0, 'roughnessFactor': 1.0,
        'metallicRoughnessTexture': {'index': tex('rb_orm')}},
        'normalTexture': {'index': tex('rb_normal')}, 'occlusionTexture': {'index': tex('rb_orm')},
        'alphaMode': 'MASK', 'alphaCutoff': 0.5}
    extras = dict(extras)
    if 'rb_palette_index' in names:
        extras['rb_palette'] = dict(extras['rb_palette'], image=tex('rb_palette_index'))
    gltf = {
        'asset': {'version': '2.0', 'generator': 'ReBushido tools/port_models/assemble.py'},
        'scene': 0, 'scenes': copy.deepcopy(g['scenes']), 'nodes': copy.deepcopy(g['nodes']),
        'skins': [{'joints': g['skins'][0]['joints'], 'skeleton': g['skins'][0].get('skeleton', 1),
                   'inverseBindMatrices': a_ibm}],
        'meshes': [{'name': extras.get('model_file', 'fighter'),
                    'primitives': [{'attributes': attributes, 'indices': indices, 'material': 0}]}],
        'materials': [material],
        'textures': [{'source': i, 'sampler': 0} for i in range(len(imgs))],
        'samplers': [{'magFilter': 9729, 'minFilter': 9987, 'wrapS': 33071, 'wrapT': 33071}],
        'images': imgs, 'extras': extras,
    }
    # the skin's joints are the bones in order, so JOINTS_0 is the bone
    for j, node in enumerate(gltf['skins'][0]['joints']):
        assert gltf['nodes'][node]['name'] == 'bone%02d' % j
    w.write(path, gltf)


def assemble_level(base, ref, parts, level_parts, R, T, limit, atlas_v, parts_uv, sign, face=None):
    """One level: the base's body less the pieces made anew, pulled in,
    with the new pieces."""
    body = Mesh.of_model(base)
    replaced = sorted({p['bone'] for p in level_parts})
    dom = dominant(base)
    gone = np.all(np.isin(dom[body.tris], replaced), 1)
    body, _ = body.keep(~gone)
    moved, worst = pull_in(body, ref, R, limit) if limit is not None else (0, 0.0)
    faced = face_pass(body, ref, R, face[0], face[1]) if face else (0, 0.0)
    body.uv = body.uv * np.array([1.0, atlas_v])
    out = body
    flipped = 0
    for p in level_parts:
        piece = rigid_piece(p, p['bone'], R, T)
        piece.uv = np.stack([piece.uv[:, 0] * parts_uv[0], atlas_v + piece.uv[:, 1] * parts_uv[1]], 1)
        flipped += outward(piece, sign)
        out = out.plus(piece)
    return out, {'removed': int(gone.sum()), 'pulled_in': moved, 'swelled': worst, 'turned': flipped,
                 'faced': faced}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--work', default='tmp/models/work')
    ap.add_argument('--ref', default='tmp/models/ref')
    ap.add_argument('--hd', default='tmp/models/ref/hd2/models')
    ap.add_argument('--lods', default='tmp/models/ref/hd1/models,tmp/models/ref')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    r = recipe_of(args.name)
    work = os.path.join(args.work, args.name)
    with open(os.path.join(work, 'parts.json'), encoding='utf-8') as f:
        parts = json.load(f)
    ref = fg.Model(os.path.join(args.ref, args.name + '.glb'))
    hd = fg.Model(os.path.join(args.hd, args.name + '.glb'))
    R, T = ref.bind()
    sign = winding_sign(ref)
    # the atlas: the hd mod's above, the parts' below
    atlas = fg.png_array(hd.base_color_png())
    from PIL import Image
    load = lambda n: np.array(Image.open(os.path.join(work, 'parts_%s.png' % n)).convert('RGBA'))  # noqa: E731
    p_albedo = load('albedo')
    p_alpha = p_albedo.copy()
    combined, atlas_v, u_share = place_parts(atlas, p_albedo)
    parts_uv = (u_share, 1.0 - atlas_v)
    normal_b, orm_b = body_maps(atlas)
    p_normal = load('normal')
    p_orm = np.stack([load('ao')[..., 0], load('rough')[..., 0], load('metal')[..., 0],
                      np.full(p_albedo.shape[:2], 255, np.uint8)], -1)
    normal, _, _ = place_parts(normal_b, p_normal)
    orm, _, _ = place_parts(orm_b, p_orm)
    # occlusion, roughness and metal change slowly: half the colour's size
    orm = np.array(Image.fromarray(orm).resize((orm.shape[1] // 2, orm.shape[0] // 2), Image.BOX))
    # where the parts' texture is not drawn on, its neighbours' colour
    images = [('rb_albedo', fg.png_bytes(combined)), ('rb_normal', fg.png_bytes(normal)),
              ('rb_orm', fg.png_bytes(orm))]
    extras = dict(hd.extras)
    extras['rb_remade'] = {'recipe': r.get('about', ''), 'parts': sorted({p['bone'] for p in parts['pieces']}),
                           'maps': 'rb_albedo, rb_normal (tangent space, OpenGL), rb_orm (glTF: R occlusion, '
                                   'G roughness, B metal); the body\'s above, the new pieces\' below'}
    if 'rb_palette' in extras and len(hd.images()) > 1:
        idx = fg.png_array(hd.images()[extras['rb_palette']['image']])
        grown = palette_index(idx, ref, dominant(ref), p_alpha, atlas_v, u_share,
                              sorted({p['bone'] for p in parts['pieces']}))
        images.append(('rb_palette_index', fg.png_bytes(grown)))
    if 'rb_cloth' in extras:
        cloth = copy.deepcopy(extras['rb_cloth'])
        for c in cloth:
            c['uvs'] = [[x * (atlas_v if k % 2 else 1.0) for k, x in enumerate(face)] for face in c['uvs']]
        extras['rb_cloth'] = cloth
    os.makedirs(args.out, exist_ok=True)
    lod_dir = os.path.join(os.path.dirname(os.path.abspath(args.out)), 'lod')
    os.makedirs(lod_dir, exist_ok=True)
    levels = [hd] + [fg.Model(os.path.join(d, args.name + '.glb')) for d in args.lods.split(',') if d]
    for level, base in enumerate(levels):
        pieces = [p for p in parts['pieces'] if p.get('level', 0) == level]
        face = None
        if r.get('face') and level < 2:
            face = (dict(r['face'], subdivide=1 - level), fg.png_array(ref.base_color_png()).shape[1::-1])
        mesh, said = assemble_level(base, ref, parts, pieces, R, T,
                                    r['pull_in_units'] if level < 2 else None, atlas_v, parts_uv, sign, face)
        path = (os.path.join(args.out, args.name + '.glb') if level == 0
                else os.path.join(lod_dir, '%s.lod%d.glb' % (args.name, level)))
        write(path, mesh, hd, images, extras)
        print('assemble: %s: %d vertices, %d triangles (%d of the base\'s taken out, %d vertices pulled in from '
              'as much as %.1f units out, %d triangles turned; the face: %d vertices, raised or sunk by up to %.1f)'
              % (path, len(mesh.pos), len(mesh.tris), said['removed'], said['pulled_in'], said['swelled'],
                 said['turned'], said['faced'][0], said['faced'][1]))


if __name__ == '__main__':
    main()
