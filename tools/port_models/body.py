"""The body of a remade fighter: the hd mod's (the game's own mesh,
subdivided, each vertex carrying the game's own sum of bones) less the
pieces made anew and held inside the game's surface, given a texture
layout of its own (cut where the game's layout is cut, laid flat in
Blender, every island at one scale and none over another), and its
pictures painted anew in that layout from what the game's painting says
each texel is (tools/port_models/README.md, "The body").
"""
import json
import os

import numpy as np

import bake
import fighter_glb as fg
from check import dominant
from charts import Chart
from meshes import Mesh, pull_in, renormal, smooth


def body_mesh(ref, hd, recipe):
    """The hd mod's mesh less the triangles wholly on the bones whose
    pieces are made anew, pulled in to the game's surface."""
    R, _ = ref.bind()
    body = Mesh.of_model(hd)
    replaced = sorted(set(recipe['replace'].values()))
    dom = dominant(hd)
    gone = np.all(np.isin(dom[body.tris], replaced), 1)
    # a piece made anew with the skin of its joint (the head with the
    # neck): every triangle any of that bone is in
    for bone in recipe.get('replace_joint', []):
        share = np.zeros(len(hd.pos))
        for k in range(hd.terms.shape[1]):
            share += np.where(hd.tjoints[:, k] == bone, np.abs(hd.terms[:, k, 3]), 0)
        gone |= (share[body.tris] > 0.05).any(1)
    body, _ = body.keep(~gone)
    evened = smooth(body, R, recipe.get('smooth_rounds', 8))
    moved, worst = pull_in(body, ref, R, recipe['pull_in_units'])
    renormal(body)
    return body, {'removed': int(gone.sum()), 'evened_units': evened, 'pulled_in': moved, 'swelled': worst}


def cut(body):
    """The body as one surface (vertices at one place one point) and the
    edges its texture layout is to be cut along: where the game's layout
    is cut, where it folds back over itself, and where more than two
    faces meet."""
    key = np.round(body.pos * 8).astype(np.int64)
    points, first, weld = np.unique(key, axis=0, return_index=True, return_inverse=True)
    weld = weld.ravel()
    faces = weld[body.tris]
    whole = (faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & (faces[:, 0] != faces[:, 2])
    uv = body.uv
    a, b, c = (uv[body.tris[:, k]] for k in range(3))
    turn = np.sign((b[:, 0] - a[:, 0]) * (c[:, 1] - a[:, 1]) - (c[:, 0] - a[:, 0]) * (b[:, 1] - a[:, 1]))
    edges = {}
    for f in np.nonzero(whole)[0]:
        for k in range(3):
            va, vb = body.tris[f, k], body.tris[f, (k + 1) % 3]
            wa, wb = weld[va], weld[vb]
            if wa > wb:
                wa, wb, va, vb = wb, wa, vb, va
            edges.setdefault((int(wa), int(wb)), []).append((f, va, vb))
    seams = []
    for (wa, wb), users in edges.items():
        if len(users) == 1:
            continue
        seam = len(users) > 2
        if not seam:
            (f0, a0, b0), (f1, a1, b1) = users
            seam = (np.abs(uv[a0] - uv[a1]).max() > 1e-6 or np.abs(uv[b0] - uv[b1]).max() > 1e-6 or
                    (turn[f0] * turn[f1] < 0))
        if seam:
            seams.append([wa, wb])
    return {'points': body.pos[first], 'faces': faces, 'whole': whole, 'seams': seams, 'weld': weld}


def write_unwrap(body, path, aspect, margin=0.004):
    c = cut(body)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump({'points': c['points'].round(3).tolist(), 'faces': c['faces'][c['whole']].tolist(),
                   'seams': c['seams'], 'aspect': aspect, 'margin': margin}, f)
    return c


def laid_out(body, c, unwrapped):
    """The body with the new layout: a vertex for each of its places in
    the layout (its place in the game's layout kept beside, `old_uv`), and
    each triangle's island. An island that came out the mirror of what a
    normal map wants is turned over where it lies."""
    with open(unwrapped, encoding='utf-8') as f:
        data = json.load(f)
    uvs = np.zeros((len(body.tris), 3, 2))
    island = np.full(len(body.tris), -1, np.int64)
    uvs[c['whole']] = np.array(data['uvs'])
    island[c['whole']] = np.array(data['island'])
    # a face that is no more than a line takes a point of a neighbour's
    spare = uvs[c['whole']][0, 0]
    uvs[~c['whole']] = spare
    # mirrored islands turned over
    P = body.pos[body.tris]
    N = body.nrm[body.tris].sum(1)
    e1, e2 = P[:, 1] - P[:, 0], P[:, 2] - P[:, 0]
    d1, d2 = uvs[:, 1] - uvs[:, 0], uvs[:, 2] - uvs[:, 0]
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    face = (np.cross(e1, e2) * N).sum(1)
    # the layout turns as the face does (seen from outside, v down): the
    # port's frame then has its second axis up the picture
    hand = -np.sign(det) * np.sign(face)
    turned = 0
    for i in np.unique(island[island >= 0]):
        sel = island == i
        if hand[sel].sum() < 0:
            lo, hi = uvs[sel][..., 0].min(), uvs[sel][..., 0].max()
            uvs[sel, :, 0] = lo + hi - uvs[sel][..., 0]
            turned += 1
    # one vertex for each (vertex, place in the layout)
    corner_v = body.tris.ravel()
    corner_uv = np.round(uvs.reshape(-1, 2) * 65536).astype(np.int64)
    keys = np.concatenate([corner_v[:, None], corner_uv], 1)
    _, first, inverse = np.unique(keys, axis=0, return_index=True, return_inverse=True)
    src = corner_v[first]
    out = Mesh(**{k: v[src] for k, v in body.__dict__.items() if k != 'tris'})
    out.old_uv = body.uv[src]
    out.uv = uvs.reshape(-1, 2)[first]
    out.tris = inverse.ravel().reshape(-1, 3)
    return out, island, turned


def texel_scale(mesh, tris, size):
    """How many texels of a picture `size` the layout gives a unit of the
    surface (the middle of its triangles')."""
    P = mesh.pos[tris]
    U = mesh.uv[tris] * np.array(size)
    a3 = np.linalg.norm(np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0]), axis=1)
    d1, d2 = U[:, 1] - U[:, 0], U[:, 2] - U[:, 0]
    a2 = np.abs(d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1])
    ok = a3 > 1e-6
    return float(np.sqrt(np.median(a2[ok] / a3[ok]))), float(np.sqrt(a2[ok].sum() / a3[ok].sum()))


# ---- what the game's painting says each texel is ----

# what a material is like: how rough, how much a metal, how deep the folds
# the game painted on it are taken to be (units), how tall its grain is
# and how wide a lump of it, whether it stands proud of what is round it
# as a cord does (units), and how much of the game's painted light and
# dark is kept in the colour
KINDS = {
    'cloth': {'rough': 0.86, 'metal': 0.0, 'fold': 12.0, 'grain': 0.35, 'grain_units': 3.0, 'cord': 0.0, 'shade': 0.7, 'paint': 0.0, 'wrinkle': 5.0},
    'lining': {'rough': 0.6, 'metal': 0.0, 'fold': 4.0, 'grain': 0.15, 'grain_units': 3.0, 'cord': 0.0, 'shade': 0.6, 'paint': 0.0, 'wrinkle': 2.0},
    'linen': {'rough': 0.8, 'metal': 0.0, 'fold': 4.0, 'grain': 0.3, 'grain_units': 2.5, 'cord': 0.0, 'shade': 0.5, 'paint': 0.0, 'wrinkle': 2.0},
    'braid': {'rough': 0.4, 'metal': 0.55, 'fold': 0.0, 'grain': 0.5, 'grain_units': 5.0, 'cord': 2.5, 'shade': 0.25, 'paint': 0.0, 'wrinkle': 0.0},
    'leather': {'rough': 0.5, 'metal': 0.0, 'fold': 0.8, 'grain': 0.2, 'grain_units': 4.0, 'cord': 0.0, 'shade': 0.7, 'paint': 1.0, 'wrinkle': 0.0},
    'rubber': {'rough': 0.7, 'metal': 0.0, 'fold': 2.0, 'grain': 0.2, 'grain_units': 4.0, 'cord': 0.0, 'shade': 0.8, 'paint': 1.0, 'wrinkle': 0.0},
    'steel': {'rough': 0.3, 'metal': 0.9, 'fold': 1.0, 'grain': 0.1, 'grain_units': 4.0, 'cord': 0.0, 'shade': 0.6, 'paint': 0.0, 'wrinkle': 0.0},
    'skin': {'rough': 0.55, 'metal': 0.0, 'fold': 1.0, 'grain': 0.05, 'grain_units': 2.0, 'cord': 0.0, 'shade': 0.6, 'paint': 0.0, 'wrinkle': 0.0},
    'hair': {'rough': 0.5, 'metal': 0.0, 'fold': 2.0, 'grain': 0.2, 'grain_units': 2.0, 'cord': 0.0, 'shade': 0.7, 'paint': 0.0, 'wrinkle': 0.0},
}


def hex_color(text):
    return np.array([int(text[k:k + 2], 16) for k in (0, 2, 4)], np.float32)


def feature(rgb):
    """A colour as what tells materials apart: what hue it is of far more
    than how light it is."""
    c = np.asarray(rgb, np.float32) / 255.0
    v = c.max(-1, keepdims=True)
    lo = c.min(-1, keepdims=True)
    chroma = (c - lo) / np.maximum(v, 0.08)       # the hue and how strong, steady in the dark
    return np.concatenate([chroma * 1.0, v * 0.45], -1)


def classify(ref, materials):
    """The game's texture as materials: each texel the material whose seed
    colour its own is nearest (a seed `RRGGBB@T` is only of colour table
    T, and such a seed's table is no other material's). Returns the
    picture of material numbers and the names in order."""
    atlas = fg.png_array(ref.base_color_png())
    names = list(materials)
    seeds, owner, table = [], [], []
    for k, name in enumerate(names):
        for s in materials[name]['seeds']:
            color, _, t = s.partition('@')
            seeds.append(hex_color(color))
            owner.append(k)
            table.append(int(t) if t else -1)
    seeds, owner, table = np.array(seeds), np.array(owner), np.array(table)
    f_seed = feature(seeds)
    f = feature(atlas[..., :3])
    d = ((f[:, :, None, :] - f_seed[None, None]) ** 2).sum(-1)
    pal = ref.extras.get('rb_palette')
    if pal is not None and (table >= 0).any():
        idx = fg.png_array(ref.images()[pal['image']])
        tbl = np.where(idx[..., 3] > 0, idx[..., 1].astype(np.int64), -1)
        # a seed of a table is not for other tables' texels
        d = np.where((table[None, None] >= 0) & (table[None, None] != tbl[..., None]), 1e9, d)
        # ... and a table some material has claimed is that material's alone
        for t in np.unique(table[table >= 0]):
            mine = owner[table == t]
            others = ~np.isin(owner, mine)
            d = np.where((tbl[..., None] == t) & others[None, None], 1e9, d)
    return owner[d.argmin(-1)], names


# the limbs a garment wrinkles on: the bones of each in order from the
# body outwards, how thick it is (units), and how strongly it wrinkles
LIMBS = {
    'leg_a': ((13, 14), 90.0, 1.0), 'leg_b': ((16, 17), 90.0, 1.0),
    'arm_a': ((5, 6), 65.0, 0.9), 'arm_b': ((9, 10), 65.0, 0.9),
}
TRUNK = (2, 12, 4, 8)


def ridges(x, around, radius, along_units, around_units, seed):
    """Folds on a limb: noise on the cylinder round it, stretched, its
    crests where the noise passes nought (-1..1, the crests 1)."""
    p = np.stack([x / along_units, np.cos(around) * radius / around_units,
                  np.sin(around) * radius / around_units], -1)
    f = bake.fbm3(p, 1.0, 2, seed)
    return 1.0 - 2.0 * np.minimum(np.abs(f) * 2.2, 1.0)


def wrinkles(P, shares, ref):
    """The folds a garment has wherever it is worn (units, at each texel
    of place P at the bind pose, where every limb is straight): pressed
    together across a limb at its joint and where it ends, drawn out
    along it between, pulled at the waist, hanging below it. `shares` is
    how much of each texel each bone carries."""
    _, T = ref.bind()
    out = np.zeros(P.shape[:-1], np.float32)
    seed = 40
    for name, (bones, radius, strength) in LIMBS.items():
        w = shares[..., list(bones)].sum(-1)
        m = w > 0.02
        if not m.any():
            continue
        a, b = T[bones[0]], T[bones[1]]
        axis = (b - a) / np.linalg.norm(b - a)
        first = float(np.linalg.norm(b - a))
        q = P[m] - a
        x = q @ axis
        side = q - x[:, None] * axis
        # round the limb, from any two ways across it
        u = np.cross(axis, [1.0, 0.0, 0.0])
        u /= np.linalg.norm(u)
        v = np.cross(axis, u)
        around = np.arctan2(side @ v, side @ u)
        end = first + (x.max() - first)
        joint = np.exp(-((x - first) / 65.0) ** 2)
        cuff = np.exp(-((x - end) / 75.0) ** 2)
        top = np.exp(-(x / 80.0) ** 2)
        h = joint * 1.0 * ridges(x, around, radius, 15.0, 75.0, seed)
        h += cuff * 0.9 * ridges(x, around, radius, 17.0, 90.0, seed + 1)
        h += top * 0.7 * ridges(x + around * 25.0, around, radius, 24.0, 55.0, seed + 2)
        h += (1 - np.maximum(joint, np.maximum(cuff, top))) * 0.22 * ridges(x + 40.0 * np.sin(around * 2.0), around, radius, 150.0, 40.0, seed + 3)
        out[m] += (w[m] * strength * h).astype(np.float32)
        seed += 7
    w = shares[..., list(TRUNK)].sum(-1)
    m = w > 0.02
    if m.any():
        q = P[m] - T[TRUNK[0]]
        x = q[:, 1]                                   # down the trunk
        around = np.arctan2(q[:, 2], q[:, 0])
        waist = np.exp(-(x / 70.0) ** 2)
        skirt = np.clip((x - 40.0) / 120.0, 0, 1)
        chest = np.clip((-x - 40.0) / 120.0, 0, 1)
        h = waist * 0.7 * ridges(x, around, 170.0, 20.0, 110.0, 90)
        h += skirt * 0.6 * ridges(x + 50.0 * np.sin(around * 3.0), around, 190.0, 260.0, 46.0, 91)
        h += chest * 0.35 * ridges(x + around * 60.0, around, 170.0, 70.0, 70.0, 92)
        out[m] += (w[m] * h).astype(np.float32)
    return out


def bone_shares(mesh):
    """How much of each vertex each bone carries (N, 21)."""
    dense = np.zeros((len(mesh.pos), fg.BONES), np.float32)
    share = np.abs(mesh.terms[:, :, 3])
    for k in range(mesh.terms.shape[1]):
        np.add.at(dense, (np.arange(len(mesh.pos)), mesh.tjoints[:, k]), share[:, k])
    return dense / np.maximum(dense.sum(1, keepdims=True), 1e-9)


def paint(mesh, tid, bary, ref, recipe, scale, painted=None):
    """The body's pictures in its new layout: colour, height (units),
    roughness, metal, occlusion, and which material each texel is. Every
    material is one colour (the game's own of it, where its painting is
    lit), shaded by a share of the game's painted light and dark, which
    is also taken for folds: the height the normal map is made of."""
    materials = recipe['materials']
    cls, names = classify(ref, materials)
    kinds = [dict(KINDS[materials[n]['kind']], **{k: v for k, v in materials[n].items()
                                                  if k not in ('kind', 'seeds')}) for n in names]
    atlas = fg.png_array(ref.base_color_png()).astype(np.float32)
    lum = (atlas[..., :3] / 255.0) @ np.array([0.299, 0.587, 0.114], np.float32)
    on = tid >= 0
    old = bake.at_texels(mesh.old_uv, mesh.tris, tid, bary)
    # only what the body's own triangles take of the game's texture counts
    used, _ = bake.rasterize(mesh.old_uv, mesh.tris, atlas.shape[1::-1], grow=0.75)
    used = used >= 0
    h, w = tid.shape
    up = 4
    big = (atlas.shape[1] * up, atlas.shape[0] * up)
    member = np.zeros((h, w, len(names)), np.float32)
    shade = np.zeros((h, w, len(names)), np.float32)
    base = np.zeros((len(names), 3), np.float32)
    for k in range(len(names)):
        m = (cls == k) & used
        if not m.any():
            continue
        member[..., k] = bake.sample(bake.resize(bake.blur(m.astype(np.float32), 0.5), big), old)
        # the material's own light and dark, nothing of its neighbours'
        own = bake.blur_in(lum, m, 0.9)
        lit = np.quantile(lum[m], 0.7)
        base[k] = atlas[..., :3][m & (lum >= np.quantile(lum[m], 0.55)) & (lum <= np.quantile(lum[m], 0.85))].mean(0) \
            if 'color' not in kinds[k] else hex_color(kinds[k]['color'])
        shade[..., k] = bake.sample(bake.resize(own / max(lit, 0.02), big), old)
    member = bake.blur(member, 0.8)
    member[~on] = 0
    which = member.argmax(-1)
    # soft edges between materials: a texel or so of both
    sharp = np.clip((member - member.max(-1, keepdims=True)) * 3.0 + 1.0, 0, 1) * (member > 0)
    sharp /= np.maximum(sharp.sum(-1, keepdims=True), 1e-6)
    P = bake.at_texels(mesh.pos, mesh.tris, tid, bary)
    par = lambda key: np.array([kd[key] for kd in kinds], np.float32)  # noqa: E731
    sh = np.take_along_axis(shade, which[..., None], -1)[..., 0]
    sh = np.clip(bake.blur_in(sh, on, 1.0), 0.35, 1.3)
    # the height: the painted folds, a cord's roundness, the grain
    fold = par('fold')[which] * (sh - 1.0)
    height = bake.blur_in(fold, on, 1.5)
    cord = par('cord')[which]
    if (cord > 0).any():
        proud = (cord > 0) & on
        inside = bake.distance(~proud, most=int(8 * scale) + 2) / scale        # units from the cord's edge
        half = 5.0
        t = np.clip(inside / half, 0, 1)
        height = height + np.where(proud, cord * np.sqrt(1 - (1 - t) ** 2), 0)
        # a stitched line beside it, in the cloth
        beside = bake.distance(proud, most=int(8 * scale) + 2) / scale
        height = height - np.where(~proud & on, 0.5 * np.exp(-((beside - 4.0) / 1.2) ** 2), 0)
    # the folds of a worn garment, where the material is one that folds
    worn = par('wrinkle')[which]
    if (worn > 0).any():
        sel = on & (worn > 0)
        shares = bake.at_texels(bone_shares(mesh), mesh.tris, tid, bary)
        fold_w = np.zeros((h, w), np.float32)
        fold_w[sel] = wrinkles(P[sel], shares[sel], ref)
        height = height + bake.blur_in(fold_w * worn, on, 1.0)
    grain = np.zeros((h, w), np.float32)
    for k in range(len(names)):
        m = (which == k) & on
        if m.any() and kinds[k]['grain'] > 0:
            g = np.zeros((h, w), np.float32)
            g[m] = bake.fbm3(P[m], kinds[k]['grain_units'], 2, seed=k)
            grain += g * kinds[k]['grain']
    height = height + grain
    # the colour: each material's own, a share of the painted light and
    # dark, the hollows of the height a little darker
    hollow = height - bake.blur_in(height, on, 6.0)
    occl = np.clip(1.0 + hollow * 0.035, 0.72, 1.06)
    # ... and the folds lit a little from above, as the sky lights them:
    # how much more or less the folded surface looks up than the smooth
    n_t = bake.normal_of_height(height, scale, on)
    T, _, _ = bake.frames(mesh.pos, mesh.nrm, mesh.uv, mesh.tris)
    N = bake.at_texels(mesh.nrm, mesh.tris, tid, bary)
    N /= np.maximum(np.linalg.norm(N, axis=-1, keepdims=True), 1e-6)
    Tt = T[np.maximum(tid, 0)].astype(np.float32)
    Tt -= N * (Tt * N).sum(-1, keepdims=True)
    Tt /= np.maximum(np.linalg.norm(Tt, axis=-1, keepdims=True), 1e-6)
    Bt = np.cross(N, Tt)
    up = recipe.get('sky', [0.0, -0.8, 0.6])           # the game's y is down; its front is +z at the bind pose
    up = np.array(up, np.float32) / np.linalg.norm(up)
    n3 = Tt * n_t[..., 0:1] + Bt * n_t[..., 1:2] + N * n_t[..., 2:3]
    sky = (n3 @ up) - (N @ up)
    occl = occl * np.clip(1.0 + 0.55 * sky, 0.6, 1.3)
    keep = (sharp * par('shade')[None, None]).sum(-1)
    tone = (1 - keep) + keep * sh
    color = (sharp[..., None] * base[None, None]).sum(2) * (tone * occl)[..., None]
    # a material with small things painted on it (a shoe's laces, a sole's
    # tread) keeps the game's painting of them, as the hd mod has it
    literal = (sharp * par('paint')[None, None]).sum(-1)
    if painted is not None and (literal > 0).any():
        color = color * (1 - literal[..., None]) + bake.sample(painted[..., :3].astype(np.float32), old) *             (literal * occl)[..., None]
    rough = (sharp * par('rough')[None, None]).sum(-1) + grain * 0.04
    metal = (sharp * par('metal')[None, None]).sum(-1)
    out = {'color': np.clip(color, 0, 255), 'height': height, 'rough': np.clip(rough, 0.05, 1), 'metal': metal,
           'occlusion': np.clip(0.9 + hollow * 0.05, 0.55, 1.0), 'material': which, 'on': on, 'old': old,
           'names': names}
    return out


def lighter(mesh, island, R, most):
    """The body at a lower level of detail, of `most` triangles at most:
    its vertices gathered by the cell of a grid they are in (the largest
    grid that leaves few enough), each cell's of one bone drawn to one
    place, and of each island there one kept, with all it carries (its
    sum of bones, its place in the texture): the layout and the bending
    are the full model's."""
    from meshes import shift
    share = np.abs(mesh.terms[:, :, 3])
    bone = mesh.tjoints[np.arange(len(mesh.pos)), share.argmax(1)]
    isle = np.zeros(len(mesh.pos), np.int64)
    isle[mesh.tris.ravel()] = np.repeat(island, 3)

    def at(size):
        cell = np.floor(mesh.pos / size).astype(np.int64)
        place_key = np.concatenate([cell, bone[:, None]], 1)
        _, place = np.unique(place_key, axis=0, return_inverse=True)
        place = place.ravel()
        count = np.bincount(place).astype(float)
        middle = np.stack([np.bincount(place, mesh.pos[:, k]) / count for k in range(3)], 1)
        _, group = np.unique(np.stack([place, isle], 1), axis=0, return_inverse=True)
        group = group.ravel()
        far = np.linalg.norm(mesh.pos - middle[place], axis=1)
        order = np.lexsort((far, group))
        first = np.ones(len(order), bool)
        first[1:] = group[order][1:] != group[order][:-1]
        keep = order[first]                                   # each group's vertex nearest its cell's middle
        tris = group[mesh.tris]
        whole = (tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 0] != tris[:, 2])
        tris = tris[whole]
        _, uniq = np.unique(np.sort(tris, 1), axis=0, return_index=True)
        return keep, tris[np.sort(uniq)], middle[place[keep]]
    lo, hi = 2.0, 400.0
    for _ in range(18):
        mid = (lo + hi) / 2
        if len(at(mid)[1]) > most:
            lo = mid
        else:
            hi = mid
    keep, tris, where = at(hi)
    out = Mesh(**{k: v[keep] for k, v in mesh.__dict__.items() if k != 'tris'})
    out.tris = tris
    shift(out, where - out.pos, R)
    renormal(out)
    return out, hi


def chart(ref, hd, recipe, unwrapped, rect):
    """The body with its new layout and its pictures, as a chart."""
    import charts as charts_
    body, said = body_mesh(ref, hd, recipe)
    mesh, island, turned = laid_out(body, cut(body), unwrapped)
    if charts_.LEVEL:
        most = recipe.get('body_triangles', [0, 4600, 1000])[charts_.LEVEL]
        light, size = lighter(mesh, island, ref.bind()[0], most)
        said.update(triangles=int(len(light.tris)), cell_units=size)
        return Chart('body', rect, light), said
    _, _, w, h = rect
    tid, bary = bake.rasterize(mesh.uv, mesh.tris, (w, h))
    scale = texel_scale(mesh, mesh.tris, (w, h))[1]
    p = paint(mesh, tid, bary, ref, recipe, scale, fg.png_array(hd.base_color_png()))
    c = Chart('body', rect, mesh)
    c.on = p['on']
    c.color = p['color']
    c.normal = bake.normal_of_height(p['height'], scale, p['on'])
    c.rough, c.metal, c.occlusion = p['rough'], p['metal'], p['occlusion']
    pal = ref.extras.get('rb_palette')
    if pal is not None:
        idx = bake.sample_nearest(fg.png_array(ref.images()[pal['image']]), p['old'])
        c.entry = idx[..., :2]
        c.entry_on = p['on'] & (idx[..., 3] > 0)
    said.update(islands=int(island.max() + 1), turned=turned, texels_per_unit=scale,
                materials={n: int(((p['material'] == k) & p['on']).sum()) for k, n in enumerate(p['names'])})
    return c, said


def cloth_chart(ref, hd, rect):
    """The cloths' part of the game's texture (the game moves a cloth's
    points itself and the port draws it through them, with the places in
    the texture the file's rb_cloth gives): the hd mod's picture of it,
    in a rectangle of the new texture, and the cloths with their places
    moved there. None where the model has no cloth."""
    cloths = ref.extras.get('rb_cloth')
    if not cloths:
        return None, None
    uv = np.array([x for c in cloths for f in c['uvs'] for x in f]).reshape(-1, 2)
    game = fg.png_array(ref.base_color_png())
    gh, gw = game.shape[:2]
    lo = np.floor(uv.min(0) * [gw, gh] - 0.5).astype(int)
    hi = np.ceil(uv.max(0) * [gw, gh] + 0.5).astype(int)
    big = fg.png_array(hd.base_color_png()).astype(np.float32)
    k = big.shape[1] // gw
    tile = big[lo[1] * k:hi[1] * k, lo[0] * k:hi[0] * k, :3]
    x, y, w, h = rect
    c = Chart('cloth', rect)
    c.color = bake.resize(tile, (w, h))
    c.on[...] = True
    c.rough[...] = 0.8
    pal = ref.extras.get('rb_palette')
    if pal is not None:
        idx = fg.png_array(ref.images()[pal['image']])[lo[1]:hi[1], lo[0]:hi[0]]
        ys = np.clip((np.arange(h) + 0.5) * idx.shape[0] / h, 0, idx.shape[0] - 1).astype(int)
        xs = np.clip((np.arange(w) + 0.5) * idx.shape[1] / w, 0, idx.shape[1] - 1).astype(int)
        c.entry = idx[ys][:, xs][..., :2]
        c.entry_on = idx[ys][:, xs][..., 3] > 0

    def moved(cloths, size):
        import copy
        out = copy.deepcopy(cloths)
        span = (hi - lo) / np.array([gw, gh], float)
        for cl in out:
            rows = []
            for f in cl['uvs']:
                q = (np.array(f).reshape(-1, 2) - lo / np.array([gw, gh], float)) / span
                rows.append(c.to_atlas(q, size).ravel().tolist())
            cl['uvs'] = rows
        return out
    return c, moved
