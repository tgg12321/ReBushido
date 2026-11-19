#!/usr/bin/env python3
"""What a fighter's remake starts from: the game's own mesh with more
polygons (body.py), each of its charts cut for its new texture layout,
for Blender to lay flat (tools/blender/fighter_unwrap.py).

    python tools/port_models/prepare.py --name M101 [--ref tmp/models/ref] [--work tmp/models/work]

Writes WORK/NAME/unwrap_in_CHART.json for each chart of the recipe
(`charts` of fighters.json, and `body` for the rest), and
WORK/NAME/atlas_game.png, the game's texture, to look at.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import body as bd  # noqa: E402
import fighter_glb as fg  # noqa: E402
from recipe import recipe_of  # noqa: E402


def charts_of(recipe):
    """The model's charts, the body's first."""
    return ['body'] + [n for n in recipe.get('charts', {})]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--ref', default='tmp/models/ref')
    ap.add_argument('--work', default='tmp/models/work')
    args = ap.parse_args()
    from assemble import RECTS
    r = recipe_of(args.name)
    out = os.path.join(args.work, args.name)
    os.makedirs(out, exist_ok=True)
    ref = fg.Model(os.path.join(args.ref, args.name + '.glb'))
    mesh, of_triangle, _, said = bd.made(ref, r)
    for name in charts_of(r):
        part = bd.group_mesh(mesh, of_triangle, name)
        # a chart's islands of one material may be larger than the rest (the face's skin)
        larger = None
        for material, by in r.get('larger', {}).get(name, {}).items():
            share = bd.of_material(part, ref, r, material)
            larger = 1.0 + (by - 1.0) * (share >= 0.3)
        cut = bd.write_unwrap(part, os.path.join(out, 'unwrap_in_%s.json' % name), RECTS[name][2] / RECTS[name][3],
                              larger=larger)
        print('prepare: %s: the chart %s: %d triangles, %d edges to cut along'
              % (args.name, name, len(part.tris), len(cut['seams'])))
    with open(os.path.join(out, 'atlas_game.png'), 'wb') as f:
        f.write(ref.base_color_png())
    print('prepare: %s: %d triangles of the game\'s %d' % (args.name, len(mesh.tris), len(ref.tris)))


if __name__ == '__main__':
    main()
