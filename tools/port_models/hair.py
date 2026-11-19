"""A fighter's hair as shaped masses (tools/port_models/README.md, "The
head"): a cap close under the game's own outline of the hair, and over it
locks, each a tapered strip arched across, springing from the crown,
lying on that outline and ending in a point past its edge (over the
forehead, the fringe), in layers so that no gap shows. The outline is the
game's, measured from the head's middle each way (head.shells); how many
locks, how long and how they sweep is the fighter's recipe (`hair`).

The locks share a few strips of one texture of strands, painted here.
"""
import numpy as np

import bake
from charts import Chart
import head as hd_

VARIANTS = 5


def hairline(outer):
    """For each column of the sphere's picture, the row where the hair,
    coming down from the top, ends."""
    rows = np.zeros(outer.shape[1], np.int64)
    for x in range(outer.shape[1]):
        col = outer[:, x]
        on = np.nonzero(col)[0]
        if len(on) == 0:
            rows[x] = 0
            continue
        y = on[0]
        while y + 1 < len(col) and (col[y + 1] or col[y + 1:y + 5].any()):
            y += 1
        rows[x] = y
    return rows


def lock(shell, skin, root, tip, width, lift, sweep, tuck, flick, rng, segments=12, across=4):
    """One lock: rows of points from its root to its tip (rays in degrees,
    (round, up)), `width` units wide at its widest, lying `lift` over the
    shell; `sweep` degrees its middle swings round; `tuck` how far its tip
    comes in to the skin under it; `flick` how far its tip stands out."""
    t = np.linspace(0, 1, segments + 1)
    lat = root[1] + (tip[1] - root[1]) * t
    dlon = ((tip[0] - root[0] + 180) % 360) - 180
    lon = root[0] + dlon * t ** 1.3 + sweep * np.sin(np.pi * t) ** 1.5
    d = hd_.ray(lon, lat)                                           # (n, 3)
    tangent = np.gradient(d, axis=0)
    tangent /= np.maximum(np.linalg.norm(tangent, axis=1, keepdims=True), 1e-9)
    side = np.cross(d, tangent)
    side /= np.maximum(np.linalg.norm(side, axis=1, keepdims=True), 1e-9)
    shape = np.minimum(1.0, 0.4 + 2.4 * t) * np.clip(1 - t ** 3, 0, 1) ** 0.85
    W = np.maximum(width * shape, 1.2)
    arch = 0.2 * W + 0.6
    w = np.linspace(-1, 1, across + 1)
    r0 = shell.at(d)
    P = np.zeros((segments + 1, across + 1, 3))
    for j, wj in enumerate(w):
        dj = d * r0[:, None] + side * (wj * W / 2)[:, None]
        dj /= np.linalg.norm(dj, axis=1, keepdims=True)
        r = shell.at(dj)
        if tuck > 0:
            rs = skin.at(dj)
            k = tuck * np.clip((t - 0.45) / 0.55, 0, 1) ** 1.5
            r = np.where(rs > 0, r * (1 - k) + np.minimum(r, rs + 7.0) * k, r)
        r = r + lift + arch * (1 - wj * wj) - 1.5 * wj * wj + flick * t ** 4 - 3.0 * (1 - t) ** 6
        P[:, j] = hd_.MIDDLE + dj * r[:, None]
    return P


def strands(variant, size, colors, rng):
    """One strip of the strands' texture (h, w): colour, height (units)."""
    w, h = size
    a = np.linspace(-1, 1, w, dtype=np.float32)[None, :].repeat(h, 0)
    t = np.linspace(0, 1, h, dtype=np.float32)[:, None].repeat(w, 1)
    p = np.stack([a * 7.0 + variant * 31.0, t * 1.4 + variant * 7.0, np.zeros_like(a)], -1)
    coarse = bake.noise3(p, 1.0, seed=11 + variant)
    p2 = np.stack([a * 26.0 + variant * 17.0 + coarse * 1.2, t * 2.2, np.zeros_like(a)], -1)
    fine = bake.noise3(p2, 1.0, seed=23 + variant)
    streak = 0.55 * coarse + 0.45 * fine                           # -1..1
    dark, mid, light = (np.array(colors[k], np.float32) for k in ('dark', 'mid', 'light'))
    # along the lock: dark at the root, a lighter band where the light
    # catches the crown of the curve, darker to the tip
    band = np.exp(-((t - 0.42) / 0.2) ** 2)
    tone = 0.2 + 0.3 * band + 0.24 * streak
    tone = tone * (1 - 0.5 * np.abs(a) ** 3) * np.clip(0.35 + t * 5.0, 0, 1) * (1 - 0.3 * t ** 3)
    tone = np.clip(tone, 0, 1)[..., None]
    lo = np.clip(tone * 2, 0, 1)
    hi = np.clip(tone * 2 - 1, 0, 1)
    color = dark * (1 - lo) + mid * lo
    color = color * (1 - hi) + light * hi
    height = streak * 0.9 + fine * 0.5
    return color, height


def charts(maps, f, rect, R, T, skull=None):
    rng = np.random.default_rng(f.get('seed', 7))
    r_hair, r_skin = maps['r_hair'], maps['r_skin']
    have = r_hair > 0
    outer = have & ((r_skin <= 0) | (r_hair > r_skin + 2.0))
    smooth = hd_.blur_round(r_hair, have, f.get('even', 3.0))
    smooth = hd_.spread_round(np.where(have, smooth, 0), have, 70)
    shell = hd_.Sphere(smooth)
    skin = hd_.Sphere(hd_.spread_round(r_skin, r_skin > 0, 12))
    line = hairline(outer)                                          # the row, for each column
    lon_of = lambda x: -179.5 + x  # noqa: E731
    lat_of = lambda y: 89.5 - y  # noqa: E731
    # over the face the hair ends on the forehead: the fringe
    front = np.array([r_skin[min(line[x] + 3, 179), x] > 0 and abs(lon_of(x)) < f.get('face_half', 40.0)
                      for x in range(360)])
    pieces, uvs, tris_all, norms = [], [], [], []
    count = 0
    x, y, w, h = rect
    strip = 1.0 / (VARIANTS + 1)

    def add(P, variant):
        nonlocal count
        rows, cols = P.shape[:2]
        u = (variant + 0.06 + 0.88 * np.linspace(0, 1, cols)) * strip
        v = 0.01 + 0.98 * np.linspace(0, 1, rows)
        uv = np.stack(np.meshgrid(u, v), -1)
        pts, nrm, uvf, tris = hd_.grid_mesh(P, uv, hd_.MIDDLE)
        pieces.append(pts)
        norms.append(nrm)
        uvs.append(uvf)
        tris_all.append(tris + count)
        count += len(pts)

    # the cap
    step = f.get('cap_step', 8.0)
    lon = np.arange(-180, 180.1, step)
    lat = np.arange(88, -80, -step)
    LON, LAT = np.meshgrid(lon, lat)
    D = hd_.ray(LON, LAT)
    P = hd_.MIDDLE + D * (shell.at(D) - f.get('cap_under', 4.0))[..., None]
    uv = np.stack(np.meshgrid((VARIANTS + 0.06 + 0.88 * np.linspace(0, 1, len(lon))) * strip,
                              0.01 + 0.98 * np.linspace(0, 1, len(lat))), -1)
    pts, nrm, uvf, tris = hd_.grid_mesh(P, uv, hd_.MIDDLE)
    grown = bake.distance(~outer, most=4) >= 2.5 if f.get('cap_inside', True) else outer
    yy = np.clip((89.5 - LAT).astype(int), 0, 179).ravel()
    xx = (np.round(LON + 179.5).astype(int) % 360).ravel()
    covered = grown[yy, xx]
    # over the face the cap stops at the forehead's top: the fringe hangs
    # from there, with nothing behind it but the brow
    covered &= ~(front[xx] & (LAT.ravel() < f.get('forehead', 16.0)))
    keep = covered[tris].all(1)
    pieces.append(pts)
    norms.append(nrm)
    uvs.append(uvf)
    tris_all.append(tris[keep])
    count += len(pts)
    # the cap's edge closed down to the skull: the mass of hair is a mass
    # from the front and from below, not a shell one sees into
    if skull is not None:
        edges = {}
        for t in tris[keep]:
            for k in range(3):
                a, b, c3 = int(t[k]), int(t[(k + 1) % 3]), int(t[(k + 2) % 3])
                key = (min(a, b), max(a, b))
                edges[key] = None if key in edges else (a, b, c3)
        wall_p, wall_n, wall_uv, wall_t = [], [], [], []
        Dflat = D.reshape(-1, 3)
        for e in edges.values():
            if e is None:
                continue
            a, b, c3 = e
            top = np.stack([pts[a], pts[b]])
            low = hd_.MIDDLE + Dflat[[a, b]] * (skull.at(Dflat[[a, b]]) - 1.0)[:, None]
            n = np.cross(top[1] - top[0], low[0] - top[0])
            n /= max(np.linalg.norm(n), 1e-9)
            if n @ (pts[c3] - top[0]) > 0:
                n = -n
            k = len(wall_p)
            wall_p += [top[0], top[1], low[0], low[1]]
            wall_n += [n] * 4
            wall_uv += [uvf[a], uvf[b], uvf[a], uvf[b]]
            wall_t += [[k, k + 2, k + 1], [k + 1, k + 2, k + 3]]
        if wall_p:
            pieces.append(np.array(wall_p))
            norms.append(np.array(wall_n))
            uvs.append(np.array(wall_uv))
            tris_all.append(np.array(wall_t) + count)
            count += len(wall_p)

    # the locks, a layer after a layer
    locks = 0
    for layer in f['layers']:
        n = layer['count']
        for i in range(n):
            lon_t = -180 + (i + layer.get('offset', 0.0) + rng.uniform(-0.25, 0.25)) * 360.0 / n
            xi = int(round(lon_t + 179.5)) % 360
            is_front = bool(front[xi])
            edge = lat_of(line[xi])
            if is_front:
                lo, hi = layer.get('fringe', f.get('fringe', [-6.0, 4.0]))
                # the fringe: its tips at a height over the face, lower at the sides
                part = abs(lon_t) / max(f.get('face_half', 40.0), 1.0)
                tip_lat = rng.uniform(lo, hi) - f.get('fringe_drop', 3.0) * part ** 2
                tuck = f.get('tuck', 0.6)
                sweep = np.sign(lon_t + 1e-6) * layer.get('part', f.get('part', 5.0)) * (1 - part)
                flick = 0.0
            else:
                tip_lat = edge - rng.uniform(*layer.get('past', f.get('past', [2.0, 11.0])))
                tuck = 0.0
                sweep = rng.uniform(-3, 3)
                flick = layer.get('flick', f.get('flick', 5.0))
            root = (lon_t + rng.uniform(-6, 6), layer.get('root', 80.0) + rng.uniform(-3, 3))
            width = layer['width'] * rng.uniform(0.8, 1.2) * (f.get('fringe_width', 0.6) if is_front else 1.0)
            P = lock(shell, skin, root, (lon_t, tip_lat), width, layer.get('lift', 0.0) + rng.uniform(0, 1.5),
                     sweep, tuck, flick, rng, layer.get('segments', 12), layer.get('across', 4))
            add(P, int(rng.integers(0, VARIANTS)))
            locks += 1
    pts = np.concatenate(pieces)
    mesh = hd_.rigid(pts, np.concatenate(norms), np.concatenate(uvs), np.concatenate(tris_all), hd_.HEAD, R, T)
    turned = hd_.right_handed(mesh)
    c = Chart('hair', rect, mesh)
    c.bones = [hd_.HEAD]
    sw = w // (VARIANTS + 1)
    colors = f['colors']
    height = np.zeros((h, w), np.float32)
    for k in range(VARIANTS + 1):
        col, hgt = strands(k, (sw, h), colors, rng)
        if k == VARIANTS:
            col = col * 0.55                                         # the cap is in the locks' shade
        c.color[:, k * sw:(k + 1) * sw] = col
        height[:, k * sw:(k + 1) * sw] = hgt
    if turned:
        c.color, height = c.color[:, ::-1].copy(), height[:, ::-1].copy()
    c.on[...] = True
    c.normal = bake.normal_of_height(height, f.get('texels_per_unit', 2.5))
    c.rough[...] = f.get('rough', 0.6)
    c.occlusion[...] = 0.9
    return [c], {'locks': locks, 'triangles': int(len(mesh.tris))}
