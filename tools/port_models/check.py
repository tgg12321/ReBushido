#!/usr/bin/env python3
"""Holds a fighter's new model to the game's own: what may never change,
because the hitboxes are the balance (docs/PORT.md, "The fighters' own
models": no hitbox comes from a model, but a model that is not where the
hitboxes are lies to the player). Fails (exit 1) on any of:

- skeleton: the file's 21 joints and its bind pose, the game's exactly
  (the inverse bind matrices of the reference, within `skeleton_units`);
- weights: every vertex wholly on the bones (its weights and its terms'
  shares of the place summing to one), no bone the game's mesh moves left
  without a vertex on it;
- proportions: for each bone, the vertices it carries most of, measured
  in that bone's own frame (its length, its width and depth), within
  `proportion_units` of the game's: a limb, a hand, a foot or the head
  no longer or wider than the game's;
- silhouette: the model posed on the game's own bones through every
  motion recorded (tools/port_models/motions.py), from the front, the
  side and above, laid over the game's own model posed the same: what of
  the new outline lies more than `silhouette_tolerance_units` outside the
  game's, or what of the game's outline the new one leaves uncovered by
  as much, counted; the frame, the view and the bone nearest each stray
  are reported, and the worst is drawn;
- self-intersection: faces of one connected surface passing through one
  another (a fold, a crushed elbow), at the bind pose and in the frames
  of the motions that bend the most;
- budgets: triangles, vertices and texture of each level of detail, and
  four fighters at once.

    python tools/port_models/check.py NEW.glb --reference REF.glb
            [--motions tmp/models/ref/motions] [--limits tools/port_models/limits.json]
            [--report FOLDER] [--lod NEW.lod1.glb ...]

Needs numpy and Pillow (Windows Python). Nothing of the game is written
into the repository: both models are read from folders git ignores.
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fighter_glb as fg  # noqa: E402

BONE_NAMES = ['root', 'body', 'chest', 'head', 'shoulder_a', 'upper_arm_a', 'forearm_a',
              'hand_a', 'shoulder_b', 'upper_arm_b', 'forearm_b', 'hand_b', 'hips', 'thigh_a',
              'shin_a', 'foot_a', 'thigh_b', 'shin_b', 'foot_b', 'extra_19', 'extra_20']
VIEWS = (('front', (0, 1)), ('side', (2, 1)), ('above', (0, 2)))


class Report:
    def __init__(self):
        self.lines = []
        self.failed = []
        self.numbers = {}

    def say(self, text):
        self.lines.append(text)
        print(text)

    def check(self, name, ok, text):
        self.say('%s %s: %s' % ('ok  ' if ok else 'FAIL', name, text))
        if not ok:
            self.failed.append(name)


# ---- skeleton and weights ----

def check_skeleton(new, ref, limits, rep):
    missing = [BONE_NAMES[b] for b in range(fg.BONES) if ref.has_joint[b] and not new.has_joint[b]]
    d = np.abs(new.ibm - ref.ibm)
    place = float(d[:, :3, 3].max())
    turn = float(d[:, :3, :3].max())
    rep.numbers['skeleton'] = {'place': place, 'turn': turn}
    rep.check('skeleton', not missing and place <= limits['skeleton_units'] and turn <= limits['skeleton_turn'],
              '21 joints%s, bind pose within %.3f units and %.5f of a turn of the game\'s'
              % (' (missing %s)' % ', '.join(missing) if missing else '', place, turn))


def check_weights(new, ref, limits, rep):
    tol = limits['weight_sum']
    wsum = new.weights.sum(1)
    hsum = new.terms[:, :, 3].sum(1)
    bad_w = int((np.abs(wsum - 1) > tol).sum())
    bad_h = int((np.abs(hsum - 1) > tol).sum())
    bad_j = int(((new.joints < 0) | (new.joints >= fg.BONES))[new.weights > 1e-6].sum())
    nan = int((~np.isfinite(new.pos)).sum() + (~np.isfinite(new.terms)).sum())

    def used(m):
        u = np.zeros(fg.BONES)
        np.add.at(u, m.tjoints.ravel(), np.abs(m.terms[:, :, 3]).ravel())
        return u

    lost = [BONE_NAMES[b] for b in range(fg.BONES)
            if used(ref)[b] > limits['bone_used_weight'] and used(new)[b] <= limits['bone_used_weight']]
    rep.numbers['weights'] = {'off_sum': bad_w, 'off_place': bad_h, 'bad_joint': bad_j, 'nan': nan}
    rep.check('weights', bad_w == 0 and bad_h == 0 and bad_j == 0 and nan == 0 and not lost,
              '%d vertices: %d whose weights do not sum to one, %d whose terms do not hold the whole place, '
              '%d on no bone, %d not numbers; bones the game moves left unweighted: %s'
              % (len(new.pos), bad_w, bad_h, bad_j, nan, ', '.join(lost) or 'none'))


def dominant(m, alone=0.95):
    """The bone that carries each vertex almost wholly (-1 for a vertex
    shared between bones, whose piece is the joint's and not a bone's)."""
    share = np.abs(m.terms[:, :, 3])
    k = share.argmax(1)
    top = share[np.arange(len(m.pos)), k] / np.maximum(share.sum(1), 1e-9)
    return np.where(top >= alone, m.tjoints[np.arange(len(m.pos)), k], -1)


def extents(m, b, dom):
    sel = dom == b
    if sel.sum() == 0:
        return None
    p = np.concatenate([m.pos[sel], np.ones((sel.sum(), 1))], 1)
    local = (m.ibm[b] @ p.T).T[:, :3]
    return local.min(0), local.max(0), int(sel.sum())


def check_proportions(new, ref, limits, rep):
    dn, dr = dominant(new), dominant(ref)
    worst = (0.0, None)
    rows = []
    for b in range(fg.BONES):
        e_ref = extents(ref, b, dr)
        e_new = extents(new, b, dn)
        if e_ref is None or e_ref[2] < limits['proportion_min_vertices']:
            continue
        if e_new is None:
            rows.append('%s: no vertex of the new model' % BONE_NAMES[b])
            worst = (1e9, BONE_NAMES[b])
            continue
        # how much longer, wider or deeper the new piece is than the game's,
        # each way along each of the bone's axes
        grow = np.maximum(e_ref[0] - e_new[0], e_new[1] - e_ref[1])
        g = float(grow.max())
        rows.append('%s %+.0f %+.0f %+.0f' % (BONE_NAMES[b], grow[0], grow[1], grow[2]))
        if g > worst[0]:
            worst = (g, BONE_NAMES[b])
    rep.numbers['proportions'] = {'worst_units': worst[0], 'bone': worst[1]}
    rep.check('proportions', worst[0] <= limits['proportion_units'],
              'each bone\'s piece no larger than the game\'s by more than %.0f units: the most %.1f (%s)'
              % (limits['proportion_units'], worst[0], worst[1]))
    rep.say('     beyond the game\'s piece along the bone, across, deep: ' + '; '.join(rows))


# ---- silhouettes ----

def rasterize(points2, tris, origin, upp, size):
    from PIL import Image, ImageDraw
    img = Image.new('L', size, 0)
    draw = ImageDraw.Draw(img)
    q = (points2 - origin) / upp
    for t in tris:
        draw.polygon([tuple(q[t[0]]), tuple(q[t[1]]), tuple(q[t[2]])], fill=255)
    return np.array(img) > 0


def grow_mask(mask, r):
    """The mask grown by r pixels (a square)."""
    out = mask.copy()
    for axis in (0, 1):
        acc = out.copy()
        for s in range(1, r + 1):
            acc[tuple(slice(s, None) if a == axis else slice(None) for a in (0, 1))] |= \
                out[tuple(slice(None, -s) if a == axis else slice(None) for a in (0, 1))]
            acc[tuple(slice(None, -s) if a == axis else slice(None) for a in (0, 1))] |= \
                out[tuple(slice(s, None) if a == axis else slice(None) for a in (0, 1))]
        out = acc
    return out


def frames_of(motions, name, every):
    out = []
    for path in fg.motion_files(motions, name):
        frames = fg.read_motion(path)
        last = None
        for k, (serial, R, T) in enumerate(frames):
            if k % every:
                continue
            key = np.round(np.concatenate([R.ravel() * 64, (T - T[1]).ravel() / 4]))
            if last is not None and np.array_equal(key, last):
                continue  # the same pose as the last one taken
            last = key
            out.append(('%s:%d' % (os.path.basename(os.path.dirname(path)), serial), R, T))
    return out


def silhouette(new, ref, frames, limits, tol_units, rep, report_dir, label):
    upp = limits['silhouette_units_per_pixel']
    tol = max(1, int(round(tol_units / upp)))
    worst = None
    total_out = total_in = 0
    strays = {}
    for name, R, T in frames:
        pn, pr = new.pose(R, T), ref.pose(R, T)
        for view, (a, b) in VIEWS:
            pts_r, pts_n = pr[:, [a, b]], pn[:, [a, b]]
            lo = np.minimum(pts_r.min(0), pts_n.min(0)) - upp * (tol + 4)
            hi = np.maximum(pts_r.max(0), pts_n.max(0)) + upp * (tol + 4)
            size = tuple(int(x) for x in np.ceil((hi - lo) / upp))
            mr = rasterize(pts_r, ref.tris, lo, upp, size)
            mn = rasterize(pts_n, new.tris, lo, upp, size)
            out = mn & ~grow_mask(mr, tol)
            inside = mr & ~grow_mask(mn, tol)
            n_out, n_in = int(out.sum()), int(inside.sum())
            total_out += n_out
            total_in += n_in
            stray = n_out + n_in
            area = max(1, int(mr.sum()))
            if stray:
                # the bone nearest each stray pixel
                ys, xs = np.nonzero(out | inside)
                where = np.stack([xs, ys], 1) * upp + lo
                joints = T[:, [a, b]]
                near = np.argmin(((where[:, None, :] - joints[None]) ** 2).sum(2), 1)
                for j in near:
                    strays[BONE_NAMES[j]] = strays.get(BONE_NAMES[j], 0) + 1
            share = stray / area
            if worst is None or stray > worst[0]:
                worst = (stray, share, name, view, n_out, n_in, mr, mn, out, inside)
    limit_px = limits['silhouette_stray_pixels']
    limit_share = limits['silhouette_stray_share']
    ok = worst is None or (worst[0] <= limit_px or worst[1] <= limit_share)
    places = ', '.join('%s %d' % kv for kv in sorted(strays.items(), key=lambda kv: -kv[1])[:6])
    rep.numbers['silhouette' + label] = {'frames': len(frames), 'worst_pixels': worst[0] if worst else 0,
                                         'worst_share': worst[1] if worst else 0,
                                         'outside_total': total_out, 'uncovered_total': total_in}
    if worst is None:
        rep.check('silhouette' + label, False, 'no frame of any motion to pose the model through')
        return
    rep.check('silhouette' + label, ok,
              '%d poses x 3 views at %d units a pixel, tolerance %d units: the worst %s from the %s, %d pixels '
              'outside the game\'s outline and %d of it uncovered (%.2f%% of it; at most %d pixels or %.2f%%); '
              'all strays %d out, %d uncovered, nearest %s'
              % (len(frames), upp, tol_units, worst[2], worst[3], worst[4], worst[5], 100 * worst[1], limit_px,
                 100 * limit_share, total_out, total_in, places or 'none'))
    if report_dir:
        from PIL import Image
        mr, mn, out, inside = worst[6:]
        pic = np.zeros(mr.shape + (3,), np.uint8)
        pic[mr] = (90, 90, 90)
        pic[mn & mr] = (170, 170, 170)
        pic[mn & ~mr] = (120, 160, 220)
        pic[out] = (255, 40, 40)
        pic[inside] = (255, 220, 0)
        path = os.path.join(report_dir, 'silhouette%s_worst.png' % label)
        Image.fromarray(pic).save(path)
        rep.say('     the worst drawn: %s (grey the game\'s, blue the new beyond it within tolerance, red beyond '
                'it, yellow the game\'s uncovered)' % path)


# ---- self-intersection ----

def shells(m):
    """Each vertex's connected surface, vertices at one place being one."""
    key = np.round(m.pos * 4).astype(np.int64)
    _, weld = np.unique(key, axis=0, return_inverse=True)
    weld = weld.ravel()
    parent = np.arange(weld.max() + 1)

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for t in weld[m.tris]:
        a, b, c = find(t[0]), find(t[1]), find(t[2])
        parent[b] = a
        parent[find(c)] = a
    roots = np.array([find(x) for x in range(len(parent))])
    return roots[weld], weld


def segment_hits(p0, p1, a, b, c):
    """Whether each segment p0-p1 crosses triangle abc (all (K, 3))."""
    e1, e2 = b - a, c - a
    d = p1 - p0
    h = np.cross(d, e2)
    det = (e1 * h).sum(1)
    ok = np.abs(det) > 1e-9
    inv = np.where(ok, 1.0 / np.where(ok, det, 1), 0)
    s = p0 - a
    u = (s * h).sum(1) * inv
    q = np.cross(s, e1)
    v = (d * q).sum(1) * inv
    t = (e2 * q).sum(1) * inv
    eps = 1e-6
    return ok & (u > eps) & (v > eps) & (u + v < 1 - eps) & (t > eps) & (t < 1 - eps)


def self_intersections(m, P, shell_of, weld):
    """The triangles of one surface that pass through another triangle of
    the same surface sharing no corner with it."""
    tris = m.tris
    shell = shell_of[tris[:, 0]]
    A, B, C = P[tris[:, 0]], P[tris[:, 1]], P[tris[:, 2]]
    lo = np.minimum(np.minimum(A, B), C)
    hi = np.maximum(np.maximum(A, B), C)
    cell = max(float(np.median((hi - lo).max(1))) * 2, 1.0)
    clo = np.floor(lo / cell).astype(np.int64)
    chi = np.floor(hi / cell).astype(np.int64)
    entries = {}
    for i in range(len(tris)):
        for x in range(clo[i, 0], chi[i, 0] + 1):
            for y in range(clo[i, 1], chi[i, 1] + 1):
                for z in range(clo[i, 2], chi[i, 2] + 1):
                    entries.setdefault((x, y, z, int(shell[i])), []).append(i)
    pairs = []
    for members in entries.values():
        if len(members) < 2:
            continue
        idx = np.array(members)
        iu, ju = np.triu_indices(len(idx), 1)
        pairs.append(np.stack([idx[iu], idx[ju]], 1))
    if not pairs:
        return 0, 0
    pairs = np.unique(np.concatenate(pairs), axis=0)
    i, j = pairs[:, 0], pairs[:, 1]
    # boxes that overlap, and no corner shared
    over = np.all((lo[i] <= hi[j]) & (lo[j] <= hi[i]), 1)
    wi, wj = weld[tris[i]], weld[tris[j]]
    shared = (wi[:, :, None] == wj[:, None, :]).any((1, 2))
    keep = over & ~shared
    i, j = i[keep], j[keep]
    hit = np.zeros(len(i), bool)
    for (s, e) in ((0, 1), (1, 2), (2, 0)):
        Ti, Tj = tris[i], tris[j]
        hit |= segment_hits(P[Ti[:, s]], P[Ti[:, e]], P[Tj[:, 0]], P[Tj[:, 1]], P[Tj[:, 2]])
        hit |= segment_hits(P[Tj[:, s]], P[Tj[:, e]], P[Ti[:, 0]], P[Ti[:, 1]], P[Ti[:, 2]])
    involved = np.unique(np.concatenate([i[hit], j[hit]]))
    return int(hit.sum()), len(involved)


def bent_poses(m, frames, limits):
    R0, T0 = m.bind()
    poses = [('bind', R0, T0)]
    if frames:
        # the poses farthest from the bind pose: the most bent
        bend = [float(np.abs(R - R[1:2]).sum()) for _, R, _ in frames]
        for k in np.argsort(bend)[::-1][:limits['self_intersection_frames']]:
            poses.append(frames[k])
    return poses


def worst_intersection(m, poses):
    shell_of, weld = shells(m)
    worst = (0, 0, None)
    for name, R, T in poses:
        pairs, count = self_intersections(m, m.pose(R, T), shell_of, weld)
        if count > worst[1]:
            worst = (pairs, count, name)
    return worst, shell_of


def reference_share(ref, frames, limits):
    worst, _ = worst_intersection(ref, bent_poses(ref, frames, limits))
    return worst[1] / max(1, len(ref.tris))


def check_self_intersection(new, ref, frames, limits, rep):
    poses = bent_poses(new, frames, limits)
    worst, shell_of = worst_intersection(new, poses)
    share = worst[1] / max(1, len(new.tris))
    rep.numbers['self_intersection'] = {'pairs': worst[0], 'triangles': worst[1], 'share': share}
    ref_share = reference_share(ref, frames, limits)
    allowed = max(limits['self_intersection_share'], ref_share * limits.get('self_intersection_of_game', 1.0))
    rep.numbers['self_intersection']['game_share'] = ref_share
    rep.check('self_intersection', share <= allowed,
              '%d surfaces, %d poses (the bind pose and the most bent): at worst %d triangles (%.3f%%) pass through '
              'their own surface%s; the game\'s own model %.3f%%; at most %.3f%%'
              % (len(np.unique(shell_of)), len(poses), worst[1], 100 * share,
                 ' (%s)' % worst[2] if worst[2] else '', 100 * ref_share, 100 * allowed))


# ---- budgets ----

def texture_pixels(m):
    from PIL import Image
    import io
    total = 0
    for data in m.images():
        w, h = Image.open(io.BytesIO(data)).size
        total += w * h
    return total


def check_budgets(models, limits, rep):
    b = limits['budgets']
    for level, m in enumerate(models):
        lb = b.get('lod%d' % level)
        if lb is None:
            continue
        ok = len(m.tris) <= lb['triangles'] and len(m.pos) <= lb['vertices']
        rep.check('budget_lod%d' % level, ok, '%d triangles (at most %d), %d vertices (at most %d)'
                  % (len(m.tris), lb['triangles'], len(m.pos), lb['vertices']))
    px = texture_pixels(models[0])
    # four fighters, every picture of each with its mipmaps (a third more)
    four_bytes = int(4 * px * 4 * 4 / 3)
    four_tris = 4 * len(models[0].tris)
    rep.numbers['budget'] = {'triangles': len(models[0].tris), 'texture_pixels': px,
                             'four_triangles': four_tris, 'four_texture_bytes': four_bytes}
    rep.check('budget_texture', px <= b['texture_pixels'], '%d texels in all its pictures (at most %d)'
              % (px, b['texture_pixels']))
    rep.check('budget_four_fighters', four_tris <= b['four_fighters_triangles'] and
              four_bytes <= b['four_fighters_texture_bytes'],
              'four at once: %d triangles (at most %d), %.0f MB of textures with mipmaps (at most %.0f)'
              % (four_tris, b['four_fighters_triangles'], four_bytes / 1e6, b['four_fighters_texture_bytes'] / 1e6))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('model')
    ap.add_argument('--reference', required=True, help='the game\'s own model (tools/fighter_export.py)')
    ap.add_argument('--motions', default=None, help='the folder of tools/port_models/motions.py')
    ap.add_argument('--limits', default=os.path.join(HERE, 'limits.json'))
    ap.add_argument('--lod', action='append', default=[], help='a lower level of detail, in order')
    ap.add_argument('--report', default=None, help='a folder for report.txt, numbers.json and the worst pictures')
    ap.add_argument('--quick', action='store_true', help='every fourth of the frames the limits say')
    args = ap.parse_args()
    with open(args.limits, encoding='utf-8') as f:
        limits = json.load(f)
    if args.report:
        os.makedirs(args.report, exist_ok=True)
    rep = Report()
    new = fg.Model(args.model)
    ref = fg.Model(args.reference)
    name = os.path.splitext(os.path.basename(args.reference))[0]
    rep.say('check %s against the game\'s %s' % (args.model, name))
    check_skeleton(new, ref, limits, rep)
    check_weights(new, ref, limits, rep)
    check_proportions(new, ref, limits, rep)
    every = limits['silhouette_every'] * (4 if args.quick else 1)
    frames = frames_of(args.motions, name, every) if args.motions else []
    silhouette(new, ref, frames, limits, limits['silhouette_tolerance_units'], rep, args.report, '')
    check_self_intersection(new, ref, frames, limits, rep)
    lods = [new]
    tols = limits['lod_silhouette_tolerance_units']
    for level, path in enumerate(args.lod, 1):
        m = fg.Model(path)
        lods.append(m)
        check_skeleton(m, ref, limits, rep)
        check_weights(m, ref, limits, rep)
        silhouette(m, ref, frames[::4], limits, tols[min(level, len(tols) - 1)], rep, args.report,
                   '_lod%d' % level)
    check_budgets(lods, limits, rep)
    rep.say('%s: %s' % (name, 'every check passes' if not rep.failed else 'FAILED: ' + ', '.join(rep.failed)))
    if args.report:
        with open(os.path.join(args.report, 'report.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(rep.lines) + '\n')
        with open(os.path.join(args.report, 'numbers.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(rep.numbers, f, indent=1)
    return 1 if rep.failed else 0


if __name__ == '__main__':
    sys.exit(main())
