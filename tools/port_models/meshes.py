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


def pull_in(mesh, ref, R, limit, inside=None):
    """Where the smoothed body swells beyond the game's own surface by more
    than `limit` units, its vertex is put back to that much outside it,
    the change carried by the term that weighs most, in its bone's frame
    at the bind pose. And where smoothing drew it in under the game's
    surface (a smooth surface lies inside its coarse one, far inside at a
    corner), it is let no deeper than `inside` units, the deeper the less
    of its depth it keeps: the game's outline is held, a corner is
    rounded and not lost."""
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
    if inside:
        deep = np.maximum(-out, 0.0)
        kept = inside * (1.0 - np.exp(-deep / inside))
        delta += N[t] * (deep - kept)[:, None]
    share = np.abs(mesh.terms[:, :, 3])
    k = share.argmax(1)
    rows = np.arange(len(mesh.pos))
    b = mesh.tjoints[rows, k]
    local = np.einsum('nji,nj->ni', R[b], delta)  # R^T delta
    mesh.terms[rows, k, :3] += local
    mesh.pos = mesh.pos + delta
    return int(move.sum()), float(out.max())


def welded(mesh, grid=8):
    """Vertices at one place as one point: each vertex's point, and the
    triangles on points."""
    key = np.round(mesh.pos * grid).astype(np.int64)
    _, weld = np.unique(key, axis=0, return_inverse=True)
    weld = weld.ravel()
    return weld, weld[mesh.tris]


def dense_terms(mesh):
    """Each vertex's sum of bones as a row for every bone (N, 21, 4)."""
    out = np.zeros((len(mesh.pos), fg.BONES, 4))
    rows = np.arange(len(mesh.pos))
    for k in range(mesh.terms.shape[1]):
        np.add.at(out, (rows, mesh.tjoints[:, k]), mesh.terms[:, k])
    return out


def sparse_terms(dense):
    """The rows back as the file's terms: the K bones that carry most
    (the rest, if any, given up and the shares of the place made whole
    again)."""
    # (a bone may carry a vector and none of the place: the game's own
    # vertices at a joint are a place on one bone and a vector on another)
    weight = np.abs(dense[:, :, 3]) * 4096.0 + np.linalg.norm(dense[:, :, :3], axis=2)
    order = np.argsort(-weight, 1)[:, :K]
    terms = np.take_along_axis(dense, order[:, :, None], 1)
    total = terms[:, :, 3].sum(1, keepdims=True)
    terms[:, :, 3] /= np.where(np.abs(total) > 1e-9, total, 1.0)
    used = np.take_along_axis(weight, order, 1) > 1e-9
    return np.where(used[:, :, None], terms, 0.0), np.where(used, order, 0)


def smoother(mesh, R, T, by=1.0, corner=135.0, small=0, most=12.0):
    """The mesh with four triangles for each (in its triangles' order,
    four for one), as a smooth surface through the coarse one's shape
    (Loop's subdivision): a new vertex on each edge and every old vertex
    moved, each to a mean of the vertices round it; its bones stay its
    own (a new vertex's, the mean of its edge's two ends'), so that it
    bends as the game's mesh does. An open
    edge stays an open edge's curve, its sharp corners (under `corner`
    degrees) where they are; an edge more than two faces meet at is kept
    as one. `by` is how much of the smoothing is taken (0: the flat
    faces, cut in four). A piece of `small` triangles or fewer (a strap,
    a plate on a shoulder) is cut in four and not smoothed: so small a
    thing is all corners, and smoothing would only shrink it. No vertex
    is moved more than `most` units."""
    weld, faces = welded(mesh)
    n = int(weld.max()) + 1
    # each point's piece (the points joined by triangles), and how many triangles it has
    parent = np.arange(n)

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a, b, c in faces:
        ra = find(a)
        parent[find(b)] = ra
        parent[find(c)] = ra
    piece = np.array([find(x) for x in range(n)])
    size = np.bincount(piece[faces[:, 0]], minlength=n)
    take = np.where(size[piece] <= small, 0.0, by)
    # each vertex's one bone, or -1: a vertex of a joint's skin, which the
    # game stretches between two bones. An edge between two bones (or to
    # such a vertex) gets no new vertex: no single vertex half way along
    # it moves as the game's stretched triangle does, by the terms or by
    # a tool's plain weights, so the joint's triangles stay the game's own
    # (to be remade with edge loops and weights of their own), and the
    # vertices at their corners stay where they are.
    carried = dense_terms(mesh)
    load = np.abs(carried[:, :, 3]) * 4096.0 + np.linalg.norm(carried[:, :, :3], axis=2)
    one = np.where((load > 1e-6).sum(1) == 1, load.argmax(1), -1)
    heavy = mesh.weights.max(1) > 0.999
    one = np.where(heavy & (mesh.joints[np.arange(len(mesh.pos)), mesh.weights.argmax(1)] == one), one, -1)
    firm = lambda x, y: one[x] >= 0 and one[x] == one[y]  # noqa: E731
    for a, b, c in mesh.tris:
        for x, y in ((a, b), (b, c), (c, a)):
            if not firm(x, y):
                take[weld[x]] = take[weld[y]] = 0.0
    first = np.zeros(n, np.int64)
    first[weld[::-1]] = np.arange(len(weld))[::-1]
    dense = dense_terms(mesh)[first]                       # a point's sum of bones
    edges = {}
    for f, (a, b, c) in enumerate(faces):
        if a == b or b == c or a == c:
            continue
        for x, y, z in ((a, b, c), (b, c, a), (c, a, b)):
            edges.setdefault((min(x, y), max(x, y)), []).append(z)
    sharp = {e for e, opp in edges.items() if len(opp) != 2}
    # the old points
    around = [[] for _ in range(n)]
    rim = [[] for _ in range(n)]
    for (a, b), opp in edges.items():
        around[a].append(b)
        around[b].append(a)
        if (a, b) in sharp:
            rim[a].append(b)
            rim[b].append(a)
    P = np.zeros((n, 3))
    P[weld] = mesh.pos
    moved = dense.copy()
    for v in range(n):
        if not around[v]:
            continue
        if rim[v]:
            if len(rim[v]) != 2:
                continue
            u, w = P[rim[v][0]] - P[v], P[rim[v][1]] - P[v]
            cosang = (u @ w) / max(np.linalg.norm(u) * np.linalg.norm(w), 1e-12)
            if np.degrees(np.arccos(np.clip(cosang, -1, 1))) < corner:
                continue
            new = 0.75 * dense[v] + 0.125 * (dense[rim[v][0]] + dense[rim[v][1]])
        else:
            k = len(around[v])
            beta = 3.0 / 16.0 if k == 3 else 3.0 / (8.0 * k)
            new = (1 - k * beta) * dense[v] + beta * dense[around[v]].sum(0)
        moved[v] = dense[v] + take[v] * (new - dense[v])
    # the new points, one an edge
    edge_point = {}
    for (a, b), opp in edges.items():
        flat = 0.5 * (dense[a] + dense[b])
        if (a, b) in sharp:
            edge_point[(a, b)] = flat
        else:
            new = 0.375 * (dense[a] + dense[b]) + 0.125 * (dense[opp[0]] + dense[opp[1]])
            edge_point[(a, b)] = flat + min(take[a], take[b]) * (new - flat)
    # the vertices: the old ones, then one for each edge of the mesh's own vertices
    count = len(mesh.pos)
    new_rows, index = [], {}
    tris = []
    for a, b, c in mesh.tris:
        mids = []
        for x, y in ((a, b), (b, c), (c, a)):
            key = (min(x, y), max(x, y))
            if not firm(x, y):
                mids.append(key[0])                 # (the triangle's pieces at this edge are then nothing)
                continue
            if key not in index:
                index[key] = count + len(new_rows)
                new_rows.append(key)
            mids.append(index[key])
        ab, bc, ca = mids
        tris += [[a, ab, ca], [ab, b, bc], [ca, bc, c], [ab, bc, ca]]
    i, j = np.array(new_rows).T
    made = np.zeros((len(new_rows), fg.BONES, 4))
    for r, (x, y) in enumerate(new_rows):
        wa, wb = weld[x], weld[y]
        made[r] = edge_point.get((min(wa, wb), max(wa, wb)), 0.5 * (dense[wa] + dense[wb]))
    every = np.concatenate([moved[weld], made])
    # where the smooth surface is, at the bind pose
    place = (np.einsum('bij,nbj->nbi', R, every[:, :, :3]) + every[:, :, 3:4] * T[None]).sum(1)
    # ... and each vertex's bones: its own if it was the game's, the mean
    # of its edge's two ends' if it is new, and no more. (The smooth
    # surface's own means take in the vertices round about, and so the
    # bones of a joint's far side: a thigh's top would be part hip, and
    # fold in where the game's stands firm.) The vertex is carried to its
    # smooth place by those bones, each by its share.
    # (two of the game's vertices at one place may be on different bones:
    # two pieces that meet; each keeps its own)
    own = dense_terms(mesh)
    flat = np.zeros((len(new_rows), fg.BONES, 4))
    for r, (x, y) in enumerate(new_rows):
        flat[r] = 0.5 * (own[x] + own[y])
    plain = np.concatenate([own, flat])
    stood = (np.einsum('bij,nbj->nbi', R, plain[:, :, :3]) + plain[:, :, 3:4] * T[None]).sum(1)
    delta = place - stood
    # no vertex goes far (a rigid piece's rim, drawn towards the joint
    # beside it, would stand where the limb is)
    far = np.linalg.norm(delta, axis=1, keepdims=True)
    delta = delta * np.minimum(1.0, most / np.maximum(far, 1e-9))
    plain[:, :, :3] += plain[:, :, 3:4] * np.einsum('bji,nj->nbi', R, delta)
    terms, tjoints = sparse_terms(plain)
    # the file's plain weights (what a tool without the terms draws by):
    # the game's file's own, a new vertex's the mean of its edge's ends'
    blend = np.zeros((len(mesh.pos), fg.BONES))
    for k in range(mesh.joints.shape[1]):
        np.add.at(blend, (np.arange(len(mesh.pos)), np.clip(mesh.joints[:, k], 0, fg.BONES - 1)), mesh.weights[:, k])
    blend = np.concatenate([blend, (blend[i] + blend[j]) / 2])
    top = np.argsort(-blend, 1)[:, :4]
    wgt = np.take_along_axis(blend, top, 1)
    wgt /= np.maximum(wgt.sum(1, keepdims=True), 1e-9)
    out = Mesh(uv=np.concatenate([mesh.uv, (mesh.uv[i] + mesh.uv[j]) / 2]),
               nrm=np.concatenate([mesh.nrm, (mesh.nrm[i] + mesh.nrm[j]) / 2]),
               joints=np.where(wgt > 0, top, 0), weights=wgt,
               terms=terms, tjoints=tjoints, tris=np.array(tris, np.int64))
    out.pos = (np.einsum('nkij,nkj->nki', R[tjoints], terms[:, :, :3]) + terms[:, :, 3:4] * T[tjoints]).sum(1)
    return out


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
