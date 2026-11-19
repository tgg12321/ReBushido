"""Close pictures of one part of a fighter's model, with nothing shown: a
row of views about the vertices one bone (or several, `bone03+bone02`)
carries, at the file's bind pose (tools/port_models/README.md).

    blender --background --factory-startup --python tools/blender/fighter_look.py -- \
            MODEL.glb OUT.png BONE [SPAN_METRES] [VIEWS] [SIZE] [flat] [FRONT UP]

VIEWS is a list of front, side, side2, back, three, three2, backthree,
top, low, bottom; `flat` draws the colours alone, unlit. FRONT and UP
are axes of the (first) bone's own frame, as `+x -z` (the head's: its
face looks along x and its crown is at -z), so that the part is seen
upright whatever the bind pose does with it; without them the views are
the file's own.
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
path, out, bone = argv[0], os.path.abspath(argv[1]), argv[2]
span = float(argv[3]) if len(argv) > 3 else 0.36
views = (argv[4] if len(argv) > 4 else 'front,three,side,back,top').split(',')
res = int(argv[5]) if len(argv) > 5 else 600
flat = len(argv) > 6 and argv[6] == 'flat'
axes = argv[7:9] if len(argv) > 8 else None
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=path)
scene = bpy.context.scene
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
pts = []
for o in meshes:
    gi = [o.vertex_groups[n].index for n in bone.split('+') if n in o.vertex_groups]
    for v in o.data.vertices:
        if any(g.group in gi and g.weight > 0.5 for g in v.groups):
            pts.append(o.matrix_world @ v.co)
lo = Vector([min(p[k] for p in pts) for k in range(3)])
hi = Vector([max(p[k] for p in pts) for k in range(3)])
c = (lo + hi) / 2
if flat:
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'FLAT'
    scene.display.shading.color_type = 'TEXTURE'
else:
    scene.render.engine = 'BLENDER_EEVEE_NEXT'
    scene.eevee.taa_render_samples = 16
scene.view_settings.view_transform = 'Standard'


def sun(rot, energy):
    d = bpy.data.lights.new('s', 'SUN')
    d.energy = energy
    o = bpy.data.objects.new('s', d)
    o.rotation_euler = [math.radians(a) for a in rot]
    scene.collection.objects.link(o)


sun((50, 0, 35), 3.0)
sun((60, 0, -60), 1.2)
sun((110, 0, 180), 1.5)
sun((60, 0, 150), 1.0)
w = bpy.data.worlds.new('w')
w.use_nodes = True
w.node_tree.nodes['Background'].inputs[0].default_value = (0.25, 0.26, 0.28, 1)
scene.world = w
cam_d = bpy.data.cameras.new('c')
cam_d.type = 'ORTHO'
cam_d.ortho_scale = span
cam = bpy.data.objects.new('c', cam_d)
scene.collection.objects.link(cam)
scene.camera = cam
dirs = {'front': (0, 1, 0), 'side': (-1, 0, 0), 'side2': (1, 0, 0), 'back': (0, -1, 0),
        'three': (-0.6, 0.8, 0.1), 'three2': (0.6, 0.8, 0.1), 'top': (0, 0.001, 1),
        'backthree': (-0.6, -0.8, 0.1), 'low': (0, 0.8, -0.6), 'bottom': (0, 0.001, -1)}
scene.render.resolution_x = scene.render.resolution_y = res


def bone_frame():
    """The part's front, left and up in Blender's space, from the bone's
    world matrix at the bind pose (the inverse of the file's inverse bind
    matrix)."""
    import json
    import struct
    from mathutils import Matrix
    with open(path, 'rb') as f:
        data = f.read()
    length = struct.unpack_from('<I', data, 12)[0]
    gltf = json.loads(data[20:20 + length])
    at = 20 + ((length + 3) & ~3)
    blob = data[at + 8:]
    skin = gltf['skins'][0]
    acc = gltf['accessors'][skin['inverseBindMatrices']]
    view = gltf['bufferViews'][acc['bufferView']]
    start = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    j = [gltf['nodes'][n].get('name') for n in skin['joints']].index(bone.split('+')[0])
    m = struct.unpack_from('<16f', blob, start + 64 * j)
    world = Matrix(((m[0], m[4], m[8]), (m[1], m[5], m[9]), (m[2], m[6], m[10]))).inverted()
    to_blender = Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0)))  # the game's y down to z up

    def axis(text):
        v = Vector([float(text[1] == a) for a in 'xyz']) * (-1 if text[0] == '-' else 1)
        return (to_blender @ (world @ v)).normalized()
    front, up = axis(axes[0]), axis(axes[1])
    return front, up.cross(front).normalized(), up


if axes:
    F, L, U = bone_frame()
shots = []
for v in views:
    d = Vector(dirs[v]).normalized()
    up = Vector((0, 0, 1))
    if axes:
        # the views' own right is -x, front +y, up +z
        d = (-L * d.x + F * d.y + U * d.z).normalized()
        up = U
    cam.location = c + d * span
    cam_d.clip_start = 0.001
    cam_d.clip_end = span * 1.9
    z = d
    x = up.cross(z).normalized()
    y = z.cross(x)
    from mathutils import Matrix as _M
    cam.rotation_euler = _M((x, y, z)).transposed().to_euler()
    scene.render.filepath = out + '.' + v + '.png'
    bpy.ops.render.render(write_still=True)
    shots.append(scene.render.filepath)
imgs = []
for p in shots:
    im = bpy.data.images.load(p)
    a = np.empty(res * res * 4, np.float32)
    im.pixels.foreach_get(a)
    imgs.append(a.reshape(res, res, 4))
    os.remove(p)
canvas = np.concatenate(imgs, 1)
big = bpy.data.images.new('sheet', canvas.shape[1], canvas.shape[0], alpha=False)
big.pixels.foreach_set(canvas.ravel())
big.filepath_raw = out
big.file_format = 'PNG'
big.save()
print('fighter_look: wrote', out)
