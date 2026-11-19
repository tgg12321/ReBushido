"""A fighter's hands made whole (tools/port_models/README.md, "The
hands"): each a palm and a wrist (one lofted surface), four fingers of
three bones and a thumb, closed round the game's grip, in the hand
bone's own frame as the game's hand lies in it: the back of the hand
towards +x, the fingers along +y from the knuckles and curled towards -x,
the thumb and the index finger on the +z side, the handle across the palm
along z. The hand of the other arm is its mirror (x turned round), as the
game's are. What it wears (a glove, fingerless or whole, open on the back
or not) is the fighter's recipe (`hand`), and is painted and modelled in
relief: the glove's hems, its strap and stud, the nails, the creases.

Needs numpy and Pillow.
"""
import numpy as np

import bake
from charts import Chart
from facepaint import curve
import head as hd_

HAND_A, HAND_B = 7, 11

# each finger: where its knuckle is across the hand (z), how thick, the
# lengths of its three bones
FINGERS = (
    ('index', 28.5, 9.4, (37.0, 23.0, 18.0)),
    ('middle', 9.0, 9.8, (40.0, 25.0, 19.0)),
    ('ring', -10.5, 9.2, (37.0, 23.0, 18.0)),
    ('little', -29.0, 8.0, (29.0, 18.0, 16.0)),
)
KNUCKLE = (17.0, 110.0)                  # x, y of the knuckles
CURL = (57.0, 84.0, 60.0)                # each joint's bend (degrees) round the handle
THUMB = (((-4.0, 26.0, 28.0), 15.0), ((-27.0, 58.0, 39.0), 12.5), ((-43.0, 86.0, 35.0), 10.6),
         ((-47.0, 108.0, 24.0), 9.0))


def sweep(points, radii, flat=0.92, around=10, cap=4, side=None):
    """A tube along the smooth line through `points`: rows of `around`
    points, the radius between those given; the end closed round. Returns
    the rows (n, around + 1, 3), how far along each row is (0..1) and the
    angle of each column (0 on the side `side` points to)."""
    line = curve(np.array(points, float), 6)
    r = np.interp(np.linspace(0, len(points) - 1, len(line)), np.arange(len(points)), radii)
    t = np.gradient(line, axis=0)
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    side = np.array(side if side is not None else [0.0, 0.0, 1.0], float)
    rows = []
    b = side - t[0] * (side @ t[0])
    b /= np.linalg.norm(b)
    phi = np.linspace(0, 2 * np.pi, around + 1)
    seg = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(line, axis=0), axis=1))])
    along = []
    for i in range(len(line)):
        b = b - t[i] * (b @ t[i])
        b /= np.linalg.norm(b)
        n = np.cross(b, t[i])
        rows.append(line[i] + np.outer(np.cos(phi), n) * r[i] * flat + np.outer(np.sin(phi), b) * r[i])
        along.append(seg[i])
    # the end, round
    n = np.cross(b, t[-1])
    for k in range(1, cap + 1):
        a = k / cap * np.pi / 2
        c = line[-1] + t[-1] * np.sin(a) * r[-1] * 0.9
        rr = max(np.cos(a), 0.04) * r[-1]
        rows.append(c + np.outer(np.cos(phi), n) * rr * flat + np.outer(np.sin(phi), b) * rr)
        along.append(seg[-1] + np.sin(a) * r[-1] * 0.9)
    along = np.array(along)
    return np.array(rows), along / along[-1], phi


def finger(z, radius, lengths, grip=1.0):
    x, y = KNUCKLE
    pts = [np.array([x - 2.0, y - 16.0, z]), np.array([x, y, z])]
    angle = 0.0
    for k, length in enumerate(lengths):
        angle += np.radians(CURL[k]) * grip
        pts.append(pts[-1] + np.array([-np.sin(angle), np.cos(angle), 0.0]) * length)
    radii = [radius * 1.02, radius * 1.1, radius * 1.0, radius * 0.93, radius * 0.82]
    return pts, radii


def palm(h):
    """The palm and the wrist as rows round the hand, from the wrist
    (y -30) to past the knuckles."""
    ys = np.array([-30.0, -22, -13, -4, 8, 24, 42, 62, 82, 98, 108, 114, 118, 120])
    cx = np.interp(ys, [-30, 0, 45, 110, 120], [2.0, 2.0, 3.0, 5.0, 6.0])
    hx = np.interp(ys, [-30, -5, 10, 45, 100, 110, 116, 120], [14.5, 14.5, 15.0, 17.5, 16.5, 13.0, 8.0, 1.0])
    hz = np.interp(ys, [-30, -5, 12, 45, 85, 110, 116, 120], [24.0, 25.0, 31.0, 38.5, 41.0, 40.0, 37.0, 31.0])
    cz = np.interp(ys, [-30, 0, 60, 120], [0.0, 0.0, -1.0, -0.5])
    power = np.interp(ys, [-30, -5, 30, 120], [2.3, 2.4, 3.2, 3.4])
    phi = np.linspace(0, 2 * np.pi, 25)
    rows = []
    for i, y in enumerate(ys):
        c, s = np.cos(phi), np.sin(phi)
        e = 2.0 / power[i]
        x = cx[i] + hx[i] * np.sign(c) * np.abs(c) ** e
        z = cz[i] + hz[i] * np.sign(s) * np.abs(s) ** e
        # the ball of the thumb, on the palm's side
        x -= 8.0 * np.clip(-c, 0, 1) * np.exp(-((y - 34.0) / 26.0) ** 2) * np.exp(-((z - 24.0) / 18.0) ** 2)
        # ... and the heel of the hand under the little finger
        x -= 4.0 * np.clip(-c, 0, 1) * np.exp(-((y - 26.0) / 24.0) ** 2) * np.exp(-((z + 24.0) / 16.0) ** 2)
        # the knuckles stand up on the back
        for _, kz, kr, _ in FINGERS:
            x += 3.2 * np.clip(c, 0, 1) * np.exp(-((y - 106.0) / 9.0) ** 2) * np.exp(-((z - kz) / (kr * 0.8)) ** 2)
        # the strap round the wrist stands proud of the glove
        if h.get('glove') is not None or True:
            lift = 1.6 * np.exp(-((y + 14.0) / 6.5) ** 4)
            x += lift * c
            z += lift * s
        rows.append(np.stack([x, np.full_like(x, y), z], 1))
    return np.array(rows), ys, phi


def build(h):
    """The hand of bone 7 as surfaces: (name, rows of points, what is
    known along and round each)."""
    out = []
    rows, ys, phi = palm(h)
    out.append(('palm', rows, ys, phi))
    for name, z, radius, lengths in FINGERS:
        pts, radii = finger(z, radius * h.get('thick', 1.0), lengths, h.get('grip', 1.0))
        rows, along, phi = sweep(pts, radii)
        out.append((name, rows, along, phi))
    rows, along, phi = sweep([p for p, _ in THUMB], [r for _, r in THUMB], flat=0.95, side=[-0.5, 0.3, 0.8])
    out.append(('thumb', rows, along, phi))
    return out


def layout(parts):
    """Each surface's rectangle of the hand's chart (0..1): the palm over
    the top half, the fingers and the thumb side by side below."""
    cells = {'palm': (0.0, 0.0, 1.0, 0.5)}
    names = [p[0] for p in parts if p[0] != 'palm']
    for k, n in enumerate(names):
        cells[n] = (k / len(names), 0.5, 1.0 / len(names), 0.5)
    return cells


def paint(name, P, N, along, phi_at, h, colors):
    """What a surface is at each texel: the colour, the height of its
    relief (units), how rough and how much a metal. P and N are in the
    hand's frame; `along` is the palm's y or how far along a finger
    (0..1), `phi_at` the angle round (a finger's back is at pi)."""
    skin = np.array(colors['skin'], np.float32)
    glove = np.array(colors['glove'], np.float32) if colors.get('glove') is not None else None
    shape = P.shape[:2]
    color = np.broadcast_to(skin, shape + (3,)).copy()
    height = np.zeros(shape, np.float32)
    rough = np.full(shape, 0.55, np.float32)
    metal = np.zeros(shape, np.float32)
    x, y, z = P[..., 0], P[..., 1], P[..., 2]
    gloved = np.zeros(shape, bool)
    hem = np.zeros(shape, np.float32)                 # 1 on the glove's hems
    back = np.cos(phi_at - np.pi) if name != 'palm' else np.cos(phi_at)       # 1 on the back of the hand
    if name == 'palm':
        if glove is not None:
            gloved[...] = True
            if h.get('open_back', False):
                oy, oz, ry, rz = h.get('opening', [24.0, -5.0, 21.0, 23.0])
                d = np.sqrt(((y - oy) / ry) ** 2 + ((z - oz) / rz) ** 2)
                hole = (d < 1.0) & (back > 0.25)
                gloved &= ~hole
                hem = np.maximum(hem, np.exp(-((d - 1.06) / 0.07) ** 2) * (back > 0.2))
            strap = np.exp(-((y + 14.0) / 6.5) ** 4)
            hem = np.maximum(hem, 0.6 * np.exp(-((np.abs(y + 14.0) - 7.5) / 0.9) ** 2))
            # the stud on the back of the strap
            sd = np.sqrt((y + 14.0) ** 2 + (z - h.get('stud_z', 2.0)) ** 2)
            stud = (sd < 5.0) & (back > 0.3)
            height += np.where(stud, 1.6 * np.sqrt(np.clip(1 - (sd / 5.0) ** 2, 0, 1)), 0)
            color[stud] = (168, 168, 176)
            metal[stud] = 0.9
            rough[stud] = 0.3
            gloved &= ~stud
            height += strap * 0.0
        # the skin of the back of the hand: the tendons to the knuckles
        for _, kz, kr, _ in FINGERS:
            height += 0.5 * np.exp(-((z - kz) / 4.0) ** 2) * np.clip((y - 40.0) / 50.0, 0, 1) * (back > 0)
    else:
        cut = h.get('finger_cut', 0.2 if name != 'thumb' else 0.42)
        if glove is not None:
            if h.get('fingerless', False):
                gloved = along < cut
                hem = np.exp(-((along - cut + 0.012) / 0.014) ** 2)
            else:
                gloved[...] = True
        bare = ~gloved
        # the joints: creases on the palm's side, wrinkles on the back
        joints = (0.33, 0.6, 0.82) if name != 'thumb' else (0.32, 0.66)
        for j in joints[1:] if name != 'thumb' else joints:
            line = np.exp(-((along - j) / 0.012) ** 2)
            height -= 0.7 * line * np.clip(-back, 0, 1) * bare
            for o in (-0.022, 0.0, 0.022):
                height -= 0.35 * np.exp(-((along - j - o) / 0.007) ** 2) * np.clip(back, 0, 1) * bare
            height += 0.6 * np.exp(-((along - j) / 0.05) ** 2) * np.clip(back, 0, 1)
        # the nail, on the back of the tip
        nd = np.sqrt(((along - 0.905) / 0.075) ** 2 + ((phi_at - np.pi) / 0.95) ** 2)
        nail = (nd < 1.0) & bare
        color[nail] = skin * 0.45 + np.array([255, 226, 218], np.float32) * 0.55
        tipw = nail & (along > 0.955)
        color[tipw] = (244, 232, 226)
        rough[nail] = 0.3
        height += np.where(nail, 0.5, 0) - 0.5 * np.exp(-((nd - 1.0) / 0.12) ** 2) * bare
        # the knuckle's and the tip's skin a little redder
        warm = (np.exp(-((along - 0.6) / 0.06) ** 2) * np.clip(back, 0, 1) + 0.6 * np.clip((along - 0.86) / 0.14, 0, 1) *
                np.clip(-back, 0, 1)) * bare
        color = color * (1 - 0.14 * warm[..., None]) + np.array([236, 110, 96], np.float32) * 0.14 * warm[..., None]
    if glove is not None:
        grain = bake.fbm3(P, 2.2, 2, seed=5)
        leather = glove[None, None] * (1.0 + 0.5 * grain[..., None]) + 6.0 * np.clip(grain, 0, 1)[..., None]
        color[gloved] = leather[gloved]
        rough[gloved] = 0.5 + 0.08 * grain[gloved]
        height += np.where(gloved, 1.0 + 0.25 * grain, 0)                    # the leather's own thickness
        height += hem * 0.9
        # a row of stitches inside each hem
        color = np.where((gloved & (hem > 0.35) & (hem < 0.8))[..., None], color * 0.6 + 70.0 * 0.4, color)
    return color, height, rough, metal


def hand_chart(name, bone, mirror, h, colors, rect, R, T):
    parts = build(h)
    cells = layout(parts)
    pieces, norms, uvs, tris_all, count = [], [], [], [], 0
    known = []
    for pname, rows, along, phi in parts:
        P = rows * (np.array([-1.0, 1.0, 1.0]) if mirror else 1.0)
        cx, cy, cw, chh = cells[pname]
        u = cx + cw * (0.03 + 0.94 * np.linspace(0, 1, P.shape[1]))
        v = cy + chh * (0.03 + 0.94 * np.linspace(0, 1, P.shape[0]))
        uv = np.stack(np.meshgrid(u, v), -1)
        inside = P.reshape(-1, 3).mean(0)
        pts, nrm, uvf, tris = hd_.grid_mesh(hd_.fsu(P), uv, hd_.fsu(inside))
        if pname == 'palm':
            # (a loft's rows are rings: away from its own axis)
            axis = P.mean(1, keepdims=True)
            flip = ((hd_.local_of(nrm).reshape(P.shape) * (P - axis)).sum(-1) < 0).mean() > 0.5
            nrm = -nrm if flip else nrm
        pieces.append(pts)
        norms.append(nrm)
        uvs.append(uvf)
        tris_all.append(tris + count)
        a = np.repeat(np.asarray(along, np.float32), P.shape[1])
        ph = np.tile(np.asarray(phi, np.float32), P.shape[0])
        known.append(np.stack([a, ph], 1))
        count += len(pts)
    mesh = hd_.rigid(np.concatenate(pieces), np.concatenate(norms), np.concatenate(uvs), np.concatenate(tris_all),
                     bone, R, T)
    turned = hd_.right_handed(mesh)
    c = Chart(name, rect, mesh)
    c.bones = [bone]
    _, _, w, hh = rect
    tid, bary = bake.rasterize(mesh.uv, mesh.tris, (w, hh), grow=1.5)
    c.on = tid >= 0
    local = mesh.terms[:, 0, :3] * (np.array([-1.0, 1.0, 1.0]) if mirror else 1.0)       # as the hand of bone 7
    P = bake.at_texels(local, mesh.tris, tid, bary)
    N = bake.at_texels(hd_.local_of(np.concatenate(norms)), mesh.tris, tid, bary)
    K = bake.at_texels(np.concatenate(known), mesh.tris, tid, bary)
    height = np.zeros((hh, w), np.float32)
    for pname, rows, along, phi in parts:
        cx, cy, cw, chh = cells[pname]
        if turned:
            cx = 1.0 - cx - cw
        x0, x1 = int(round(cx * w)), int(round((cx + cw) * w))
        y0, y1 = int(round(cy * hh)), int(round((cy + chh) * hh))
        sl = (slice(y0, y1), slice(x0, x1))
        col, hgt, rgh, mtl = paint(pname, P[sl], N[sl], K[sl][..., 0], K[sl][..., 1], h, colors)
        c.color[sl], c.rough[sl], c.metal[sl] = col, rgh, mtl
        height[sl] = hgt
    c.normal = bake.normal_of_height_at(height, P, c.on)
    hollow = height - bake.blur_in(height, c.on, 3.0)
    c.occlusion = np.clip(0.92 + hollow * 0.12, 0.6, 1.0)
    c.color = c.color * np.clip(1.0 + hollow * 0.1, 0.75, 1.08)[..., None]
    return c, int(len(mesh.tris))


def charts(ref, hd, recipe, work, rects):
    import json
    import os
    h = recipe['hand']
    with open(os.path.join(work, 'pieces.json'), encoding='utf-8') as f:
        said = json.load(f)['colors']
    skin = h.get('skin') or said['skin_srgb']
    glove = h.get('glove')
    if glove is None and h.get('gloved', False):
        glove = said.get('glove_srgb', [24, 24, 26])
    colors = {'skin': skin, 'glove': glove}
    R, T = ref.bind()
    out, tris = [], {}
    for name, bone, mirror in (('hand_a', HAND_A, False), ('hand_b', HAND_B, True)):
        c, tris[name] = hand_chart(name, bone, mirror, h, colors, rects[name], R, T)
        out.append(c)
    return out, {'triangles': tris, 'colors': colors}
