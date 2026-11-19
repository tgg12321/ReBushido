"""Pictures made in a model's own texture layout, in numpy alone: which
triangle each texel lies on and where on it, so that anything known at
the vertices (the place, the normal, a place in another texture) is known
at every texel; and what the remake paints with (blurs, noise, distances,
a height turned into a normal map in the frame the port's shader works
out at the pixel). tools/port_models/README.md, "The maps".

Needs numpy and Pillow.
"""
import numpy as np
from PIL import Image


# ---- texels on triangles ----

def rasterize(uv, tris, size, grow=0.5):
    """For each texel of a picture `size` (w, h): the triangle of `tris`
    (points `uv`, 0..1, v down) it lies on, -1 for none, and its three
    weights there. A texel within `grow` texels of a triangle counts as on
    it (the first to claim it keeps it), so that the islands' edges are
    covered."""
    w, h = size
    tri_id = np.full((h, w), -1, np.int32)
    bary = np.zeros((h, w, 3), np.float32)
    best = np.full((h, w), 1e9, np.float32)
    P = uv[tris] * np.array([w, h]) - 0.5          # texel centres at whole numbers
    for t in range(len(tris)):
        a, b, c = P[t]
        det = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
        if abs(det) < 1e-9:
            continue
        x0 = max(0, int(np.floor(min(a[0], b[0], c[0]) - grow)))
        x1 = min(w - 1, int(np.ceil(max(a[0], b[0], c[0]) + grow)))
        y0 = max(0, int(np.floor(min(a[1], b[1], c[1]) - grow)))
        y1 = min(h - 1, int(np.ceil(max(a[1], b[1], c[1]) + grow)))
        if x1 < x0 or y1 < y0:
            continue
        xs = np.arange(x0, x1 + 1, dtype=np.float32)[None, :]
        ys = np.arange(y0, y1 + 1, dtype=np.float32)[:, None]
        w1 = ((xs - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (ys - a[1])) / det
        w2 = ((b[0] - a[0]) * (ys - a[1]) - (xs - a[0]) * (b[1] - a[1])) / det
        w0 = 1 - w1 - w2
        # how far outside the triangle, in texels (0 inside)
        ea = np.linalg.norm(b - a) + 1e-9
        eb = np.linalg.norm(c - b) + 1e-9
        ec = np.linalg.norm(a - c) + 1e-9
        ad = abs(det)
        out = np.maximum(np.maximum(-w2 * ad / ea, -w0 * ad / eb), -w1 * ad / ec)
        out = np.maximum(out, 0)
        sub = best[y0:y1 + 1, x0:x1 + 1]
        take = (out <= grow) & (out < sub)
        if not take.any():
            continue
        sub[take] = out[take]
        tri_id[y0:y1 + 1, x0:x1 + 1][take] = t
        bb = bary[y0:y1 + 1, x0:x1 + 1]
        bb[take] = np.stack([w0, w1, w2], -1)[take]
    return tri_id, bary


def at_texels(values, tris, tri_id, bary):
    """What is known at the vertices (N, K), at every texel (h, w, K);
    nought where no triangle is."""
    v = np.asarray(values, np.float32)
    if v.ndim == 1:
        v = v[:, None]
    corners = v[tris[np.maximum(tri_id, 0)]]                 # (h, w, 3, K)
    out = (corners * bary[..., None]).sum(2)
    out[tri_id < 0] = 0
    return out


def spread(img, mask, passes=12):
    """The picture's drawn texels (`mask`) spread outwards into those not
    drawn, so that a texture point near an island's edge, or a smaller
    copy of the picture, never takes in what is not the island's."""
    img = img.astype(np.float32).copy()
    mask = mask.copy()
    for _ in range(passes):
        if mask.all():
            break
        acc = np.zeros_like(img)
        cnt = np.zeros(mask.shape, np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            m = np.roll(mask, (dy, dx), (0, 1))
            acc += np.roll(img, (dy, dx), (0, 1)) * (m[..., None] if img.ndim == 3 else m)
            cnt += m
        new = ~mask & (cnt > 0)
        div = np.maximum(cnt, 1)
        img[new] = (acc / (div[..., None] if img.ndim == 3 else div))[new]
        mask |= new
    return img


# ---- painting ----

def box(a, r, axis):
    """The mean over 2r+1 along an axis, the ends held."""
    if r <= 0:
        return a
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pad, mode='edge'), axis=axis, dtype=np.float64)
    n = a.shape[axis]
    hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis)
    lo = np.take(c, np.arange(0, n), axis=axis)
    return ((hi - lo) / (2 * r + 1)).astype(np.float32)


def blur(a, sigma):
    """Near enough a Gaussian blur: three means, one after another."""
    if sigma <= 0.3:
        return a.astype(np.float32)
    r = max(1, int(round(np.sqrt(sigma * sigma * 12 / 3 + 1) / 2 - 0.5)))
    out = a.astype(np.float32)
    for _ in range(3):
        out = box(box(out, r, 0), r, 1)
    return out


def blur_in(a, mask, sigma):
    """A blur that takes in only the texels of `mask` (an island's own)."""
    m = mask.astype(np.float32)
    num = blur(a * (m[..., None] if a.ndim == 3 else m), sigma)
    den = blur(m, sigma)
    den = np.maximum(den, 1e-4)
    return num / (den[..., None] if a.ndim == 3 else den)


def distance(mask, most=64):
    """How far each texel is from the nearest texel of `mask`, in texels,
    up to `most` (a chamfer's distance: good to a few parts in a
    hundred)."""
    d = np.where(mask, 0.0, float(most)).astype(np.float32)
    for _ in range(int(most) + 1):
        before = d
        n = d
        for dy, dx, cost in ((0, 1, 1.0), (0, -1, 1.0), (1, 0, 1.0), (-1, 0, 1.0),
                             (1, 1, 1.41421), (1, -1, 1.41421), (-1, 1, 1.41421), (-1, -1, 1.41421)):
            s = np.roll(d, (dy, dx), (0, 1))
            # nothing comes round the picture's edge
            if dy == 1:
                s[0, :] = most
            if dy == -1:
                s[-1, :] = most
            if dx == 1:
                s[:, 0] = most
            if dx == -1:
                s[:, -1] = most
            n = np.minimum(n, s + cost)
        d = n
        if np.array_equal(before, d):
            break
    return d


def hash3(ix, iy, iz, seed=0):
    """A number in 0..1 for each whole point of space."""
    n = (ix.astype(np.int64) * 73856093) ^ (iy.astype(np.int64) * 19349663) ^ \
        (iz.astype(np.int64) * 83492791) ^ (seed * 2654435761)
    n = (n ^ (n >> 13)) * 1274126177
    n = n ^ (n >> 16)
    return ((n & 0xFFFFFF) / float(0xFFFFFF)).astype(np.float32)


def noise3(p, scale, seed=0):
    """Smooth noise in space (-1..1), a lump every `scale` units: the same
    on both sides of a seam of the texture, being of the place."""
    q = p / scale
    i = np.floor(q).astype(np.int64)
    f = (q - i).astype(np.float32)
    f = f * f * (3 - 2 * f)
    out = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                wgt = (f[..., 0] if dx else 1 - f[..., 0]) * (f[..., 1] if dy else 1 - f[..., 1]) * \
                      (f[..., 2] if dz else 1 - f[..., 2])
                out = out + wgt * hash3(i[..., 0] + dx, i[..., 1] + dy, i[..., 2] + dz, seed)
    return out * 2 - 1


def fbm3(p, scale, octaves=3, seed=0, gain=0.5):
    out = 0
    amp = 1.0
    total = 0.0
    for o in range(octaves):
        out = out + amp * noise3(p, scale / (2 ** o), seed + o * 17)
        total += amp
        amp *= gain
    return out / total


def noise2(shape, scale, seed=0):
    """Smooth noise over a picture (-1..1), a lump every `scale` texels."""
    h, w = shape
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    p = np.stack([xs, ys, np.zeros_like(xs)], -1)
    return noise3(p, scale, seed)


# ---- heights to normals ----

def normal_of_height(height, texels_per_unit, mask=None):
    """A tangent-space normal map (OpenGL's: red along u, green up the
    picture, as the port's shader builds its frame, the tangent along u
    and the other axis the normal crossed with it) of a height in the
    model's units, where the layout has `texels_per_unit` texels to a
    unit (a number, or one for each texel). An island's edge has no
    slope."""
    h = height.astype(np.float32)
    if mask is not None:
        h = spread(h, mask, 3)
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * texels_per_unit
    gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * texels_per_unit
    n = np.stack([-gx, gy, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n


def normal_of_height_at(height, P, mask):
    """The same of a layout that is not at one scale: P is each texel's
    place (units), so that a texel's step along the picture is as long
    as the surface says it is there."""
    h = height.astype(np.float32)
    ok = mask & np.roll(mask, 1, 1) & np.roll(mask, -1, 1) & np.roll(mask, 1, 0) & np.roll(mask, -1, 0)
    sx = np.linalg.norm(np.roll(P, -1, 1) - np.roll(P, 1, 1), axis=-1)
    sy = np.linalg.norm(np.roll(P, -1, 0) - np.roll(P, 1, 0), axis=-1)
    gx = np.where(ok, (np.roll(h, -1, 1) - np.roll(h, 1, 1)) / np.maximum(sx, 1e-3), 0)
    gy = np.where(ok, (np.roll(h, -1, 0) - np.roll(h, 1, 0)) / np.maximum(sy, 1e-3), 0)
    n = np.stack([-gx, gy, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n


def pack_normal(n, mask=None):
    out = np.clip((n * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)
    if mask is not None:
        out[~mask] = (128, 128, 255)
    return np.concatenate([out, np.full(out.shape[:2] + (1,), 255, np.uint8)], -1)


def blend_normals(base, detail):
    """Two tangent-space normals as one (the slopes added)."""
    s = base[..., :2] / np.maximum(base[..., 2:3], 1e-3) + detail[..., :2] / np.maximum(detail[..., 2:3], 1e-3)
    n = np.concatenate([s, np.ones_like(s[..., :1])], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def frames(pos, nrm, uv, tris):
    """Each triangle's tangent as the port's shader finds it: the way the
    place changes along u, and whether the normal crossed with it goes up
    the picture (+1) or down it (-1: the island is the mirror of what a
    normal map wants)."""
    p0, p1, p2 = (pos[tris[:, k]] for k in range(3))
    u0, u1, u2 = (uv[tris[:, k]] for k in range(3))
    e1, e2 = p1 - p0, p2 - p0
    d1, d2 = u1 - u0, u2 - u0
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    det = np.where(np.abs(det) < 1e-20, 1e-20, det)
    T = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / det[:, None]       # dP/du
    Bv = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / det[:, None]      # dP/dv (down the picture)
    n = nrm[tris].sum(1)
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    hand = np.sign((np.cross(n, T) * -Bv).sum(1))
    return T, Bv, hand


def sample(img, uv, wrap=False):
    """The picture at texture points (..., 2), between its texels."""
    h, w = img.shape[:2]
    x = uv[..., 0] * w - 0.5
    y = uv[..., 1] * h - 0.5
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = (x - x0).astype(np.float32)
    fy = (y - y0).astype(np.float32)
    if img.ndim == 3:
        fx, fy = fx[..., None], fy[..., None]

    def at(yy, xx):
        if wrap:
            return img[yy % h, xx % w]
        return img[np.clip(yy, 0, h - 1), np.clip(xx, 0, w - 1)]
    return (at(y0, x0) * (1 - fx) + at(y0, x0 + 1) * fx) * (1 - fy) + \
           (at(y0 + 1, x0) * (1 - fx) + at(y0 + 1, x0 + 1) * fx) * fy


def sample_nearest(img, uv):
    h, w = img.shape[:2]
    x = np.clip((uv[..., 0] * w).astype(np.int64), 0, w - 1)
    y = np.clip((uv[..., 1] * h).astype(np.int64), 0, h - 1)
    return img[y, x]


def resize(a, size, how=Image.BICUBIC):
    """A picture of floats (h, w) or (h, w, K) at another size (w, h)."""
    a = np.asarray(a, np.float32)
    if a.ndim == 2:
        return np.array(Image.fromarray(a, 'F').resize(size, how))
    return np.stack([np.array(Image.fromarray(np.ascontiguousarray(a[..., k]), 'F').resize(size, how))
                     for k in range(a.shape[-1])], -1)


def srgb_to_linear(c):
    c = np.asarray(c, np.float32)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(np.asarray(c, np.float32), 0, None)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)
