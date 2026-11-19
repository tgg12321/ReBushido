"""A face painted anew: the eyes, the brows, the nose's and the mouth's
lines drawn as shapes on a canvas that faces the head (so many pixels to
one of the game's units, across the face and up it), which the head's
texels then take their colours from by where they are
(tools/port_models/README.md, "The head"). What is drawn and where is the
fighter's recipe (`face` in fighters.json), measured from the game's own
painting of him; nothing of the game's texture is copied.

Needs numpy and Pillow.
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter


class Canvas:
    """A picture over the face: `across` and `up` are the units it spans
    (lo, hi), `k` pixels to a unit. `rgb` (0..255) and `alpha` are the
    paint; `height` (units) is the relief the paint stands for."""

    def __init__(self, across=(-80.0, 80.0), up=(-60.0, 100.0), k=8):
        self.across, self.up, self.k = across, up, k
        self.w = int((across[1] - across[0]) * k)
        self.h = int((up[1] - up[0]) * k)
        self.rgb = np.zeros((self.h, self.w, 3), np.float32)
        self.alpha = np.zeros((self.h, self.w), np.float32)
        self.height = np.zeros((self.h, self.w), np.float32)
        ys, xs = np.mgrid[0:self.h, 0:self.w].astype(np.float32)
        self.S = across[0] + (xs + 0.5) / k
        self.U = up[1] - (ys + 0.5) / k

    def px(self, pts):
        pts = np.asarray(pts, np.float32)
        return np.stack([(pts[:, 0] - self.across[0]) * self.k, (self.up[1] - pts[:, 1]) * self.k], 1)

    def mask(self, polygon, soft=0.0):
        """A shape (its corners in units) as a picture 0..1, its edge
        `soft` units wide."""
        img = Image.new('L', (self.w, self.h), 0)
        ImageDraw.Draw(img).polygon([tuple(p) for p in self.px(polygon)], fill=255)
        img = img.filter(ImageFilter.GaussianBlur(max(0.6, soft * self.k)))
        return np.asarray(img, np.float32) / 255.0

    def lay(self, mask, color, strength=1.0):
        """Paint through a mask, over what is there."""
        a = np.clip(mask * strength, 0, 1)
        color = np.asarray(color, np.float32)
        if color.ndim == 1:
            color = color[None, None]
        new_alpha = a + self.alpha * (1 - a)
        self.rgb = (color * a[..., None] + self.rgb * (self.alpha * (1 - a))[..., None]) / \
            np.maximum(new_alpha, 1e-6)[..., None]
        self.alpha = new_alpha

    def mirror(self):
        """What is painted on the fighter's one side, on the other too."""
        other_rgb, other_a, other_h = self.rgb[:, ::-1], self.alpha[:, ::-1], self.height[:, ::-1]
        a = other_a
        new_alpha = a + self.alpha * (1 - a)
        self.rgb = (other_rgb * a[..., None] + self.rgb * (self.alpha * (1 - a))[..., None]) / \
            np.maximum(new_alpha, 1e-6)[..., None]
        self.alpha = new_alpha
        self.height = self.height + other_h

    def at(self, S, U):
        """The paint and its relief where the head's texels are."""
        x = (S - self.across[0]) * self.k - 0.5
        y = (self.up[1] - U) * self.k - 0.5
        x0 = np.clip(np.floor(x).astype(np.int64), 0, self.w - 2)
        y0 = np.clip(np.floor(y).astype(np.int64), 0, self.h - 2)
        fx = np.clip(x - x0, 0, 1).astype(np.float32)
        fy = np.clip(y - y0, 0, 1).astype(np.float32)
        inside = (x > 0) & (x < self.w - 1) & (y > 0) & (y < self.h - 1)

        def take(a):
            if a.ndim == 3:
                gx, gy = fx[..., None], fy[..., None]
            else:
                gx, gy = fx, fy
            return (a[y0, x0] * (1 - gx) + a[y0, x0 + 1] * gx) * (1 - gy) + \
                   (a[y0 + 1, x0] * (1 - gx) + a[y0 + 1, x0 + 1] * gx) * gy
        return take(self.rgb), take(self.alpha) * inside, take(self.height) * inside


def curve(points, n=24):
    """A smooth line through the points given (a Catmull-Rom spline), as
    `n` points a span."""
    p = np.asarray(points, np.float32)
    p = np.concatenate([p[:1] * 2 - p[1:2], p, p[-1:] * 2 - p[-2:-1]])
    out = []
    for i in range(1, len(p) - 2):
        t = np.linspace(0, 1, n, endpoint=False)[:, None]
        a, b, c, d = p[i - 1], p[i], p[i + 1], p[i + 2]
        out.append(0.5 * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t ** 2 +
                          (-a + 3 * b - 3 * c + d) * t ** 3))
    out.append(p[-2:-1])
    return np.concatenate(out)


def stroke(points, widths, n=24):
    """A brush stroke along a smooth line, as a shape: `widths` its whole
    width at each point given."""
    line = curve(points, n)
    w = np.interp(np.linspace(0, len(points) - 1, len(line)), np.arange(len(points)), widths)
    d = np.gradient(line, axis=0)
    d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-6)
    nrm = np.stack([-d[:, 1], d[:, 0]], 1)
    return np.concatenate([line + nrm * w[:, None] / 2, (line - nrm * w[:, None] / 2)[::-1]])


def eye(c, f):
    """One eye, on the fighter's own left (+across); `f` is the recipe:
    the corners, the lids' highest and lowest points, the iris."""
    inner, outer = np.array(f['inner']), np.array(f['outer'])
    top, bottom = np.array(f['top']), np.array(f['bottom'])
    upper = curve([inner, (inner + top) / 2 + [-(top[0] - inner[0]) * 0.18, (top[1] - inner[1]) * 0.32], top,
                   (top + outer) / 2 + [(outer[0] - top[0]) * 0.2, (top[1] - outer[1]) * 0.38], outer])
    lower = curve([outer, (outer + bottom) / 2 + [(outer[0] - bottom[0]) * 0.12, -(outer[1] - bottom[1]) * 0.3],
                   bottom, (bottom + inner) / 2 + [-(bottom[0] - inner[0]) * 0.1, -(inner[1] - bottom[1]) * 0.25],
                   inner])
    almond = np.concatenate([upper, lower])
    white = c.mask(almond, 0.12)
    # the lid's tint round the eye
    S, U = c.S, c.U
    mid = (inner + outer) / 2
    lid = np.exp(-(((S - mid[0]) / (np.linalg.norm(outer - inner) * 0.62)) ** 2 +
                   ((U - mid[1] - 2.0) / 11.0) ** 2))
    c.lay(lid, f.get('lid_color', [222, 120, 100]), 0.42)
    # the white, shaded under the upper lid
    shade = np.clip((U - (bottom[1] + 1.0)) / max(top[1] - bottom[1], 1e-3), 0, 1)
    sclera = np.array(f.get('white', [246, 240, 236]), np.float32)[None, None] * (1 - 0.42 * shade[..., None] ** 1.5)
    c.lay(white, sclera)
    # the iris: darker at its rim and above, lit below; the pupil
    ic, (rx, ry) = np.array(f['iris']), f['iris_radii']
    d = np.sqrt(((S - ic[0]) / rx) ** 2 + ((U - ic[1]) / ry) ** 2)
    disc = np.clip((1.0 - d) * rx * c.k / 1.5, 0, 1) * white
    dark, lit = np.array(f['iris_dark'], np.float32), np.array(f['iris_lit'], np.float32)
    t = np.clip((ic[1] + ry * 0.2 - U) / (ry * 1.1), 0, 1)             # lower is lighter
    rim = np.clip((d - 0.72) / 0.28, 0, 1)
    col = dark[None, None] * (1 - t[..., None]) + lit[None, None] * t[..., None]
    col = col * (1 - 0.65 * rim[..., None])
    # fine rays in the iris
    ang = np.arctan2(U - ic[1], S - ic[0])
    rays = 0.5 + 0.5 * np.sin(ang * 23.0) * np.sin(ang * 7.0 + 1.3)
    col = col * (0.9 + 0.2 * rays[..., None] * np.clip(d * 1.5, 0, 1)[..., None])
    c.lay(disc, col)
    pupil = np.clip((f.get('pupil', 0.42) - d) * rx * c.k / 1.5, 0, 1) * white
    c.lay(pupil, [10, 6, 8])
    # the lids' lines: the upper lash thick and dark, winged at the outer
    # corner; the lower thin and brown, from the outer corner inwards
    lash = f.get('lash', 3.4)
    n = len(upper)
    wide = lash * (0.45 + 0.75 * np.sin(np.linspace(0.05, 1.0, n) * np.pi * 0.62) ** 1.2)
    up_line = np.concatenate([upper + [0, 1] * wide[:, None] * 0.75, (upper - [0, 1] * wide[:, None] * 0.25)[::-1]])
    c.lay(c.mask(up_line, 0.1), f.get('lash_color', [18, 12, 12]))
    # ... its tail past the corner, the way the lid's line was going
    way = upper[-1] - upper[-6]
    way = way / max(np.linalg.norm(way), 1e-6)
    way = (way + np.array([0.35, 0.75])) / np.linalg.norm(way + np.array([0.35, 0.75]))
    base = upper[-1] + np.array([0, 1]) * wide[-1] * 0.25
    wing = stroke([base - way * 2.0, base + way * 2.2, base + way * 5.5], [wide[-1] * 1.0, wide[-1] * 0.7, 0.2], 10)
    c.lay(c.mask(wing, 0.1), f.get('lash_color', [18, 12, 12]))
    low = lower[: int(len(lower) * 0.72)]
    low_line = stroke(low[::6], np.linspace(1.5, 0.5, len(low[::6])), 6)
    c.lay(c.mask(low_line, 0.12), f.get('lower_color', [110, 52, 40]), 0.85)
    # the fold of the lid above
    fold = stroke([inner + [3.0, 6.2], top + [-2.0, 4.6], top + [9.0, 4.4], outer + [0.5, 6.0]],
                  [0.5, 1.0, 1.0, 0.4], 12)
    c.lay(c.mask(fold, 0.15), f.get('fold_color', [150, 70, 56]), 0.7)
    c.height -= white * 0.8                      # the eye lies a little behind its lids
    c.height += c.mask(up_line, 0.4) * 0.9


def eye_lights(c, f):
    """The light in each eye, on the same side of both (it is one light
    they both look at), painted after the eyes are mirrored."""
    S, U = c.S, c.U
    (rx, ry) = f['iris_radii']
    for side in (1, -1):
        ic = np.array([f['iris'][0] * side, f['iris'][1]])
        for (ds, du, r, s) in f.get('lights', [(-0.36, 0.3, 0.27, 1.0), (0.36, -0.42, 0.12, 0.75)]):
            dd = np.sqrt((S - ic[0] - ds * rx) ** 2 + (U - ic[1] - du * ry) ** 2)
            c.lay(np.clip((r * rx - dd) * c.k / 1.2, 0, 1), [255, 255, 255], s)


def brow(c, f):
    pts = np.array(f['line'], np.float32)
    shape = stroke(pts, f['widths'], 16)
    m = c.mask(shape, 0.18)
    # the hairs of it: fine streaks along it
    S, U = c.S, c.U
    d = pts[-1] - pts[0]
    d /= np.linalg.norm(d)
    along = S * d[0] + U * d[1]
    across = -S * d[1] + U * d[0]
    hairs = 0.5 + 0.5 * np.sin(across * 5.2 + np.sin(along * 0.9) * 1.5 + along * 1.1)
    col = np.array(f.get('color', [22, 16, 14]), np.float32)[None, None] * (0.85 + 0.5 * hairs[..., None])
    c.lay(m, col, 0.97)
    c.height += m * 0.7


def nose(c, f):
    """The nose's own lines: the shadow under its tip, the nostrils, a
    soft line down one side of the bridge."""
    tip = np.array(f['tip'], np.float32)
    S, U = c.S, c.U
    under = np.exp(-(((S - tip[0]) / f.get('under_wide', 7.5)) ** 2 + ((U - tip[1] + 5.0) / 2.6) ** 2))
    c.lay(under, f.get('shade_color', [214, 104, 78]), 0.6)
    for side in (-1, 1):
        nostril = stroke([tip + [side * 3.0, -5.4], tip + [side * 5.6, -4.6], tip + [side * 7.6, -2.6]],
                         [0.9, 1.5, 0.5], 8)
        c.lay(c.mask(nostril, 0.2), f.get('line_color', [120, 48, 40]), 0.8)
        c.height -= c.mask(nostril, 0.3) * 0.6


def mouth(c, f):
    """The mouth: the line between the lips, the lips' own colour above
    and below it, the shade under the lower lip."""
    pts = np.array(f['line'], np.float32)                    # across the whole mouth, corner to corner
    S, U = c.S, c.U
    mid = pts[len(pts) // 2]
    half = abs(pts[-1][0] - pts[0][0]) / 2
    line_u = np.interp(S, pts[:, 0], pts[:, 1])
    within = np.clip((half - np.abs(S - mid[0])) / (half * 0.35), 0, 1)
    upper = np.exp(-((U - line_u - 1.6) / 1.9) ** 2) * within * np.clip(1.25 - np.abs(S - mid[0]) / half, 0, 1)
    lower = np.exp(-((U - line_u + 2.6) / 2.4) ** 2) * within ** 2 * np.clip(1.0 - np.abs(S - mid[0]) / (half * 0.8), 0, 1)
    c.lay(upper, f.get('lip_color', [206, 96, 80]), 0.6)
    c.lay(lower, f.get('lip_color', [206, 96, 80]), 0.42)
    c.lay(np.exp(-(((S - mid[0]) / (half * 0.55)) ** 2 + ((U - line_u + 6.6) / 1.5) ** 2)),
          f.get('shade_color', [214, 104, 78]), 0.45)
    line = stroke(pts, f.get('widths', [0.5] + [1.3] * (len(pts) - 2) + [0.5]), 16)
    c.lay(c.mask(line, 0.14), f.get('line_color', [92, 30, 30]), 0.95)
    c.height -= c.mask(line, 0.3) * 0.9


def paint_face(f, k=8):
    """The canvas of a fighter's face from his recipe."""
    c = Canvas(k=k)
    eye(c, f['eye'])
    brow(c, f['brow'])
    c.mirror()
    eye_lights(c, f['eye'])
    nose(c, f['nose'])
    mouth(c, f['mouth'])
    return c
