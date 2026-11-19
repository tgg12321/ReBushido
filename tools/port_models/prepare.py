#!/usr/bin/env python3
"""The pieces of a fighter's model that the remake replaces, each in its
own bone's frame, for Blender to model against (tools/blender/fighter_parts.py):
the game's own piece (its shape is what the new one is held to) and the
hd mod's (tools/fighter_hd.py), with their texture points, and the atlas.

    python tools/port_models/prepare.py --name M101 [--ref tmp/models/ref]
            [--hd tmp/mods/hd/models] [--work tmp/models/work]

Writes WORK/NAME/pieces.json and WORK/NAME/atlas_game.png, atlas_hd.png.
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fighter_glb as fg  # noqa: E402
from check import dominant  # noqa: E402
from recipe import recipe_of, main_checkout  # noqa: E402


def piece(m, bone, dom):
    """The triangles wholly on `bone`, in the bone's own frame."""
    sel = dom == bone
    tri = np.all(sel[m.tris], 1)
    used = np.unique(m.tris[tri])
    index = -np.ones(len(m.pos), np.int64)
    index[used] = np.arange(len(used))
    p = np.concatenate([m.pos[used], np.ones((len(used), 1))], 1)
    local = (m.ibm[bone] @ p.T).T[:, :3]
    n = (m.ibm[bone][:3, :3] @ m.nrm[used].T).T
    return {'bone': bone, 'points': local.round(3).tolist(), 'normals': n.round(4).tolist(),
            'uvs': m.uv[used].round(5).tolist(), 'triangles': index[m.tris[tri]].tolist()}


def linear(c):
    """An 8-bit sRGB colour as Blender's linear one."""
    c = np.asarray(c, float) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4).round(4).tolist()


def colors_of(m, dom, r):
    """The hands' colours, from the game's own texture of them: the skin
    (its light, warm texels) and the glove (its dark ones), unless the
    recipe says; the nail a paler skin, the stud a grey."""
    atlas = fg.png_array(m.base_color_png()).astype(float)
    h, w = atlas.shape[:2]
    sel = np.isin(dom, [7, 11])
    uv = m.uv[sel]
    texels = atlas[np.clip((uv[:, 1] * h).astype(int), 0, h - 1), np.clip((uv[:, 0] * w).astype(int), 0, w - 1)]
    # every texel of the hands' triangles, not only their corners
    tri = np.all(sel[m.tris], 1)
    samples = []
    rng = np.random.default_rng(1)
    for t in m.tris[tri]:
        for _ in range(24):
            a, b = rng.random(2)
            if a + b > 1:
                a, b = 1 - a, 1 - b
            p = m.uv[t[0]] + a * (m.uv[t[1]] - m.uv[t[0]]) + b * (m.uv[t[2]] - m.uv[t[0]])
            samples.append(atlas[min(h - 1, int(p[1] * h)), min(w - 1, int(p[0] * w))])
    texels = np.concatenate([texels, np.array(samples)]) if samples else texels
    rgb = texels[:, :3]
    warm = rgb[(rgb[:, 0] > 140) & (rgb[:, 0] > rgb[:, 2] + 40)]
    skin = np.median(warm, 0) if len(warm) else np.array([224, 160, 124])
    dark = rgb[rgb.sum(1) < 150]
    glove = np.array(r['hand']['glove']) if r['hand'].get('glove') else (
        np.median(dark, 0) if len(dark) else np.array([30, 30, 30]))
    nail = skin * 0.75 + np.array([255, 225, 215]) * 0.25
    return {'skin': linear(skin), 'glove': linear(glove), 'nail': linear(nail),
            'stud': linear([150, 150, 156]), 'skin_srgb': skin.round().tolist()}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--ref', default='tmp/models/ref')
    ap.add_argument('--hd', default=None)
    ap.add_argument('--work', default='tmp/models/work')
    ap.add_argument('--motions', default=None, help='the folder of tools/port_models/motions.py')
    args = ap.parse_args()
    hd_dir = args.hd or os.path.join(main_checkout(), 'tmp', 'mods', 'hd', 'models')
    r = recipe_of(args.name)
    r.get('materials', {}).pop('about', None)
    out = os.path.join(args.work, args.name)
    os.makedirs(out, exist_ok=True)
    ref = fg.Model(os.path.join(args.ref, args.name + '.glb'))
    hd = fg.Model(os.path.join(hd_dir, args.name + '.glb'))
    dr, dh = dominant(ref), dominant(hd)
    R, T = ref.bind()
    pieces = {}
    for bone in sorted(set(r['replace'].values())):
        pieces[str(bone)] = {'game': piece(ref, bone, dr), 'hd': piece(hd, bone, dh),
                             'bind_rotation': R[bone].tolist(), 'bind_place': T[bone].tolist()}
    colors = colors_of(ref, dr, r)
    with open(os.path.join(out, 'pieces.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump({'name': args.name, 'recipe': r, 'pieces': pieces, 'colors': colors}, f)
    # the body, cut for its new texture layout (tools/blender/fighter_unwrap.py)
    import body as bd
    from assemble import RECTS
    mesh, said = bd.body_mesh(ref, hd, r)
    cut = bd.write_unwrap(mesh, os.path.join(out, 'unwrap_in.json'), RECTS['body'][2] / RECTS['body'][3])
    print('prepare: the body: %d triangles (%d of the hd mod\'s left out), %d edges to cut along'
          % (len(mesh.tris), said['removed'], len(cut['seams'])))
    # the head as the game has it, seen from its middle (head.py), and
    # the fighter standing (the first frame of a fight of his: where the
    # neck is a neck, which at the bind pose it is not)
    if 'face' in r:
        import head
        np.savez(os.path.join(out, 'head_shells.npz'), **head.shells(ref, hd))
    clips = fg.motion_files(args.motions, args.name) if args.motions else []
    if clips:
        own = [c for c in clips if c.endswith('.0.motion.txt')] or clips
        _, Rs, Ts = fg.read_motion(own[0])[0]
        with open(os.path.join(out, 'stance.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump({'from': os.path.basename(os.path.dirname(own[0])), 'R': Rs.tolist(), 'T': Ts.tolist()}, f)
    with open(os.path.join(out, 'atlas_game.png'), 'wb') as f:
        f.write(ref.base_color_png())
    with open(os.path.join(out, 'atlas_hd.png'), 'wb') as f:
        f.write(hd.base_color_png())
    print('prepare: %s: %s' % (args.name, ', '.join('bone %s: game %d, hd %d triangles'
                                                     % (b, len(p['game']['triangles']), len(p['hd']['triangles']))
                                                     for b, p in pieces.items())))


if __name__ == '__main__':
    main()
