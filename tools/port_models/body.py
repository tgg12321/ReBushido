"""A remade fighter's surface: the game's own mesh with more polygons
(each triangle made four, flat as it was, then the facets' edges
rounded), every vertex carrying the game's own sum of bones; in a
texture layout of its own (cut where the game's layout is cut, laid flat
in Blender, every island at one scale and none over another); and its
pictures: the game's own texture cleaned and made sharp (bake.Clean),
read into that layout, with what each texel is made of (the recipe's
materials) said beside it for the lighting
(tools/port_models/README.md, "What a remade fighter is made of").
"""
import json
import os

import numpy as np

import bake
import fighter_glb as fg
from check import dominant
from charts import Chart
from meshes import Mesh, pull_in, renormal, smoother


LEVELS = 2          # how many times each triangle of the game's is made four


def body_mesh(ref, recipe):
    """The game's own mesh with more polygons: each triangle made four
    (twice) as a smooth surface through the game's shape (meshes.smoother:
    every vertex a mean of the game's vertices round it, and its sum of
    bones the same mean of theirs, so that it bends as the game's does):
    the game's forms, their facets gone. Nothing is fitted or swollen: a
    smooth surface lies inside its coarse one. The triangles stay in the
    game's order, sixteen for each of its own."""
    R, T = ref.bind()
    body = Mesh.of_model(ref)
    for level in range(LEVELS):
        body = smoother(body, R, T, recipe.get('smooth', 1.0), small=recipe.get('small_piece', 24) * 4 ** level, most=recipe.get('most_units', 12.0) / 2 ** level)
    moved, worst = pull_in(body, ref, R, recipe['pull_in_units'], recipe.get('inside_units', 8.0))
    # a small triangle that smoothing turned over (its corners drawn past
    # one another) goes back to where the game's flat face has it, its
    # neighbours with it, until none is turned
    flat = Mesh.of_model(ref)
    for level in range(LEVELS):
        flat = smoother(flat, R, T, 0.0)

    def face(m):
        p0, p1, p2 = (m.pos[m.tris[:, k]] for k in range(3))
        n = np.cross(p1 - p0, p2 - p0)
        return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    was = face(flat)
    back = 0
    for _ in range(6):
        bad = ((face(body) * was).sum(1) < 0.0) & (np.linalg.norm(was, axis=1) > 0.5)
        if not bad.any():
            break
        v = np.unique(body.tris[bad])
        # (every vertex at those places: the two sides of a seam of the texture go together)
        key = {tuple(k) for k in np.round(flat.pos[v] * 8).astype(np.int64)}
        v = np.array([i for i, k in enumerate(np.round(flat.pos * 8).astype(np.int64)) if tuple(k) in key])
        body.pos[v], body.terms[v], body.tjoints[v] = flat.pos[v], flat.terms[v], flat.tjoints[v]
        back += len(v)
    renormal(body)
    return body, {'pulled_in': moved, 'swelled': worst, 'put_back': back}


def groups(ref, recipe):
    """Which chart each of the game's triangles is in: the recipe's
    `charts` names the bones of each (a triangle any of whose corners is
    on one of them); the rest are the body's."""
    dom = dominant(ref)
    out = np.full(len(ref.tris), 'body', object)
    for name, bones in recipe.get('charts', {}).items():
        out[np.isin(dom[ref.tris], bones).any(1)] = name
    # a chart kept as the game painted it (the face): the triangles of
    # another chart whose texture is mostly one material
    for name, spec in recipe.get('kept', {}).items():
        share = of_material(Mesh.of_model(ref), ref, recipe, spec['material'])
        out[(out == spec['chart']) & (share >= 0.3)] = name
    return out


def kept_layout(part, ref, rect):
    """The layout of a chart kept as the game painted it: the game's own
    layout of it, a whole number of times larger, so that each of the
    game's texels is a square block of the new texture's and nothing is
    filtered or smoothed. The parts of the game's texture its triangles
    use are boxes, put side by side in the chart's rectangle at the
    largest whole scale that fits. Returns each triangle's corners'
    places and the scale."""
    gh, gw = fg.png_array(ref.base_color_png()).shape[:2]
    size = np.array([gw, gh], float)
    per = 4 ** LEVELS
    uv = part.uv[part.tris] * size                          # (n, 3, 2) in the game's texels
    n0 = len(part.tris) // per
    lo = np.floor(uv.reshape(n0, per * 3, 2).min(1) - 1e-6).astype(int) - 1
    hi = np.ceil(uv.reshape(n0, per * 3, 2).max(1) + 1e-6).astype(int) + 1
    # the boxes: the game's triangles whose parts of the texture meet
    box = list(range(n0))

    def find(i):
        while box[i] != i:
            box[i] = box[box[i]]
            i = box[i]
        return i
    changed = True
    while changed:
        changed = False
        roots = sorted({find(i) for i in range(n0)})
        for x in range(len(roots)):
            for y in range(x + 1, len(roots)):
                a, b = find(roots[x]), find(roots[y])
                if a != b and (lo[a] < hi[b]).all() and (lo[b] < hi[a]).all():
                    box[b] = a
                    lo[a], hi[a] = np.minimum(lo[a], lo[b]), np.maximum(hi[a], hi[b])
                    changed = True
    roots = sorted({find(i) for i in range(n0)}, key=lambda r: -(hi[r][1] - lo[r][1]))
    _, _, w, h = rect
    for k in range(16, 0, -1):
        at, x, y, shelf = {}, 0, 0, 0
        for r in roots:
            bw, bh = (hi[r] - lo[r]) * k
            if x + bw > w:
                x, y, shelf = 0, y + shelf, 0
            at[r] = (x, y)
            x += bw
            shelf = max(shelf, bh)
        if y + shelf <= h and all(p[0] + (hi[r][0] - lo[r][0]) * k <= w for r, p in at.items()):
            break
    out = np.zeros_like(uv)
    for g in range(n0):
        r = find(g)
        rows = slice(g * per, (g + 1) * per)
        out[rows] = ((uv[rows] - lo[r]) * k + np.array(at[r], float)) / np.array([w, h], float)
    return out, k


def group_mesh(body, of_triangle, name):
    """The triangles of one chart (sixteen for each of the game's), as a
    mesh of its own."""
    mask = np.repeat(of_triangle == name, 4 ** LEVELS)
    return body.keep(mask)[0]


def coarser(mesh, corner_uv):
    """A mesh one level of detail lower: each four triangles the one they
    were made of, its corners theirs (their places in the layout too)."""
    n = len(mesh.tris) // 4
    t = mesh.tris.reshape(n, 4, 3)
    tris = np.stack([t[:, 0, 0], t[:, 1, 1], t[:, 2, 2]], 1)
    c = corner_uv.reshape(n, 4, 3, 2)
    uv = np.stack([c[:, 0, 0], c[:, 1, 1], c[:, 2, 2]], 1)
    out = Mesh(**{k: v for k, v in mesh.__dict__.items() if k != 'tris'})
    out.tris = tris
    return out, uv


def split(body, uvs):
    """The mesh with a vertex for each (vertex, place in the layout):
    `uvs` is each triangle's corners' places. The game's layout's place
    is kept beside (`old_uv`)."""
    corner_v = body.tris.ravel()
    corner_uv = np.round(uvs.reshape(-1, 2) * 65536).astype(np.int64)
    keys = np.concatenate([corner_v[:, None], corner_uv], 1)
    _, first, inverse = np.unique(keys, axis=0, return_index=True, return_inverse=True)
    src = corner_v[first]
    out = Mesh(**{k: v[src] for k, v in body.__dict__.items() if k not in ('tris', 'old_uv')})
    out.old_uv = body.uv[src]
    out.uv = uvs.reshape(-1, 2)[first]
    out.tris = inverse.ravel().reshape(-1, 3)
    return out


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


def write_unwrap(body, path, aspect, margin=0.004, larger=None):
    """What Blender lays flat. `larger` is, for each triangle, how many
    times larger than the rest its island is to be in the layout (a
    face's, which is looked at)."""
    c = cut(body)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump({'points': c['points'].round(3).tolist(), 'faces': c['faces'][c['whole']].tolist(),
                   'seams': c['seams'], 'aspect': aspect, 'margin': margin,
                   'larger': None if larger is None else np.asarray(larger)[c['whole']].round(3).tolist()}, f)
    return c


def of_material(mesh, ref, recipe, name):
    """How much of each triangle's texture is the material `name`."""
    cls, names = classify(ref, recipe['materials'])
    if name not in names:
        return np.zeros(len(mesh.tris))
    k = names.index(name)
    rng = np.random.default_rng(4)
    share = np.zeros(len(mesh.tris))
    n = 8
    for _ in range(n):
        a, b = rng.random(2)
        if a + b > 1:
            a, b = 1 - a, 1 - b
        uv = mesh.uv[mesh.tris[:, 0]] + a * (mesh.uv[mesh.tris[:, 1]] - mesh.uv[mesh.tris[:, 0]]) + \
            b * (mesh.uv[mesh.tris[:, 2]] - mesh.uv[mesh.tris[:, 0]])
        share += (bake.sample_nearest(cls, uv) == k) / n
    return share


def laid_out(body, c, unwrapped):
    """The mesh with the new layout: a vertex for each of its places in
    the layout (its place in the game's layout kept beside, `old_uv`),
    each triangle's island, and each triangle's corners' places. An island
    that came out the mirror of what a normal map wants is turned over
    where it lies."""
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
    return split(body, uvs), island, turned, uvs


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

# what a material is like: how rough, how much a metal, how tall its
# grain is and how wide a lump of it (units), and whether it stands proud
# of what is round it as a cord does (units). Nothing is folded: the
# game's surfaces are plain, and so are these (the owner, 2026-11-18)
KINDS = {
    'cloth': {'rough': 0.86, 'metal': 0.0, 'grain': 0.2, 'grain_units': 3.0, 'cord': 0.0},
    'lining': {'rough': 0.6, 'metal': 0.0, 'grain': 0.1, 'grain_units': 3.0, 'cord': 0.0},
    'linen': {'rough': 0.8, 'metal': 0.0, 'grain': 0.15, 'grain_units': 2.5, 'cord': 0.0},
    'braid': {'rough': 0.4, 'metal': 0.55, 'grain': 0.25, 'grain_units': 5.0, 'cord': 1.2},
    'leather': {'rough': 0.5, 'metal': 0.0, 'grain': 0.15, 'grain_units': 4.0, 'cord': 0.0},
    'rubber': {'rough': 0.7, 'metal': 0.0, 'grain': 0.15, 'grain_units': 4.0, 'cord': 0.0},
    'steel': {'rough': 0.3, 'metal': 0.9, 'grain': 0.05, 'grain_units': 4.0, 'cord': 0.0},
    'skin': {'rough': 0.7, 'metal': 0.0, 'grain': 0.0, 'grain_units': 2.0, 'cord': 0.0},
    'hair': {'rough': 0.8, 'metal': 0.0, 'grain': 0.0, 'grain_units': 1.5, 'cord': 0.0},
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


def paint(mesh, tid, bary, ref, recipe, scale, clean, only=None):
    """A chart's pictures in its new layout: the colour (the game's own
    texture, cleaned, read where each texel lay in the game's layout) and
    whether it is drawn, the height of its relief (a cord's roundness and
    a grain, units), how rough and how much a metal each texel is by the
    material it is of (`only`: the materials this chart has: a head has
    no leather, whatever its lashes' black is nearest)."""
    materials = recipe['materials']
    if only:
        materials = {k: v for k, v in materials.items() if k in only}
    cls, names = classify(ref, materials)
    kinds = [dict(KINDS[materials[n]['kind']], **{k: v for k, v in materials[n].items()
                                                  if k not in ('kind', 'seeds')}) for n in names]
    on = tid >= 0
    old = bake.at_texels(mesh.old_uv, mesh.tris, tid, bary)
    color, drawn = clean.at(old)
    h, w = tid.shape
    # each texel's material: the game's texel's, its edges cleaned as the colour's are
    large = bake.grown_twice(bake.grown_twice(cls.astype(np.int16)))
    which = bake.sample_nearest(large, old).astype(np.int64)
    par = lambda key: np.array([kd[key] for kd in kinds], np.float32)  # noqa: E731
    P = bake.at_texels(mesh.pos, mesh.tris, tid, bary)
    height = np.zeros((h, w), np.float32)
    cord = par('cord')[which]
    if (cord > 0).any():
        proud = (cord > 0) & on
        inside = bake.distance(~proud, most=int(8 * scale) + 2) / scale        # units from the cord's edge
        t = np.clip(inside / 4.0, 0, 1)
        height += np.where(proud, cord * np.sqrt(1 - (1 - t) ** 2), 0)
    grain = np.zeros((h, w), np.float32)
    for k in range(len(names)):
        m = (which == k) & on
        if m.any() and kinds[k]['grain'] > 0:
            g = np.zeros((h, w), np.float32)
            g[m] = bake.fbm3(P[m], kinds[k]['grain_units'], 2, seed=k)
            grain += g * kinds[k]['grain']
    height += grain
    return {'color': color, 'drawn': drawn, 'height': height, 'rough': np.clip(par('rough')[which] + grain * 0.04, 0.05, 1),
            'metal': par('metal')[which], 'material': which, 'on': on, 'old': old, 'names': names}


_made = {}


def made(ref, recipe):
    """The whole mesh, each of the game's triangles' chart, and the
    texture cleaned: made once for all the charts and levels."""
    key = id(ref)
    if key not in _made:
        body, said = body_mesh(ref, recipe)
        used, _ = bake.rasterize(ref.uv, ref.tris, fg.png_array(ref.base_color_png()).shape[1::-1], grow=0.75)
        within, _ = bake.rasterize(ref.uv, ref.tris, fg.png_array(ref.base_color_png()).shape[1::-1], grow=0.0)
        clean = bake.Clean(fg.png_array(ref.base_color_png()), recipe.get('near', 30.0), used=used >= 0,
                           within=within >= 0)
        _made[key] = (body, groups(ref, recipe), clean, said)
    return _made[key]


def chart(ref, recipe, name, unwrapped, rect):
    """One chart of the model (`name` of the recipe's `charts`, or
    `body`): its mesh in its new layout and its pictures; at a lower
    level of detail its mesh alone, of the triangles the full one's were
    made of."""
    import charts as charts_
    body, of_triangle, clean, said = made(ref, recipe)
    said = dict(said)
    part = group_mesh(body, of_triangle, name)
    kept = name in recipe.get('kept', {})
    if kept:
        corner_uv, times = kept_layout(part, ref, rect)
        mesh = split(part, corner_uv)
    else:
        mesh, island, turned, corner_uv = laid_out(part, cut(part), unwrapped)
    if charts_.LEVEL:
        low = part
        for _ in range(charts_.LEVEL):
            low, corner_uv = coarser(low, corner_uv)
        low = split(low, corner_uv)
        used = np.unique(low.tris)
        index = -np.ones(len(low.pos), np.int64)
        index[used] = np.arange(len(used))
        out = Mesh(**{k: v[used] for k, v in low.__dict__.items() if k != 'tris'})
        out.tris = index[low.tris]
        renormal(out)
        return Chart(name, rect, out), {'triangles': int(len(out.tris))}
    _, _, w, h = rect
    tid, bary = bake.rasterize(mesh.uv, mesh.tris, (w, h))
    c = Chart(name, rect, mesh)
    if kept:
        # the game's own texels, each a block: nothing cleaned, nothing smoothed
        c.on = tid >= 0
        old = bake.at_texels(mesh.old_uv, mesh.tris, tid, bary)
        texel = bake.sample_nearest(fg.png_array(ref.base_color_png()), old)
        c.color = texel[..., :3].astype(np.float32)
        c.alpha = (texel[..., 3] >= 128).astype(np.float32)
        c.spread = 1
        c.rough[...] = KINDS['skin']['rough']
        pal = ref.extras.get('rb_palette')
        if pal is not None:
            idx = bake.sample_nearest(fg.png_array(ref.images()[pal['image']]), old)
            c.entry = idx[..., :2]
            c.entry_on = c.on & (idx[..., 3] > 0)
        said.update(triangles=int(len(mesh.tris)), times_the_games_texels=int(times))
        return c, said
    scale = texel_scale(mesh, mesh.tris, (w, h))[1]
    p = paint(mesh, tid, bary, ref, recipe, scale, clean, recipe.get('chart_materials', {}).get(name))
    c.on = p['on']
    c.alpha = p['drawn']
    c.color = p['color']
    c.normal = bake.normal_of_height(p['height'], scale, p['on'])
    c.rough, c.metal = p['rough'], p['metal']
    pal = ref.extras.get('rb_palette')
    if pal is not None:
        idx = bake.sample_nearest(fg.png_array(ref.images()[pal['image']]), p['old'])
        c.entry = idx[..., :2]
        c.entry_on = p['on'] & (idx[..., 3] > 0)
    said.update(triangles=int(len(mesh.tris)), islands=int(island.max() + 1), turned=turned, texels_per_unit=scale,
                materials={n: int(((p['material'] == k) & p['on']).sum()) for k, n in enumerate(p['names'])})
    return c, said


def cloth_chart(ref, recipe, rect):
    """The cloths' part of the game's texture (the game moves a cloth's
    points itself and the port draws it through them, with the places in
    the texture the file's rb_cloth gives): the game's picture of it,
    cleaned, in a rectangle of the new texture, and the cloths with their places
    moved there. None where the model has no cloth."""
    cloths = ref.extras.get('rb_cloth')
    if not cloths:
        return None, None
    uv = np.array([x for c in cloths for f in c['uvs'] for x in f]).reshape(-1, 2)
    game = fg.png_array(ref.base_color_png())
    gh, gw = game.shape[:2]
    lo = np.floor(uv.min(0) * [gw, gh] - 0.5).astype(int)
    hi = np.ceil(uv.max(0) * [gw, gh] + 0.5).astype(int)
    x, y, w, h = rect
    c = Chart('cloth', rect)
    clean = made(ref, recipe)[2]
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    at = np.stack([(lo[0] + (xs + 0.5) / w * (hi[0] - lo[0])) / gw, (lo[1] + (ys + 0.5) / h * (hi[1] - lo[1])) / gh], -1)
    c.color, _ = clean.at(at)
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
