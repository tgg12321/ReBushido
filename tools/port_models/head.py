"""A fighter's head made whole (tools/port_models/README.md, "The head"):

- the face and the skull: one smooth surface about the head's middle,
  its shape the game's own head (the hd mod's skin, seen from the middle
  and evened), with the eyes' sockets, the brow, the nose, the lips and
  the chin modelled on it where the fighter's recipe says his features
  are, and its texture painted anew (facepaint.py);
- the ears and the neck (the neck stretched between the head and the
  chest as the game's own is);
- the hair: a cap under locks, each lock a tapered strip that springs
  from the crown, lies on the game's own outline of the hair and ends in
  a point at its edge, so that the outline is the game's and the mass is
  hair.

Everything is worked in the head bone's own frame, as (front, side, up):
the bone's x, its y, and its z turned over. Needs numpy and Pillow.
"""
import json
import os

import numpy as np

import bake
import facepaint
import fighter_glb as fg
import charts as charts_
from charts import Chart
from check import dominant
from meshes import K, Mesh

HEAD, CHEST = 3, 2
MIDDLE = np.array([30.0, 0.0, 70.0])          # the middle the head is seen from, (front, side, up)


def fsu(local):
    return np.stack([local[..., 0], local[..., 1], -local[..., 2]], -1)


def local_of(p):
    return np.stack([p[..., 0], p[..., 1], -p[..., 2]], -1)


# ---- the game's head, seen from its middle ----

def head_triangles(m):
    """The head's own triangles, its points in (front, side, up), and how
    much of each triangle's texture is skin."""
    d = dominant(m)
    tris = m.tris[np.all(d[m.tris] == HEAD, 1)]
    p = (np.concatenate([m.pos, np.ones((len(m.pos), 1))], 1) @ m.ibm[HEAD].T)[:, :3]
    atlas = fg.png_array(m.base_color_png()).astype(np.float32)
    rng = np.random.default_rng(3)
    share = np.zeros(len(tris))
    n = 12
    for _ in range(n):
        a, b = rng.random(2)
        if a + b > 1:
            a, b = 1 - a, 1 - b
        uv = m.uv[tris[:, 0]] + a * (m.uv[tris[:, 1]] - m.uv[tris[:, 0]]) + b * (m.uv[tris[:, 2]] - m.uv[tris[:, 0]])
        c = bake.sample_nearest(atlas, uv)
        share += ((c[:, 0] > 150) & (c[:, 0] > c[:, 2] + 40) & (c[:, 3] > 128)) / n
    return fsu(p), tris, share


def cast(P, tris, D, origin):
    """The farthest place each ray from `origin` along D meets the
    triangles (0 for none)."""
    if len(tris) == 0:
        return np.zeros(len(D))
    a, b, c = (P[tris[:, k]] for k in range(3))
    e1, e2 = b - a, c - a
    out = np.zeros(len(D))
    sv = (origin - a)[None]
    q = np.cross(sv, e1[None])
    for s in range(0, len(D), 2048):
        d = D[s:s + 2048, None, :]
        h = np.cross(d, e2[None])
        det = (e1[None] * h).sum(-1)
        ok = np.abs(det) > 1e-9
        inv = 1.0 / np.where(ok, det, 1)
        u = (sv * h).sum(-1) * inv
        v = (d * q).sum(-1) * inv
        t = (e2[None] * q).sum(-1) * inv
        hit = ok & (u >= -1e-4) & (v >= -1e-4) & (u + v <= 1 + 1e-4) & (t > 0)
        out[s:s + 2048] = np.where(hit, t, 0).max(1)
    return out


def directions():
    """A ray for each degree of the sphere: rows from the top down,
    columns round from behind (-180) by the fighter's right... to behind."""
    lat = np.radians(np.arange(89.5, -90, -1.0))
    lon = np.radians(np.arange(-179.5, 180, 1.0))
    LON, LAT = np.meshgrid(lon, lat)
    return np.stack([np.cos(LAT) * np.cos(LON), np.cos(LAT) * np.sin(LON), np.sin(LAT)], -1)


def shells(ref, hd):
    """How far from the head's middle the game's skin and hair are, each
    way, of the game's own mesh (its outline is what the check holds the
    new head to; the hd mod's is swollen)."""
    D = directions()
    P, tris, share = head_triangles(ref)
    centre = P[tris].mean(1)
    ear = (np.abs(centre[:, 1]) > 58) & (centre[:, 0] < 70)
    r_skin = cast(P, tris[(share >= 0.3) & ~ear], D.reshape(-1, 3), MIDDLE).reshape(D.shape[:2])
    r_hair = cast(P, tris[share < 0.3], D.reshape(-1, 3), MIDDLE).reshape(D.shape[:2])
    return {'r_skin': r_skin.astype(np.float32), 'r_hair': r_hair.astype(np.float32)}


def blur_round(a, mask, sigma):
    """A blur over the sphere's picture that goes round it (the columns
    wrap) and takes in only `mask`."""
    pad = int(sigma * 4) + 2
    wide = np.concatenate([a[:, -pad:], a, a[:, :pad]], 1)
    wide_m = np.concatenate([mask[:, -pad:], mask, mask[:, :pad]], 1)
    return bake.blur_in(wide, wide_m, sigma)[:, pad:-pad]


def spread_round(a, mask, passes):
    pad = passes + 2
    wide = np.concatenate([a[:, -pad:], a, a[:, :pad]], 1)
    wide_m = np.concatenate([mask[:, -pad:], mask, mask[:, :pad]], 1)
    return bake.spread(wide, wide_m, passes)[:, pad:-pad]


class Sphere:
    """A picture of the sphere (a row a degree from the top, a column a
    degree round), read between its texels at any ray."""

    def __init__(self, picture):
        self.p = picture

    def at(self, d):
        d = d / np.maximum(np.linalg.norm(d, axis=-1, keepdims=True), 1e-9)
        lat = np.degrees(np.arcsin(np.clip(d[..., 2], -1, 1)))
        lon = np.degrees(np.arctan2(d[..., 1], d[..., 0]))
        y = np.clip(89.5 - lat, 0, 179)
        x = (lon + 179.5) % 360.0
        y0 = np.clip(np.floor(y).astype(np.int64), 0, 178)
        x0 = np.floor(x).astype(np.int64) % 360
        x1 = (x0 + 1) % 360
        fy = y - y0
        fx = x - np.floor(x)
        p = self.p
        return (p[y0, x0] * (1 - fx) + p[y0, x1] * fx) * (1 - fy) + (p[y0 + 1, x0] * (1 - fx) + p[y0 + 1, x1] * fx) * fy


def ray(lon, lat):
    lon, lat = np.radians(lon), np.radians(lat)
    return np.stack([np.cos(lat) * np.cos(lon), np.cos(lat) * np.sin(lon), np.sin(lat)], -1)


# ---- surfaces of rows and columns ----

def grid_mesh(P, uv, out_from, wrap=False):
    """A surface of rows and columns of points as vertices, normals
    (pointing away from `out_from`) and triangles."""
    rows, cols = P.shape[:2]
    du = np.gradient(P, axis=1)
    if wrap:
        du = (np.roll(P, -1, 1) - np.roll(P, 1, 1)) / 2
    dv = np.gradient(P, axis=0)
    n = np.cross(du, dv)
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-9)
    if ((n * (P - out_from)).sum(-1) < 0).mean() > 0.5:
        n = -n
    idx = np.arange(rows * cols).reshape(rows, cols)
    a, b, c, d = idx[:-1, :-1], idx[:-1, 1:], idx[1:, :-1], idx[1:, 1:]
    tris = np.concatenate([np.stack([a, c, b], -1).reshape(-1, 3), np.stack([b, c, d], -1).reshape(-1, 3)])
    return P.reshape(-1, 3), n.reshape(-1, 3), uv.reshape(-1, 2), tris


def rigid(points, normals, uv, tris, bone, R, T):
    """A piece wholly on one bone, from its points in (front, side, up)
    of that bone's frame."""
    p = local_of(points)
    n = local_of(normals)
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
    return Mesh(pos=p @ R[bone].T + T[bone], nrm=n @ R[bone].T, uv=np.asarray(uv, float), joints=joints,
                weights=weights, terms=terms, tjoints=tj, tris=np.asarray(tris, np.int64))


def right_handed(mesh):
    """The layout turned over (u for 1 - u) if it is the mirror of what a
    normal map wants (bake.frames)."""
    _, _, hand = bake.frames(mesh.pos, mesh.nrm, mesh.uv, mesh.tris)
    if hand.sum() < 0:
        mesh.uv = mesh.uv * np.array([-1.0, 1.0]) + np.array([1.0, 0.0])
        return True
    return False


def texels(mesh, rect):
    """Each texel of the chart: on which triangle, where on it, its place
    (front, side, up) in its bone's frame and its normal there."""
    _, _, w, h = rect
    tid, bary = bake.rasterize(mesh.uv, mesh.tris, (w, h), grow=1.5)
    local = fsu(mesh.terms[:, 0, :3])
    P = bake.at_texels(local, mesh.tris, tid, bary)
    return tid, bary, P


# ---- the face ----

def bump(S, U, at, radii, power=2.0):
    d = ((S - at[0]) / radii[0]) ** 2 + ((U - at[1]) / radii[1]) ** 2
    return np.exp(-d ** (power / 2.0))


def sculpt(S, U, f):
    """How far the face's surface is moved out (or in) from the game's
    evened head at (across, up): the features' own shapes, placed by the
    fighter's painted ones."""
    eye, nose, mouth = f['eye'], f['nose'], f['mouth']
    out = np.zeros_like(S)
    ic = np.array(eye['iris'])
    mid_eye = (np.array(eye['inner']) + np.array(eye['outer'])) / 2
    span = np.linalg.norm(np.array(eye['outer']) - np.array(eye['inner']))
    for side in (1, -1):
        # the socket the eye lies in, the brow's ridge over it, the cheekbone under it
        out -= 3.2 * bump(S, U, (side * mid_eye[0], mid_eye[1] + 1.0), (span * 0.62, 8.5), 3.0)
        out += 2.4 * bump(S, U, (side * (mid_eye[0] + 2.0), mid_eye[1] + 17.0), (span * 0.7, 6.5))
        out += 1.5 * bump(S, U, (side * (ic[0] + 6.0), ic[1] - 22.0), (17.0, 11.0))
    tip = np.array(nose['tip'])
    top = mid_eye[1] + 4.0
    # the nose: a ridge from between the eyes to its tip, growing; the tip;
    # the wings beside it; cut in under the tip
    t = np.clip((top - U) / max(top - tip[1], 1e-3), 0, 1)
    ridge = np.exp(-(S / (3.2 + 2.6 * t)) ** 2) * np.where((U <= top + 6) & (U >= tip[1] - 1.5), 1.0, 0.0)
    ridge *= np.clip((top + 6 - U) / 6.0, 0, 1) * np.clip((U - tip[1] + 1.5) / 3.0, 0, 1)
    out += ridge * (0.8 + 4.4 * t ** 1.6) * nose.get('height', 1.0)
    out += 2.6 * bump(S, U, (0, tip[1] + 1.5), (5.2, 4.2)) * nose.get('height', 1.0)
    for side in (1, -1):
        out += 1.5 * bump(S, U, (side * 6.2, tip[1] - 0.5), (3.4, 3.2))
    out -= 2.6 * bump(S, U, (0, tip[1] - 6.0), (9.0, 2.6))
    # the mouth: the lips either side of their line, the hollow under the
    # lower lip, the chin
    line = np.array(mouth['line'])
    mu = line[len(line) // 2][1]
    half = abs(line[-1][0] - line[0][0]) / 2
    out += 1.7 * bump(S, U, (0, mu + 2.6), (half * 0.72, 2.6))
    out += 1.9 * bump(S, U, (0, mu - 3.4), (half * 0.6, 3.0))
    out -= 1.3 * bump(S, U, (0, mu), (half * 1.05, 0.9))
    out -= 1.6 * bump(S, U, (0, mu - 9.5), (half * 0.6, 3.0))
    out += 2.2 * bump(S, U, (0, mu - 20.0), (11.0, 8.0))
    for side in (1, -1):
        out -= 0.9 * bump(S, U, (side * (half + 2.5), mu - 1.5), (3.0, 4.0))
    return out


def face_grid():
    """The face's rows and columns as rays from the head's middle: close
    together where the features are, far apart where the hair covers
    (and further apart at the lower levels of detail)."""
    k = (1.0, 2.0, 4.0)[charts_.LEVEL]
    lon = np.concatenate([np.arange(-102, -30, 4.0 * k), np.arange(-30, 30, 1.5 * k), np.arange(30, 102.1, 4.0 * k)])
    lat = np.concatenate([np.arange(84, 12, -12.0), np.arange(12, 4, -4.0), np.arange(4, -62, -1.6 * k),
                          np.arange(-62, -86.1, -6.0 * min(k, 2.0))])
    return lon, lat


def face_surface(maps, f, only_skull=False):
    r_skin, r_hair = maps['r_skin'], maps['r_hair']
    have = r_skin > 0
    # under the hair, a skull: an egg about the middle, never nearer the
    # hair's outline than a lock is thick
    D1 = directions()
    a, b, c = f.get('skull', [94.0, 70.0, 98.0])
    egg = 1.0 / np.sqrt((D1[..., 0] / a) ** 2 + (D1[..., 1] / b) ** 2 + (D1[..., 2] / c) ** 2)
    under = np.where(r_hair > 0, np.minimum(np.maximum(r_hair - 8.0, 40.0), egg), 0)
    base = np.where(have, r_skin, under)
    known = base > 0
    base = spread_round(base, known, 60)
    base = blur_round(base, np.ones_like(known), f.get('even', 3.0))
    sphere = Sphere(base)
    if only_skull:
        return sphere
    lon, lat = face_grid()
    LON, LAT = np.meshgrid(lon, lat)
    D = ray(LON, LAT)
    r = sphere.at(D)
    P = MIDDLE + D * r[..., None]
    front = np.clip((P[..., 0] - 70.0) / 25.0, 0, 1)             # only the front of the head is a face
    move = sculpt(P[..., 1], P[..., 2], f) * front
    P = MIDDLE + D * (r + move)[..., None]
    uv = np.stack(np.meshgrid(np.linspace(0.01, 0.99, len(lon)), np.linspace(0.01, 0.99, len(lat))), -1)
    # where the game's head has nothing at all (under the nape) there is
    # no skull either: the neck is there
    near = bake.distance(known, most=8) <= 5
    yy = np.clip((89.5 - LAT).astype(int), 0, 179)
    xx = np.round(LON + 179.5).astype(int) % 360
    return P, uv, near[yy, xx]


def skin_shade(P, N, f, hair_edge=None):
    """How lit the skin is painted (1 for fully): less underneath (the
    jaw, the nose's tip, the lower lip), and under the fringe."""
    up = np.array([0.25, 0.0, 0.97])
    t = (N @ up)
    shade = 0.9 + 0.1 * np.clip(t, -1, 1)
    shade = np.where(t < 0, shade + 0.22 * t, shade)
    if hair_edge is not None:
        shade = shade * (1.0 - 0.22 * np.clip((P[..., 2] - hair_edge + 14.0) / 16.0, 0, 1))
    return shade


def tint(color, shade, f):
    """Skin less lit goes towards its own shadow colour, not to grey."""
    skin = np.array(f.get('skin', [250, 174, 140]), np.float32)
    dark = np.array(f.get('skin_shade', [196, 98, 76]), np.float32)
    k = np.clip((1.0 - shade) * 1.6, 0, 1)[..., None]
    return color * (1 - k) + (color * dark / skin) * k


def face_chart(maps, f, rect, R, T):
    P, uv, there = face_surface(maps, f)
    pts, nrm, uvs, tris = grid_mesh(P, uv, MIDDLE)
    tris = tris[there.ravel()[tris].all(1)]
    mesh = rigid(pts, nrm, uvs, tris, HEAD, R, T)
    right_handed(mesh)
    c = Chart('face', rect, mesh)
    c.bones = [HEAD]
    if charts_.LEVEL:
        return c, {'triangles': len(tris)}
    tid, bary, at = texels(mesh, rect)
    N = bake.at_texels(nrm, mesh.tris, tid, bary)
    N /= np.maximum(np.linalg.norm(N, axis=-1, keepdims=True), 1e-6)
    canvas = facepaint.paint_face(f)
    rgb, alpha, height = canvas.at(at[..., 1], at[..., 2])
    facing = np.clip((N[..., 0] - 0.15) / 0.3, 0, 1) * (at[..., 0] > 60)       # paint only what faces the front
    alpha = alpha * facing
    skin = np.array(f.get('skin', [250, 174, 140]), np.float32)
    shade = skin_shade(at, N, f, f.get('fringe', 84.0))
    # the cheeks a little warmer
    warm = np.zeros(at.shape[:2], np.float32)
    ic = np.array(f['eye']['iris'])
    for side in (1, -1):
        warm += bump(at[..., 1], at[..., 2], (side * (ic[0] + 4), ic[1] - 24), (16, 9)) * facing
    base = skin[None, None] * (1 - 0.1 * warm[..., None]) + np.array([255, 120, 110], np.float32) * 0.1 * warm[..., None]
    color = tint(base, shade, f)
    color = color * (1 - alpha[..., None]) + rgb * alpha[..., None] * np.clip(shade + 0.08, 0, 1)[..., None]
    c.on = tid >= 0
    c.color = color
    c.normal = bake.normal_of_height_at(height * facing, at, c.on)
    c.rough[...] = 0.55
    c.rough[(alpha > 0.5) & (rgb.sum(-1) > 600)] = 0.25           # the eyes' whites are wet
    c.occlusion = np.clip(shade + 0.1, 0, 1)
    return c, {'triangles': len(tris)}


# ---- the neck ----

def neck_rungs(ref):
    """The game's neck: the triangles between the head and the chest, as
    pairs (a point of the head's rim, a point of the chest's) in order
    round it, each in its own bone's frame."""
    d = dominant(ref)
    key = {i: tuple(np.round(ref.pos[i] * 4).astype(int)) for i in range(len(ref.pos))}
    span = [t for t in ref.tris if (d[t] == HEAD).any() and (d[t] == CHEST).any() and
            np.isin(d[t], [HEAD, CHEST]).all()]
    rungs = {}
    for t in span:
        for k in range(3):
            a, b = t[k], t[(k + 1) % 3]
            if d[a] == CHEST:
                a, b = b, a
            if d[a] == HEAD and d[b] == CHEST:
                rungs[(key[a], key[b])] = (a, b)
    # in order round the neck: each next rung shares a point with the last
    todo = dict(rungs)
    first = next(iter(todo))
    order = [todo.pop(first)]
    last = first
    while todo:
        nxt = [k for k in todo if k[0] == last[0] or k[1] == last[1]]
        if not nxt:
            break
        last = nxt[0]
        order.append(todo.pop(last))

    def frame(i, bone):
        return fsu((ref.ibm[bone] @ np.append(ref.pos[i], 1.0))[:3])
    return np.array([frame(a, HEAD) for a, _ in order]), np.array([frame(b, CHEST) for _, b in order])


def ring(points, n, shrink=1.0, rounds=2):
    """A ring of `n` points evenly round the closed line through
    `points`, evened, and drawn in towards its middle by `shrink`."""
    p = np.asarray(points, float)
    for _ in range(rounds):
        p = (np.roll(p, 1, 0) + 2 * p + np.roll(p, -1, 0)) / 4
    closed = np.concatenate([p, p[:1]])
    seg = np.linalg.norm(np.diff(closed, axis=0), axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])
    t = np.linspace(0, s[-1], n, endpoint=False)
    out = np.stack([np.interp(t, s, closed[:, k]) for k in range(3)], 1)
    mid = out.mean(0)
    return mid + (out - mid) * shrink, s, t


def neck_chart(ref, f, rect, R, T, stance):
    """The neck: a tube from a ring inside the head to the game's ring on
    the chest, each of its points a share of both bones as the game's own
    neck's are, so that it stretches as the head turns."""
    top, bottom = neck_rungs(ref)
    n, rows = ((24, 6), (16, 4), (10, 3))[charts_.LEVEL]
    # both rings sampled at the same places round the neck (by the rungs)
    a, s, t = ring(top, n, f.get('neck_top', 0.8))
    idx = np.interp(t, s, np.arange(len(s)))

    def along(points):
        p = np.asarray(points, float)
        for _ in range(2):
            p = (np.roll(p, 1, 0) + 2 * p + np.roll(p, -1, 0)) / 4
        closed = np.concatenate([p, p[:1]])
        return np.stack([np.interp(idx, np.arange(len(closed)), closed[:, k]) for k in range(3)], 1)
    b = along(bottom)
    a[:, 2] += f.get('neck_up', 6.0)                    # the top ring a little up inside the head
    b = b.mean(0) + (b - b.mean(0)) * f.get('neck_bottom', 1.0)
    Rs, Ts = stance if stance is not None else (R, T)
    wa = local_of(a) @ Rs[HEAD].T + Ts[HEAD]             # in the world, standing
    wb = local_of(b) @ Rs[CHEST].T + Ts[CHEST]
    k = np.linspace(0, 1, rows)
    world = wa[None] * (1 - k[:, None, None]) + wb[None] * k[:, None, None]
    world = np.concatenate([world, world[:, :1]], 1)                       # round, the seam's column twice
    uv = np.stack(np.meshgrid(np.linspace(0.02, 0.98, n + 1), np.linspace(0.04, 0.96, rows)), -1)
    pts, nrm, uvs, tris = grid_mesh(world, uv, world.reshape(-1, 3).mean(0), wrap=False)
    # the seam's two columns' normals agree
    nr = nrm.reshape(rows, n + 1, 3)
    both = nr[:, 0] + nr[:, -1]
    both /= np.maximum(np.linalg.norm(both, axis=-1, keepdims=True), 1e-9)
    nr[:, 0] = nr[:, -1] = both
    count = rows * (n + 1)
    share = np.repeat(k, n + 1)
    la = np.tile(np.concatenate([local_of(a), local_of(a)[:1]]), (rows, 1))
    lb = np.tile(np.concatenate([local_of(b), local_of(b)[:1]]), (rows, 1))
    terms = np.zeros((count, K, 4))
    tj = np.zeros((count, K), np.int64)
    terms[:, 0, :3] = la * (1 - share[:, None])
    terms[:, 0, 3] = 1 - share
    tj[:, 0] = HEAD
    terms[:, 1, :3] = lb * share[:, None]
    terms[:, 1, 3] = share
    tj[:, 1] = CHEST
    joints = np.zeros((count, 4), np.int64)
    joints[:, 0], joints[:, 1] = HEAD, CHEST
    weights = np.zeros((count, 4))
    weights[:, 0], weights[:, 1] = 1 - share, share
    # the normals as the file wants them: such that the pose's blend of
    # the two bones turns them to what they are standing
    ibm = ref.ibm
    skin_h = Rs[HEAD] @ ibm[HEAD][:3, :3]
    skin_c = Rs[CHEST] @ ibm[CHEST][:3, :3]
    blend = skin_h[None] * (1 - share[:, None, None]) + skin_c[None] * share[:, None, None]
    bind_n = np.einsum('nij,nj->ni', np.linalg.inv(blend), nr.reshape(-1, 3))
    bind_n /= np.maximum(np.linalg.norm(bind_n, axis=1, keepdims=True), 1e-9)
    pos = (terms[:, 0, :3] @ R[HEAD].T + terms[:, 0, 3:4] * T[HEAD]) + (terms[:, 1, :3] @ R[CHEST].T +
                                                                       terms[:, 1, 3:4] * T[CHEST])
    mesh = Mesh(pos=pos, nrm=bind_n, uv=uvs, joints=joints, weights=weights, terms=terms, tjoints=tj, tris=tris)
    # (the layout's handedness is judged standing, where the tube is a tube)
    stand = Mesh(pos=pts, nrm=nr.reshape(-1, 3), uv=uvs, tris=tris)
    _, _, hand = bake.frames(stand.pos, stand.nrm, stand.uv, stand.tris)
    if hand.sum() < 0:
        mesh.uv = mesh.uv * np.array([-1.0, 1.0]) + np.array([1.0, 0.0])
    c = Chart('neck', rect, mesh)
    c.bones = [HEAD]
    if charts_.LEVEL:
        return c, {'triangles': len(tris)}
    _, _, w, h = rect
    tid, bary = bake.rasterize(mesh.uv, mesh.tris, (w, h), grow=1.5)
    c.on = tid >= 0
    down = bake.at_texels(share, mesh.tris, tid, bary)[..., 0]
    skin = np.array(f.get('skin', [250, 174, 140]), np.float32)
    shade = 0.74 + 0.2 * np.clip(down * 1.4, 0, 1)                  # under the jaw it is darker
    c.color = tint(np.broadcast_to(skin, (h, w, 3)).copy(), shade, f)
    c.rough[...] = 0.55
    c.occlusion = np.clip(shade + 0.1, 0, 1)
    return c, {'triangles': len(tris), 'rungs': len(top)}


# ---- the ears ----

def ear_chart(f, rect, R, T):
    """Both ears: each a shallow bowl with a rolled rim, standing out
    from the side of the head behind the jaw, a little pointed above."""
    e = f.get('ear', {})
    at = np.array(e.get('at', [42.0, 61.0, 15.0]))             # (front, side, up) of the left one's middle
    half_w, half_h = e.get('size', [11.0, 21.0])
    flare = np.radians(e.get('flare', 24.0))
    rho = np.array([0.06, 0.3, 0.55, 0.76, 0.92, 1.0, 1.0, 0.8])
    relief = np.array([-2.6, -3.2, 0.2, -1.0, 2.4, 0.6, -5.0, -7.0])      # out of the ear's own plane
    psi = np.linspace(0, 2 * np.pi, (21, 13, 7)[charts_.LEVEL])
    pieces, norms, uvs, tris_all, count = [], [], [], [], 0
    for k, side in enumerate((1.0, -1.0)):
        wide = np.array([-np.cos(flare), side * np.sin(flare), 0.0])       # across the ear, to its back edge
        tall = np.array([-0.2, 0.0, 0.98])
        tall -= wide * (tall @ wide)
        tall /= np.linalg.norm(tall)
        out = np.cross(wide, tall)
        out *= np.sign(out[1] * side)
        P = np.zeros((len(rho), len(psi), 3))
        for i, (r, h) in enumerate(zip(rho, relief)):
            point = 1.0 + e.get('point', 0.2) * np.exp(-((psi - np.radians(62)) / 0.5) ** 2)
            x = r * half_w * np.cos(psi) * (1.0 + 0.1 * np.sin(psi))
            y = r * half_h * np.sin(psi) * point
            rim = 0.45 + 0.55 * (0.5 + 0.5 * np.cos(psi))                 # the rim is rolled at the back, flat at the front
            lift = h * (rim if h > 0 else np.ones_like(rim))
            P[i] = (at * np.array([1, side, 1]) + wide * (x[:, None] + half_w * 0.75) + tall * y[:, None] +
                    out * lift[:, None])
        u = (k + 0.04 + 0.92 * np.linspace(0, 1, len(psi))) / 2
        uv = np.stack(np.meshgrid(u, np.linspace(0.04, 0.96, len(rho))), -1)
        pts, nrm, uvf, tris = grid_mesh(P, uv, at * np.array([1, 0, 1]))
        # (a bowl: its normals point out of the head's side, not from a middle)
        n = nrm.reshape(len(rho), len(psi), 3)
        if (n[:5] @ out).mean() < 0:
            nrm = -nrm
        pieces.append(pts)
        norms.append(nrm)
        uvs.append(uvf)
        tris_all.append(tris + count)
        count += len(pts)
    mesh = rigid(np.concatenate(pieces), np.concatenate(norms), np.concatenate(uvs), np.concatenate(tris_all),
                 HEAD, R, T)
    c = Chart('ears', rect, mesh)
    c.bones = [HEAD]
    if charts_.LEVEL:
        return c, {'triangles': int(len(mesh.tris))}
    _, _, w, h = rect
    tid, bary = bake.rasterize(mesh.uv, mesh.tris, (w, h), grow=1.5)
    c.on = tid >= 0
    v = np.linspace(0, 1, h, dtype=np.float32)[:, None].repeat(w, 1)
    depth = np.interp(v, np.linspace(0.04, 0.96, len(rho)), np.clip((relief + 3.2) / 5.6, 0, 1))
    skin = np.array(f.get('skin', [250, 174, 140]), np.float32)
    shade = 0.62 + 0.36 * depth
    c.color = tint(np.broadcast_to(skin, (h, w, 3)).copy(), shade, f)
    c.rough[...] = 0.55
    c.occlusion = np.clip(shade + 0.1, 0, 1)
    return c, {'triangles': int(len(mesh.tris))}


# ---- putting it together ----

def charts(ref, hd, recipe, work, rects):
    f = recipe['face']
    R, T = ref.bind()
    maps = dict(np.load(os.path.join(work, 'head_shells.npz')))
    stance = None
    path = os.path.join(work, 'stance.json')
    if os.path.exists(path):
        with open(path, encoding='utf-8') as fh:
            s = json.load(fh)
        stance = (np.array(s['R']), np.array(s['T']))
    out, said = [], {}
    c, said['face'] = face_chart(maps, f, rects['face'], R, T)
    out.append(c)
    c, said['neck'] = neck_chart(ref, f, rects['neck'], R, T, stance)
    out.append(c)
    if charts_.LEVEL < 2:
        c, said['ears'] = ear_chart(f, rects['ears'], R, T)
        out.append(c)
    if 'hair' in recipe:
        import hair
        made, said['hair'] = hair.charts(maps, recipe['hair'], rects['hair'], R, T, face_surface(maps, f, True))
        out += made
    return out, said
