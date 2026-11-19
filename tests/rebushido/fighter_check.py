"""The model pipeline's checks (tools/port_models/check.py): a model that
keeps the game's skeleton, weights, proportions and outline passes, and
each way of breaking one of them is caught

No C here and nothing of the game: the models are boxes on the game's 21
bones, made in this file, posed through a motion made here too. Needs
numpy and Pillow, as the pipeline does; skipped where they are not."""
import json
import os
import sys

import support

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'port_models'))

BONES = 21
# where each made-up bone is at the bind pose, and the box it carries
# (bone, centre, half sizes): a body, a head, two arms
PLACES = {b: (0.0, -100.0 * (b % 5), 40.0 * (b // 5)) for b in range(BONES)}
BOXES = [(1, (0.0, 0.0, 0.0), (120.0, 200.0, 60.0)), (3, (0.0, -120.0, 0.0), (60.0, 70.0, 60.0)),
         (7, (90.0, 0.0, 0.0), (90.0, 25.0, 25.0)), (11, (-90.0, 0.0, 0.0), (90.0, 25.0, 25.0))]


def box(centre, half, segments=4):
    """A box's vertices and outward triangles, each face a grid."""
    import numpy as np
    pts, tris = [], []
    for axis in range(3):
        for sign in (-1, 1):
            u, v = [a for a in range(3) if a != axis]
            base = len(pts)
            for i in range(segments + 1):
                for j in range(segments + 1):
                    p = [0.0, 0.0, 0.0]
                    p[axis] = sign
                    p[u] = -1 + 2 * i / segments
                    p[v] = -1 + 2 * j / segments
                    pts.append(p)
            for i in range(segments):
                for j in range(segments):
                    a = base + i * (segments + 1) + j
                    q = [a, a + segments + 1, a + segments + 2, a + 1]
                    t1, t2 = [q[0], q[1], q[2]], [q[0], q[2], q[3]]
                    # outward: the corners round the face's normal
                    n = [0, 0, 0]
                    n[axis] = sign
                    pa, pb, pc = (np.array(pts[k]) for k in t1)
                    if np.cross(pb - pa, pc - pa) @ np.array(n) < 0:
                        t1, t2 = t1[::-1], t2[::-1]
                    tris += [t1, t2]
    pts = np.array(pts) * np.array(half) + np.array(centre)
    return pts, np.array(tris)


def model(path, stretch=None, weight=1.0, moved_bone=None, dense=4):
    """A model file of the boxes, each wholly on its bone, written by the
    pipeline's own writer; `stretch` (bone, units) makes a bone's box
    longer along x, `weight` the weight of the first vertex, `moved_bone`
    a bone whose bind place is moved."""
    import numpy as np
    import fighter_glb as fg
    pos, nrm, joints, tris = [], [], [], []
    for bone, centre, half in BOXES:
        half = list(half)
        if stretch and stretch[0] == bone:
            half[0] += stretch[1]
        p, t = box(centre, half, dense)
        p = p + np.array(PLACES[bone])
        tris.append(t + sum(len(x) for x in pos))
        pos.append(p)
        nrm.append((p - np.array(PLACES[bone]) - np.array(centre)) /
                   np.maximum(np.linalg.norm(p - np.array(PLACES[bone]) - np.array(centre), axis=1,
                                             keepdims=True), 1e-9))
        joints.append(np.full(len(p), bone))
    pos, nrm, joints, tris = (np.concatenate(x) for x in (pos, nrm, joints, tris))
    n = len(pos)
    w = fg.GlbWriter()
    jw = np.zeros((n, 4), np.int64)
    jw[:, 0] = joints
    ww = np.zeros((n, 4))
    ww[:, 0] = 1.0
    ww[0, 0] = weight
    attributes = {'POSITION': w.array(pos, 5126, 'VEC3', 34962, minmax=True),
                  'NORMAL': w.array(nrm, 5126, 'VEC3', 34962),
                  'TEXCOORD_0': w.array(np.zeros((n, 2)), 5126, 'VEC2', 34962),
                  'JOINTS_0': w.array(jw, 5121, 'VEC4', 34962),
                  'WEIGHTS_0': w.array(ww, 5126, 'VEC4', 34962)}
    ibm = []
    for b in range(BONES):
        m = np.eye(4)
        m[:3, 3] = -np.array(PLACES[b])
        if b == moved_bone:
            m[0, 3] += 5.0
        ibm.append(m.T.reshape(16))
    a_ibm = w.array(np.array(ibm), 5126, 'MAT4')
    nodes = [{'name': 'bone%02d' % b} for b in range(BONES)] + [{'name': 'body', 'mesh': 0, 'skin': 0}]
    gltf = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': list(range(BONES + 1))}],
            'nodes': nodes, 'skins': [{'joints': list(range(BONES)), 'inverseBindMatrices': a_ibm}],
            'meshes': [{'primitives': [{'attributes': attributes,
                                        'indices': w.array(tris.reshape(-1, 1), 5125, 'SCALAR', 34963)}]}],
            'extras': {'rb_fighter': 1}}
    w.write(path, gltf)
    return fg.Model(path)


def motion(folder, name):
    """A motion of a few frames: the head and the arms turned about."""
    import numpy as np
    run = os.path.join(folder, 'made-up')
    os.makedirs(run, exist_ok=True)
    with open(os.path.join(run, name + '.0.motion.txt'), 'w', encoding='ascii', newline='\n') as f:
        for frame in range(6):
            words = [str(frame)]
            for b in range(BONES):
                a = 0.3 * frame if b in (3, 7, 11) else 0.0
                c, s = np.cos(a), np.sin(a)
                r = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
                words += ['%d' % round(x * 4096) for x in r.ravel()]
                words += ['%d' % round(x) for x in np.array(PLACES[b]) + [1000.0, 0.0, 500.0]]
            f.write(' '.join(words) + '\n')


def outcome(new, ref, folder, limits):
    import check
    rep = check.Report()
    check.print = lambda *a, **k: None
    check.check_skeleton(new, ref, limits, rep)
    check.check_weights(new, ref, limits, rep)
    check.check_proportions(new, ref, limits, rep)
    frames = check.frames_of(folder, 'M999', 1)
    check.silhouette(new, ref, frames, limits, limits['silhouette_tolerance_units'], rep, None, '')
    check.check_self_intersection(new, ref, frames, limits, rep)
    check.check_budgets([new], limits, rep)
    return rep.failed, len(frames)


def run(checks):
    # tests/random.py is a test, and Pillow's own import of the standard
    # `random` must not find it: tests/ is left off the path meanwhile
    tests = os.path.abspath(os.path.join(HERE, '..'))
    saved = sys.path[:]
    sys.path[:] = [p for p in sys.path if os.path.abspath(p or '.') != tests]
    try:
        import random  # noqa: F401
        import tempfile  # noqa: F401
        import numpy  # noqa: F401
        import PIL.Image  # noqa: F401
        import PIL.ImageDraw  # noqa: F401
    except ImportError:
        raise support.Skip('numpy or Pillow not installed (the model pipeline needs them)')
    finally:
        sys.path[:] = saved
    import shutil
    folder = os.path.join(support.WORK, 'fighter_check.%d' % os.getpid())
    os.makedirs(folder, exist_ok=True)
    try:
        checks_of(checks, folder)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def checks_of(checks, folder):
    motion(folder, 'M999')
    with open(os.path.join(HERE, '..', '..', 'tools', 'port_models', 'limits.json'), encoding='utf-8') as f:
        limits = json.load(f)
    path = lambda n: os.path.join(folder, n + '.glb')  # noqa: E731
    ref = model(path('ref'))
    failed, frames = outcome(model(path('same')), ref, folder, limits)
    checks.equal('poses made up', frames, 6)
    checks.equal('the same model passes every check', failed, [])
    # finer, and a little larger within the tolerance: still passes
    failed, _ = outcome(model(path('finer'), stretch=(3, 8.0), dense=8), ref, folder, limits)
    checks.equal('a finer model 8 units larger passes', failed, [])
    failed, _ = outcome(model(path('long_arm'), stretch=(7, 60.0)), ref, folder, limits)
    checks.true('an arm 60 units longer fails proportions', 'proportions' in failed)
    checks.true('... and the silhouette', 'silhouette' in failed)
    failed, _ = outcome(model(path('light'), weight=0.5), ref, folder, limits)
    checks.true('a vertex half weighted fails the weights', 'weights' in failed)
    failed, _ = outcome(model(path('moved'), moved_bone=3), ref, folder, limits)
    checks.true('a bone\'s bind place moved fails the skeleton', 'skeleton' in failed)
    small = dict(limits, budgets=dict(limits['budgets'], lod0={'triangles': 100, 'vertices': 100000}))
    failed, _ = outcome(model(path('heavy')), ref, folder, small)
    checks.equal('more triangles than the budget fails the budget alone', failed, ['budget_lod0'])
    # the body's box folded through itself: the middle of its back pushed
    # out through its front
    import numpy as np
    folded = model(path('folded'), dense=6)
    body = folded.joints[:, 0] == 1
    p = folded.pos - np.array(PLACES[1])
    back = body & (p[:, 2] < -59) & (np.abs(p[:, 0]) < 70) & (np.abs(p[:, 1]) < 120)
    # (aslant, so that no edge meets the front exactly on one of its own)
    push = np.array([17.0, 11.0, 150.0])
    folded.pos[back] += push
    folded.terms[back, 0, :3] += push
    failed, _ = outcome(folded, ref, folder, limits)
    checks.true('a surface pushed through itself fails self-intersection', 'self_intersection' in failed)
    checks.true('(numbers)', bool(np.isfinite(folded.pos).all()))
