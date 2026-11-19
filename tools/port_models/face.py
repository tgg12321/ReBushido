"""A face in relief where the game paints one on a flat front of the head
(tools/port_models/README.md, "The face"): the nose, the brows, the eyes'
sockets, the cheekbones, the lips and the chin raised or sunk under the
game's own painting of them, so that the painted eyes sit in sockets and
the painted nose stands out. The features are found where the painting has
them: the recipe gives each one's texel in the game's atlas, and the
game's own head mesh says where that texel lies on the head. The front of
the head is subdivided twice first, so that a nose has the vertices to
be one; nothing moves by more than the relief's tallest bump (the
silhouette check holds it).
"""
import numpy as np

HEAD = 3


def uv_to_head(ref, ibm, texel, atlas_size):
    """Where on the game's head (in the head bone's frame) the atlas's
    texel lies: the head's triangle whose texture covers it."""
    u, v = (texel[0] + 0.5) / atlas_size[0], (texel[1] + 0.5) / atlas_size[1]
    share = np.abs(ref.terms[:, :, 3])
    head = ref.tjoints[np.arange(len(ref.pos)), share.argmax(1)] == HEAD
    best = None
    for t in ref.tris[np.all(head[ref.tris], 1)]:
        a, b, c = ref.uv[t]
        m = np.array([[b[0] - a[0], c[0] - a[0]], [b[1] - a[1], c[1] - a[1]]])
        if abs(np.linalg.det(m)) < 1e-12:
            continue
        s, r = np.linalg.solve(m, [u - a[0], v - a[1]])
        outside = max(0.0, -s) + max(0.0, -r) + max(0.0, s + r - 1)
        if best is None or outside < best[0]:
            p = ref.pos[t[0]] + s * (ref.pos[t[1]] - ref.pos[t[0]]) + r * (ref.pos[t[2]] - ref.pos[t[0]])
            best = (outside, p)
    p = np.append(best[1], 1.0)
    return (ibm @ p)[:3]


def features(ref, recipe, atlas_size):
    ibm = ref.ibm[HEAD]
    return {k: uv_to_head(ref, ibm, v, atlas_size) for k, v in recipe['texels'].items()}


def bumps(f, recipe):
    """The relief's bumps: (centre, radii along the face's up and across,
    height outwards)."""
    eye_l, eye_r = f['eye_left'], f['eye_right']
    mid = (eye_l + eye_r) / 2
    tip, mouth, chin = f['nose_tip'], f['mouth'], f['chin']
    width = np.linalg.norm(eye_l - eye_r)
    h = recipe.get('depth', 1.0)
    out = []
    # the nose: a ridge from between the eyes to its tip, its tip the most
    # (none where the game's mesh has a nose of its own: `nose` false)
    for k in range(7 if recipe.get('nose', True) else 0):
        t = k / 6.0
        c = mid * (1 - t) + tip * t
        # (the seven overlap about three deep)
        out.append((c, (width * 0.24, width * 0.2 + 0.05 * width * t), (1.6 + 3.2 * t ** 1.5) * h))
    # the nostrils' wings either side of the tip
    side = (eye_l - eye_r) / np.linalg.norm(eye_l - eye_r)
    for s in ((-1, 1) if recipe.get('nose', True) else ()):
        out.append((tip + side * s * width * 0.18 + (mouth - tip) * 0.15, (width * 0.12, width * 0.12), 4.0 * h))
    for eye in (eye_l, eye_r):
        # the socket, the brow above it, the cheekbone below and out
        out.append((eye, (width * 0.2, width * 0.3), -5.0 * h))
        up = (mid - (mouth + chin) / 2)
        up = up / np.linalg.norm(up)
        out.append((eye + up * width * 0.33, (width * 0.12, width * 0.36), 4.5 * h))
        out.append((eye - up * width * 0.45 + (eye - mid) * 0.35, (width * 0.25, width * 0.28), 3.0 * h))
    # the lips, the groove between them, the chin
    out.append((mouth + (tip - mouth) * 0.25, (width * 0.1, width * 0.3), 3.5 * h))
    out.append((mouth - (tip - mouth) * 0.25, (width * 0.1, width * 0.26), 3.0 * h))
    out.append((mouth, (width * 0.035, width * 0.3), -1.5 * h))
    out.append((chin, (width * 0.22, width * 0.3), 3.5 * h))
    return out


def axes(f):
    """The face's own axes in the head's frame: across (from one eye to
    the other), up (from the chin to between the eyes) and out."""
    side = f['eye_left'] - f['eye_right']
    side /= np.linalg.norm(side)
    up = (f['eye_left'] + f['eye_right']) / 2 - f['chin']
    up -= side * (up @ side)
    up /= np.linalg.norm(up)
    out = np.cross(side, up)
    # out of the face, away from the head's middle
    if (f['nose_tip'] - (f['eye_left'] + f['eye_right'] + f['chin']) / 3) @ out < 0:
        out = -out
    return side, up, out


def relief(points, bs, side_axis, up_axis):
    """The height at each point (head frame): the bumps summed, each a
    smooth hill of its centre and radii, measured over the face (up and
    across) and not out."""
    h = np.zeros(len(points))
    for c, (r_up, r_across), height in bs:
        d = points - c
        up = d @ up_axis
        across = d @ side_axis
        q = (up / r_up) ** 2 + (across / r_across) ** 2
        h += height * np.exp(-1.5 * q)
    return h
