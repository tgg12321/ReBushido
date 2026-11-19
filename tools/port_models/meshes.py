"""A model's arrays as the remake puts them together, and what is done to
them: pieces taken out and added, a piece wholly on one bone, the body
pulled back inside the game's own surface (tools/port_models/README.md).
"""
import numpy as np

import fighter_glb as fg

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


def shift(mesh, delta, R):
    """Each vertex moved by `delta` at the bind pose, every one of its
    bones carrying its share of the move."""
    for k in range(mesh.terms.shape[1]):
        b = mesh.tjoints[:, k]
        local = np.einsum('nji,nj->ni', R[b], delta)  # R^T delta
        mesh.terms[:, k, :3] += local * mesh.terms[:, k, 3:4]
    mesh.pos = mesh.pos + delta


def welded(mesh, grid=8):
    """Vertices at one place as one point: each vertex's point, and the
    triangles on points."""
    key = np.round(mesh.pos * grid).astype(np.int64)
    _, weld = np.unique(key, axis=0, return_inverse=True)
    weld = weld.ravel()
    return weld, weld[mesh.tris]


def smooth(mesh, R, rounds=8, lam=0.5, mu=-0.53, hold=None, most=12.0):
    """The surface made even without shrinking it (a step in, a step out,
    `rounds` times): the lumps a subdivided coarse mesh keeps are taken
    out. An open edge's vertices stay where they are, and those of `hold`;
    none moves more than `most` units (a thin piece is not flattened).
    Returns the most a vertex moved."""
    weld, faces = welded(mesh)
    n = weld.max() + 1
    P = np.zeros((n, 3))
    P[weld] = mesh.pos
    e = np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]])
    e = e[e[:, 0] != e[:, 1]]
    und = np.unique(np.sort(e, 1), axis=0, return_counts=True)
    edges, uses = und
    fixed = np.zeros(n, bool)
    fixed[edges[uses == 1].ravel()] = True
    if hold is not None:
        fixed[weld[hold]] = True
    i = np.concatenate([edges[:, 0], edges[:, 1]])
    j = np.concatenate([edges[:, 1], edges[:, 0]])
    deg = np.bincount(i, minlength=n).astype(float)
    start = P.copy()
    for _ in range(rounds):
        for f in (lam, mu):
            mean = np.zeros((n, 3))
            np.add.at(mean, i, P[j])
            mean /= np.maximum(deg, 1)[:, None]
            step = np.where(fixed[:, None] | (deg == 0)[:, None], 0.0, (mean - P) * f)
            P = P + step
    delta = (P - start)[weld]
    far = np.linalg.norm(delta, axis=1, keepdims=True)
    delta = delta * np.minimum(1.0, most / np.maximum(far, 1e-9))
    shift(mesh, delta, R)
    return float(np.linalg.norm(delta, axis=1).max())


def renormal(mesh, crease=50.0):
    """The normals worked out again: each point's the mean of its faces',
    by their angles there; a vertex the mesh keeps apart from its point's
    others, whose own faces look more than `crease` degrees away, keeps
    its own (a hem's edge). An unlit vertex (a normal of nought) stays
    unlit."""
    weld, faces = welded(mesh)
    P = mesh.pos
    a, b, c = (P[mesh.tris[:, k]] for k in range(3))
    fn = np.cross(b - a, c - a)
    # outward as the old normals had it
    old = mesh.nrm[mesh.tris].sum(1)
    fn *= np.where((fn * old).sum(1) < 0, -1.0, 1.0)[:, None]
    unit = fn / np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-12)
    own = np.zeros_like(P)
    point = np.zeros((weld.max() + 1, 3))
    for k in range(3):
        p0, p1, p2 = P[mesh.tris[:, k]], P[mesh.tris[:, (k + 1) % 3]], P[mesh.tris[:, (k + 2) % 3]]
        u, v = p1 - p0, p2 - p0
        cosang = (u * v).sum(1) / np.maximum(np.linalg.norm(u, axis=1) * np.linalg.norm(v, axis=1), 1e-12)
        ang = np.arccos(np.clip(cosang, -1, 1))
        np.add.at(own, mesh.tris[:, k], unit * ang[:, None])
        np.add.at(point, faces[:, k], unit * ang[:, None])
    own /= np.maximum(np.linalg.norm(own, axis=1, keepdims=True), 1e-12)
    point /= np.maximum(np.linalg.norm(point, axis=1, keepdims=True), 1e-12)
    shared = point[weld]
    keep_own = (own * shared).sum(1) < np.cos(np.radians(crease))
    lit = np.linalg.norm(mesh.nrm, axis=1) > 1e-6
    new = np.where(keep_own[:, None], own, shared)
    mesh.nrm = np.where(lit[:, None], new, mesh.nrm)
