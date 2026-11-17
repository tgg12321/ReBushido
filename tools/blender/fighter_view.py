"""Renders a fighter's glTF in its bind pose from the front and the side,
with nothing shown: a check that a file the exporter or the rebuild wrote
opens in Blender and looks like the fighter.

    blender --background --factory-startup --python tools/blender/fighter_view.py -- \
            IN.glb OUT.png [--size 640]

Blender is the portable build in the main checkout's tmp/tools/
(tools/blender/README.md).
"""
import math
import os
import sys

import bpy
from mathutils import Vector


def args_after_dashes():
    argv = sys.argv
    return argv[argv.index('--') + 1:] if '--' in argv else []


def main():
    args = args_after_dashes()
    src, out = os.path.abspath(args[0]), os.path.abspath(args[1])
    size = int(args[args.index('--size') + 1]) if '--size' in args else 640
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    deps = bpy.context.evaluated_depsgraph_get()
    for o in meshes:
        for v in o.data.vertices:
            w = o.matrix_world @ v.co
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
    middle = (lo + hi) / 2
    height = max(hi - lo)
    print('fighter_view: %d meshes, bounds %s to %s' % (len(meshes), tuple(round(c, 3) for c in lo),
                                                         tuple(round(c, 3) for c in hi)))
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'TEXTURE'
    scene.render.resolution_x = size * 2
    scene.render.resolution_y = size
    scene.render.film_transparent = False
    world = bpy.data.worlds.new('w')
    scene.world = world
    cam_data = bpy.data.cameras.new('cam')
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = height * 2.3
    cam = bpy.data.objects.new('cam', cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    # two views side by side: the fighter and a copy turned a quarter
    for o in list(scene.objects):
        if o.parent is None and o is not cam:
            o.location.x -= middle.x + height * 0.55
            o.location.y -= middle.y
            o.location.z -= middle.z
    bpy.ops.object.select_all(action='DESELECT')
    for o in list(scene.objects):
        if o is not cam:
            o.select_set(True)
    bpy.context.view_layer.objects.active = [o for o in scene.objects if o is not cam][0]
    bpy.ops.object.duplicate()
    for o in bpy.context.selected_objects:
        if o.parent is None:
            o.location.x += height * 1.1
            o.rotation_mode = 'XYZ'
            o.rotation_euler.z += math.pi / 2
    cam.location = (0.0, -height * 4, 0.0)
    cam.rotation_euler = (math.pi / 2, 0.0, 0.0)
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print('fighter_view: wrote', out)


main()
