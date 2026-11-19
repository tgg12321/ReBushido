#!/usr/bin/env python3
"""A fighter's remade model put together as the port's file
(src/port/gl/gl_fighter_file.h), with its levels of detail and the
pictures the lighting reads (tools/port_models/README.md):

- the game's own mesh with more polygons, its facets' edges rounded,
  every vertex the game's own sum of bones (body.py);
- one texture of charts (charts.py), each chart's rectangle its own, in
  a layout where nothing lies over anything else: the game's texture
  cleaned and made sharp, a normal map and an
  occlusion-roughness-metal map (glTF's normalTexture, occlusionTexture
  and metallicRoughnessTexture: R occlusion, G roughness, B metal), and
  the colour tables' picture (rb_palette) in the same layout, so that
  the game's tints and fades reach it as they reach the game's own.

    python tools/port_models/assemble.py --name M101 [--work tmp/models/work]
            [--ref tmp/models/ref] [--out tmp/mods/remade/models]

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
from meshes import K, Mesh  # noqa: E402
from recipe import recipe_of  # noqa: E402

# each chart's rectangle of a texture 2048 texels square: the head has
# more texels to a unit than the body (a face is looked at)
RECTS = {
    'body': (0, 0, 2048, 1280),
    'face': (0, 1280, 1024, 768),
    'head': (1024, 1280, 704, 768),
    'cloth': (1728, 1280, 128, 128),
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
    # (the metal factor is nought: a viewer that reads the factor and
    # fails to read the map then draws the colour, not a black metal; the
    # port's lighting reads the map's own blue)
    material = {'name': 'rb_remade', 'pbrMetallicRoughness': {
        'baseColorTexture': {'index': tex('rb_albedo')}, 'metallicFactor': 0.0, 'roughnessFactor': 1.0,
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


def join(charts, size):
    """The charts' pieces as one mesh, their texture points the atlas's."""
    out = None
    for c in charts:
        if c.mesh is None:
            continue
        m = Mesh(**{k: v for k, v in c.mesh.__dict__.items()
                    if k in ('pos', 'nrm', 'uv', 'joints', 'weights', 'terms', 'tjoints', 'tris')})
        m.uv = c.to_atlas(m.uv, size)
        out = m if out is None else out.plus(m)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--work', default='tmp/models/work')
    ap.add_argument('--ref', default='tmp/models/ref')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    from prepare import charts_of
    r = recipe_of(args.name)
    work = os.path.join(args.work, args.name)
    ref = fg.Model(os.path.join(args.ref, args.name + '.glb'))
    size = r['atlas']
    cloth, moved = bd.cloth_chart(ref, r, rect_of('cloth', size))

    def pieces(level):
        """The model's charts at a level of detail (the pictures are the
        first level's: the lower ones are meshes alone, in its layout)."""
        ch.LEVEL = level
        charts = []
        for name in charts_of(r) + list(r.get('kept', {})):
            c, said = bd.chart(ref, r, name, os.path.join(work, 'unwrap_out_%s.json' % name), rect_of(name, size))
            charts.append(c)
            print('assemble: level %d: the chart %s: %s' % (level, name, json.dumps(said)))
        if cloth is not None and level == 0:
            charts.append(cloth)
        ch.LEVEL = 0
        return charts
    charts = pieces(0)
    mesh = join(charts, size)
    color, normal, orm, entry = ch.compose(charts, size)
    images = [('rb_albedo', fg.png_bytes(color)), ('rb_normal', fg.png_bytes(normal)),
              ('rb_orm', fg.png_bytes(orm))]
    extras = dict(ref.extras)
    extras['rb_remade'] = {'recipe': r.get('about', ''), 'charts': {c.name: list(c.rect) for c in charts},
                           'maps': 'rb_albedo, rb_normal (tangent space, OpenGL), rb_orm (glTF: R occlusion, '
                                   'G roughness, B metal), rb_palette_index, all in one layout'}
    if 'rb_palette' in extras:
        images.append(('rb_palette_index', fg.png_bytes(entry)))
    if cloth is not None:
        extras['rb_cloth'] = moved(extras['rb_cloth'], size)
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, args.name + '.glb')
    write(path, mesh, ref, images, extras)
    print('assemble: %s: %d vertices, %d triangles' % (path, len(mesh.pos), len(mesh.tris)))
    lod_dir = os.path.join(os.path.dirname(os.path.abspath(args.out)), 'lod')
    os.makedirs(lod_dir, exist_ok=True)
    for level in (1, 2):
        mesh = join(pieces(level), size)
        path = os.path.join(lod_dir, '%s.lod%d.glb' % (args.name, level))
        write(path, mesh, ref, images, extras)
        print('assemble: %s: %d vertices, %d triangles' % (path, len(mesh.pos), len(mesh.tris)))
    from PIL import Image
    for name, pic in (('albedo', color), ('normal', normal), ('orm', orm)):
        Image.fromarray(pic).save(os.path.join(work, 'atlas_%s.png' % name))


if __name__ == '__main__':
    main()
