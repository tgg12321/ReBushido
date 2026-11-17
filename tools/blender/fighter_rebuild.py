"""Rebuilds a fighter at a higher definition from the game's own model, in
Blender, with nothing shown (docs/PORT.md, "The fighters' own models").

    blender --background --factory-startup --python tools/blender/fighter_rebuild.py -- \
            IN.glb OUT.glb [--levels 2] [--crease 50] [--texture TEXTURE.png]

What it does, every step a script can do and do faithfully:
  - welds the body into one surface: the game's mesh is one piece per
    bone with the joints' vertices copied between them, and pieces that
    meet at the same place are made one vertex;
  - marks as creases the edges whose faces meet at more than `--crease`
    degrees, the open edges, and with `--uv-creases` the edges of the
    texture's islands, so that the
    subdivision rounds the body and keeps the hard lines of a sleeve, a
    sash or a sandal where the game has them;
  - subdivides (Catmull-Clark, `--levels`, the creases kept), which keeps
    the silhouette within the game's own: a smooth surface through the
    same corners;
  - keeps the 21-bone rig and its weights: the subdivision carries each
    vertex's weights to the new ones by the same rule as their places;
  - smooths the shading, with the creases kept sharp;
  - takes a texture of `--texture` (an upscaled picture of the same atlas,
    tools/fighter_texture.py) if one is given.

What it cannot do well and does not try: a face (the game's are a few
painted quads, and subdividing them only rounds the quads), the folds of
cloth, and anything the game's texture does not already show.

The output is Blender's own glTF; tools/fighter_conform.py makes it a file
the port reads (its joints by name, in the game's units and axes).
"""
import math
import os
import sys

import bmesh
import bpy


def args_after_dashes():
    argv = sys.argv
    return argv[argv.index('--') + 1:] if '--' in argv else []


def option(args, name, default):
    return args[args.index(name) + 1] if name in args else default


def main():
    args = args_after_dashes()
    src, out = os.path.abspath(args[0]), os.path.abspath(args[1])
    levels = int(option(args, '--levels', '2'))
    crease_angle = math.radians(float(option(args, '--crease', '50')))
    texture = option(args, '--texture', None)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src, bone_heuristic='TEMPERANCE')
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    for o in meshes:
        bpy.context.view_layer.objects.active = o
        me = o.data
        bm = bmesh.new()
        bm.from_mesh(me)
        before = len(bm.verts)
        # one surface: what meets at one place is one vertex (half a game
        # unit, the file being in metres: the game's own joints meet to
        # its rounding)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=float(option(args, '--weld', '0.0005')))
        welded = before - len(bm.verts)
        uv = bm.loops.layers.uv.active
        crease = bm.edges.layers.float.get('crease_edge') or bm.edges.layers.float.new('crease_edge')
        creased = 0
        for e in bm.edges:
            hard = False
            if len(e.link_faces) != 2:
                hard = True
            else:
                f0, f1 = e.link_faces
                if f0.normal.angle(f1.normal, 0.0) > crease_angle:
                    hard = True
                elif uv is not None and '--uv-creases' in args:
                    # an edge of a texture island: the two faces do not
                    # agree on its texture points
                    a = [l[uv].uv.copy() for l in e.link_loops]
                    if len(a) == 2:
                        l0, l1 = e.link_loops
                        n0 = l0.link_loop_next[uv].uv
                        n1 = l1.link_loop_next[uv].uv
                        if (l0[uv].uv - n1).length > 1e-4 or (l1[uv].uv - n0).length > 1e-4:
                            hard = True
            if hard:
                e[crease] = 1.0
                e.smooth = False
                creased += 1
        bm.to_mesh(me)
        bm.free()
        for p in me.polygons:
            p.use_smooth = True
        mod = o.modifiers.new('subdivide', 'SUBSURF')
        mod.levels = levels
        mod.render_levels = levels
        mod.use_creases = True
        mod.uv_smooth = 'PRESERVE_BOUNDARIES'
        # the subdivision before the armature: the rig deforms the result
        while o.modifiers[0].name != 'subdivide':
            bpy.ops.object.modifier_move_up(modifier='subdivide')
        bpy.ops.object.modifier_apply(modifier='subdivide')
        print('fighter_rebuild: %s: %d vertices welded, %d edges creased, now %d vertices, %d faces'
              % (o.name, welded, creased, len(me.vertices), len(me.polygons)))
        if texture:
            image = bpy.data.images.load(os.path.abspath(texture))
            for mat in me.materials:
                for node in mat.node_tree.nodes:
                    if node.type == 'TEX_IMAGE':
                        node.image = image
                        node.interpolation = 'Linear'
    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_skins=True,
                              export_all_influences=False, export_yup=True,
                              export_apply=False, export_animations=False)
    print('fighter_rebuild: wrote', out)


main()
