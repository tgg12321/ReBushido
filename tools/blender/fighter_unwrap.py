"""A new texture layout for a surface, with nothing shown: the surface cut
along the seams given, each island laid flat with as little stretch as
may be, all at one scale, and packed into a rectangle
(tools/port_models/README.md, "The maps").

    blender --background --factory-startup --python tools/blender/fighter_unwrap.py -- IN.json OUT.json

IN.json: `points` (each a place), `faces` (each three points), `seams`
(each two points: an edge to cut along), `aspect` (the rectangle's width
over its height), `margin` (between islands, a share of the rectangle).
OUT.json: `uvs`, for each face its three corners' places in the
rectangle (0..1 both ways, v down the picture), and `island`, each
face's island.
"""
import json
import sys

import bmesh
import bpy

argv = sys.argv[sys.argv.index('--') + 1:]
with open(argv[0], encoding='utf-8') as f:
    data = json.load(f)
aspect = float(data.get('aspect', 1.0))
margin = float(data.get('margin', 0.004))

bpy.ops.wm.read_factory_settings(use_empty=True)
mesh = bpy.data.meshes.new('surface')
mesh.from_pydata(data['points'], [], data['faces'])
mesh.update()
obj = bpy.data.objects.new('surface', mesh)
bpy.context.scene.collection.objects.link(obj)
bpy.context.view_layer.objects.active = obj
obj.select_set(True)
assert len(mesh.polygons) == len(data['faces']), 'a face was not made'

seams = {(min(a, b), max(a, b)) for a, b in data['seams']}
for e in mesh.edges:
    a, b = e.vertices
    e.use_seam = (min(a, b), max(a, b)) in seams
mesh.uv_layers.new(name='UVMap')

bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.unwrap(method='ANGLE_BASED', margin=0.001)
bpy.ops.uv.select_all(action='SELECT')
bpy.ops.uv.average_islands_scale()
bpy.ops.object.mode_set(mode='OBJECT')

# the islands narrowed by the rectangle's shape, packed into a square
# without being turned, and widened again: packed into the rectangle
uv = mesh.uv_layers.active.data
for loop in uv:
    loop.uv.x /= aspect
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.select_all(action='SELECT')
bpy.ops.uv.pack_islands(rotate=False, margin=margin, margin_method='FRACTION', shape_method='CONCAVE')

bm = bmesh.from_edit_mesh(mesh)
layer = bm.loops.layers.uv.verify()
# each face's island: the faces joined across edges that are not seams
bm.faces.ensure_lookup_table()
island = [-1] * len(bm.faces)
count = 0
for f in bm.faces:
    if island[f.index] >= 0:
        continue
    stack = [f]
    island[f.index] = count
    while stack:
        g = stack.pop()
        for e in g.edges:
            if e.seam:
                continue
            for o in e.link_faces:
                if island[o.index] < 0:
                    island[o.index] = count
                    stack.append(o)
    count += 1
uvs = [None] * len(bm.faces)
lo = [1e9, 1e9]
hi = [-1e9, -1e9]
for f in bm.faces:
    order = {l.vert.index: l[layer].uv for l in f.loops}
    row = [[order[v].x, order[v].y] for v in data['faces'][f.index]]
    uvs[f.index] = row
    for x, y in row:
        lo = [min(lo[0], x), min(lo[1], y)]
        hi = [max(hi[0], x), max(hi[1], y)]
bpy.ops.object.mode_set(mode='OBJECT')
# v down the picture
uvs = [[[x, 1.0 - y] for x, y in row] for row in uvs]
with open(argv[1], 'w', encoding='utf-8', newline='\n') as f:
    json.dump({'uvs': uvs, 'island': island}, f)
print('fighter_unwrap: %d faces, %d islands, u %.3f..%.3f, v %.3f..%.3f' % (len(uvs), count, lo[0], hi[0], lo[1], hi[1]))
