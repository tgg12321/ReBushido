#!/usr/bin/env python3
"""A fighter's remade model put together as the port's file
(src/port/gl/gl_fighter_file.h), with its levels of detail and the
pictures the lighting reads (tools/port_models/README.md):

- the body (body.py): the hd mod's, less the pieces made anew, held
  inside the game's own surface, in a texture layout of its own and
  painted anew;
- the head (head.py) and the hands (hands.py), made whole;
- one texture of charts (charts.py), each piece's rectangle its own: the
  colour, a normal map and an occlusion-roughness-metal map (glTF's
  normalTexture, occlusionTexture and metallicRoughnessTexture: R
  occlusion, G roughness, B metal), and the colour tables' picture
  (rb_palette) in the same layout, so that the game's tints and fades
  reach every piece.

    python tools/port_models/assemble.py --name M101 [--work tmp/models/work]
            [--ref tmp/models/ref] [--hd tmp/models/ref/hd2/models] [--out tmp/mods/remade/models]

Writes OUT/NAME.glb, and OUT/../lod/NAME.lod1.glb and .lod2.glb.
"""
import argparse
import copy
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import body as bd  # noqa: E402
import charts as ch  # noqa: E402
import fighter_glb as fg  # noqa: E402
from check import dominant  # noqa: E402
from meshes import K, Mesh, outward, winding_sign  # noqa: E402
from recipe import recipe_of  # noqa: E402

# each chart's rectangle of a texture 2048 texels square
RECTS = {
    'body': (0, 0, 2048, 1280),
    'face': (0, 1280, 768, 768),
    'hair': (768, 1280, 640, 768),
    'hand_a': (1408, 1280, 320, 384),
    'hand_b': (1408, 1664, 320, 384),
    'cloth': (1728, 1280, 128, 128),
    'neck': (1728, 1408, 320, 128),
    'ears': (1856, 1280, 192, 128),
    'spare': (1728, 1536, 320, 512),
}


def rect_of(name, size):
    return tuple(int(v * size // 2048) for v in RECTS[name])


def write(path, mesh, template, images, extras):
    """The file: one skin of the 21 bones (the template's nodes and inverse
    bind matrices), one primitive, its material's colour, normals and
    occlusion-roughness-metal, and the colour tables' picture."""
    g = template.gltf
    w = fg.GlbWriter()
    nrm = mesh.nrm / np.maximum(np.linalg.norm(mesh.nrm, axis=1, keepdims=True), 1e-9)
    nrm[np.linalg.norm(mesh.nrm, axis=1) < 1e-6] = 0
    attributes = {
        'POSITION': w.array(mesh.pos, 5126, 'VEC3', 34962, minmax=True),
        'NORMAL': w.array(nrm, 5126, 'VEC3', 34962),
        'TEXCOORD_0': w.array(mesh.uv, 5126, 'VEC2', 34962),
        'JOINTS_0': w.array(mesh.joints, 5121, 'VEC4', 34962),
        'WEIGHTS_0': w.array(mesh.weights, 5126, 'VEC4', 34962),
        '_RB_TJOINTS': w.array(mesh.tjoints[:, :4], 5121, 'VEC4', 34962),
        '_RB_TJOINTS1': w.array(mesh.tjoints[:, 4:], 5121, 'VEC4', 34962),
    }
    for k in range(K):
        attributes['_RB_TERM%d' % k] = w.array(mesh.terms[:, k], 5126, 'VEC4', 34962)
    indices = w.array(mesh.tris.reshape(-1, 1), 5125, 'SCALAR', 34963)
    ibm = np.stack([template.ibm[b].T.reshape(16) for b in range(fg.BONES)])
    a_ibm = w.array(ibm, 5126, 'MAT4')
    imgs = []
    for name, data in images:
        entry = {'bufferView': w.view(data), 'mimeType': 'image/png'}
        if name:
            entry['name'] = name
        imgs.append(entry)
    names = [n for n, _ in images]
    tex = lambda name: names.index(name)  # noqa: E731
    material = {'name': 'rb_remade', 'pbrMetallicRoughness': {
        'baseColorTexture': {'index': tex('rb_albedo')}, 'metallicFactor': 1.0, 'roughnessFactor': 1.0,
        'metallicRoughnessTexture': {'index': tex('rb_orm')}},
        'normalTexture': {'index': tex('rb_normal')}, 'occlusionTexture': {'index': tex('rb_orm')},
        'alphaMode': 'MASK', 'alphaCutoff': 0.5}
    extras = dict(extras)
    if 'rb_palette_index' in names:
        extras['rb_palette'] = dict(extras['rb_palette'], image=tex('rb_palette_index'))
    gltf = {
        'asset': {'version': '2.0', 'generator': 'ReBushido tools/port_models/assemble.py'},
        'scene': 0, 'scenes': copy.deepcopy(g['scenes']), 'nodes': copy.deepcopy(g['nodes']),
        'skins': [{'joints': g['skins'][0]['joints'], 'skeleton': g['skins'][0].get('skeleton', 1),
                   'inverseBindMatrices': a_ibm}],
        'meshes': [{'name': extras.get('model_file', 'fighter'),
                    'primitives': [{'attributes': attributes, 'indices': indices, 'material': 0}]}],
        'materials': [material],
        'textures': [{'source': i, 'sampler': 0} for i in range(len(imgs))],
        'samplers': [{'magFilter': 9729, 'minFilter': 9987, 'wrapS': 33071, 'wrapT': 33071}],
        'images': imgs, 'extras': extras,
    }
    # the skin's joints are the bones in order, so JOINTS_0 is the bone
    for j, node in enumerate(gltf['skins'][0]['joints']):
        assert gltf['nodes'][node]['name'] == 'bone%02d' % j
    w.write(path, gltf)


def blend_of_terms(mesh):
    """The file's plain weights (what a tool without the terms reads) from
    the terms: the four bones with most of the place."""
    share = np.abs(mesh.terms[:, :, 3])
    dense = np.zeros((len(mesh.pos), fg.BONES))
    for k in range(mesh.terms.shape[1]):
        np.add.at(dense, (np.arange(len(mesh.pos)), mesh.tjoints[:, k]), share[:, k])
    order = np.argsort(-dense, 1)[:, :4]
    wgt = np.take_along_axis(dense, order, 1)
    wgt /= np.maximum(wgt.sum(1, keepdims=True), 1e-9)
    mesh.joints = np.where(wgt > 0, order, 0)
    mesh.weights = wgt


def join(charts, size, sign):
    """The charts' pieces as one mesh, their texture points the atlas's."""
    out = None
    for c in charts:
        if c.mesh is None:
            continue
        m = Mesh(**{k: v for k, v in c.mesh.__dict__.items()
                    if k in ('pos', 'nrm', 'uv', 'joints', 'weights', 'terms', 'tjoints', 'tris')})
        m.uv = c.to_atlas(m.uv, size)
        outward(m, sign)
        out = m if out is None else out.plus(m)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--work', default='tmp/models/work')
    ap.add_argument('--ref', default='tmp/models/ref')
    ap.add_argument('--hd', default='tmp/models/ref/hd2/models')
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', default='', help='of body,head,hands: the others left out (to look at one)')
    args = ap.parse_args()
    r = recipe_of(args.name)
    work = os.path.join(args.work, args.name)
    ref = fg.Model(os.path.join(args.ref, args.name + '.glb'))
    hd = fg.Model(os.path.join(args.hd, args.name + '.glb'))
    size = r['atlas']
    sign = winding_sign(ref)
    only = [x for x in args.only.split(',') if x]
    charts = []
    if not only or 'body' in only:
        body, said = bd.chart(ref, hd, r, os.path.join(work, 'unwrap_out.json'), rect_of('body', size))
        charts.append(body)
        print('assemble: the body: %s' % json.dumps(said))
    cloth, moved = bd.cloth_chart(ref, hd, rect_of('cloth', size))
    if cloth is not None:
        charts.append(cloth)
    if not only or 'head' in only:
        import head
        made, said = head.charts(ref, hd, r, work, {k: rect_of(k, size) for k in ('face', 'hair', 'neck', 'ears')})
        charts += made
        print('assemble: the head: %s' % json.dumps(said))
    if not only or 'hands' in only:
        import hands
        made, said = hands.charts(ref, hd, r, work, {k: rect_of(k, size) for k in ('hand_a', 'hand_b')})
        charts += made
        print('assemble: the hands: %s' % json.dumps(said))
    # a new piece's texels answer to the colour tables' entries of the
    # game's own piece, nearest in colour
    dom = dominant(ref)
    for c in charts:
        bones = getattr(c, 'bones', None)
        if bones and not c.entry_on.any():
            colors, entries = ch.piece_entries(ref, dom, bones)
            c.entry, c.entry_on = ch.entries_by_color(c.color, c.on, colors, entries)
    mesh = join(charts, size, sign)
    color, normal, orm, entry = ch.compose(charts, size)
    images = [('rb_albedo', fg.png_bytes(color)), ('rb_normal', fg.png_bytes(normal)),
              ('rb_orm', fg.png_bytes(orm))]
    extras = dict(hd.extras)
    extras.pop('rb_hd', None)
    extras['rb_remade'] = {'recipe': r.get('about', ''), 'charts': {c.name: list(c.rect) for c in charts},
                           'maps': 'rb_albedo, rb_normal (tangent space, OpenGL), rb_orm (glTF: R occlusion, '
                                   'G roughness, B metal), rb_palette_index, all in one layout'}
    if 'rb_palette' in extras:
        images.append(('rb_palette_index', fg.png_bytes(entry)))
    if cloth is not None:
        extras['rb_cloth'] = moved(extras['rb_cloth'], size)
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, args.name + '.glb')
    write(path, mesh, hd, images, extras)
    print('assemble: %s: %d vertices, %d triangles' % (path, len(mesh.pos), len(mesh.tris)))
    from PIL import Image
    for name, pic in (('albedo', color), ('normal', normal), ('orm', orm)):
        Image.fromarray(pic).save(os.path.join(work, 'atlas_%s.png' % name))


if __name__ == '__main__':
    main()
