"""A Blender scene of fighters on the game's own skeleton, played through
the game's own motions, with nothing shown: the model pipeline's review
(tools/port_models/README.md).

    blender --background --factory-startup --python tools/blender/fighter_scene.py -- \
            --model A.glb [--model B.glb ...] [--motion FILE.motion.txt] \
            [--frames FROM:TO:STEP] [--views front,side,three] [--size 900] \
            [--engine eevee|workbench] [--out FOLDER] [--blend FILE.blend] [--sheet NAME.png]

Each model is imported with its armature (the joints `bone00` to `bone20`,
at the game's bind pose, which the file's inverse bind matrices are), the
models side by side, a fighter's width apart. A motion
(tools/port_models/motions.py) is keyed on every armature: each bone's
pose is the game's world matrix of the bone times the file's inverse bind
matrix, taken into Blender's space, which is exactly the matrix the port
gives a joint; the fighter's place over the ground is kept, his walk
across the stage taken off. Without a motion the models stand in their
bind pose. Pictures of the frames asked, from each view, go into the
folder; `--sheet` lays them out as one picture; `--blend` keeps the scene
to open by hand.
"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

BONES = 21


def args_of():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    out = {'model': [], 'motion': None, 'frames': None, 'views': 'front,side,three', 'size': 900,
           'engine': 'eevee', 'out': None, 'blend': None, 'sheet': None, 'labels': None}
    i = 0
    while i < len(argv):
        key = argv[i][2:]
        value = argv[i + 1]
        if key == 'model':
            out['model'].append(os.path.abspath(value))
        else:
            out[key] = value
        i += 2
    return out


def read_motion(path):
    frames = []
    with open(path, encoding='ascii') as f:
        for line in f:
            v = line.split()
            if len(v) != 1 + BONES * 12:
                continue
            bones = []
            for b in range(BONES):
                a = [float(x) for x in v[1 + 12 * b:13 + 12 * b]]
                R = [[a[0] / 4096, a[1] / 4096, a[2] / 4096],
                     [a[3] / 4096, a[4] / 4096, a[5] / 4096],
                     [a[6] / 4096, a[7] / 4096, a[8] / 4096]]
                bones.append(Matrix((R[0] + [a[9]], R[1] + [a[10]], R[2] + [a[11]], [0, 0, 0, 1])))
            frames.append(bones)
    return frames


# glTF's root node of a fighter's file: the game's y down and units to
# Blender's z up and metres (y up and metres in glTF, which the importer
# turns to z up)
GAME_TO_GLTF = Matrix.Diagonal((0.001, -0.001, -0.001, 1.0))
GLTF_TO_BLENDER = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
GAME_TO_BLENDER = GLTF_TO_BLENDER @ GAME_TO_GLTF


def import_model(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    arm = next(o for o in new if o.type == 'ARMATURE')
    meshes = [o for o in new if o.type == 'MESH']
    tops = [o for o in new if o.parent is None]
    return arm, meshes, tops


def bind_of(arm):
    """Each bone's game world matrix at the bind pose: the inverse of the
    file's inverse bind matrix, read back from the rest pose."""
    out = {}
    world = arm.matrix_world
    for b in arm.data.bones:
        if b.name.startswith('bone'):
            out[int(b.name[4:])] = world @ b.matrix_local
    return out


def key_motion(arm, frames, bind_game, offset):
    """Keys every frame: a bone's armature-space matrix such that its
    deformation is the port's joint matrix (world of the bone times the
    inverse bind)."""
    rest = bind_of(arm)
    inv_arm = arm.matrix_world.inverted()
    # the fighter's own frame at the clip's start: his root bone's turn and
    # place taken off, so that he faces the front and stands at the middle
    shift = frames[0][0].inverted()
    for f, bones in enumerate(frames):
        for pb in arm.pose.bones:
            if not pb.name.startswith('bone'):
                continue
            b = int(pb.name[4:])
            joint = shift @ bones[b] @ bind_game[b].inverted()
            joint_b = GAME_TO_BLENDER @ joint @ GAME_TO_BLENDER.inverted()
            pb.matrix = inv_arm @ joint_b @ rest[b]
            bpy.context.view_layer.update()
            pb.keyframe_insert('location', frame=f + 1)
            pb.keyframe_insert('rotation_quaternion', frame=f + 1)
            pb.keyframe_insert('scale', frame=f + 1)


def bounds(meshes):
    deps = bpy.context.evaluated_depsgraph_get()
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for o in meshes:
        e = o.evaluated_get(deps)
        for v in e.to_mesh().vertices:
            w = o.matrix_world @ v.co
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
        e.to_mesh_clear()
    return lo, hi


def head_middles(meshes):
    """The middle of each model's head (the vertices bone03 carries most
    of), posed."""
    deps = bpy.context.evaluated_depsgraph_get()
    out = []
    for o in meshes:
        group = o.vertex_groups.get('bone03')
        if group is None:
            continue
        carried = [v.index for v in o.data.vertices
                   if any(g.group == group.index and g.weight > 0.5 for g in v.groups)]
        if not carried:
            continue
        e = o.evaluated_get(deps)
        m = e.to_mesh()
        total = Vector()
        for i in carried:
            total += o.matrix_world @ m.vertices[i].co
        e.to_mesh_clear()
        out.append(total / len(carried))
    return out


def lights(scene):
    def sun(name, rot, energy, color=(1, 1, 1)):
        d = bpy.data.lights.new(name, 'SUN')
        d.energy = energy
        d.color = color
        o = bpy.data.objects.new(name, d)
        o.rotation_euler = [math.radians(a) for a in rot]
        scene.collection.objects.link(o)
    sun('key', (50, 0, 35), 3.2, (1.0, 0.96, 0.9))
    sun('fill', (60, 0, -60), 1.0, (0.8, 0.85, 1.0))
    sun('rim', (110, 0, 180), 2.0)
    world = bpy.data.worlds.new('w')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs[0].default_value = (0.05, 0.055, 0.065, 1)
    world.node_tree.nodes['Background'].inputs[1].default_value = 1.0
    scene.world = world


def main():
    a = args_of()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    fighters = []
    for path in a['model']:
        arm, meshes, tops = import_model(path)
        fighters.append((path, arm, meshes, tops))
    motion = read_motion(a['motion']) if a['motion'] else None
    if motion:
        lo_f, hi_f, step = (int(x) for x in (a['frames'] or '0:%d:1' % len(motion)).split(':'))
        clip = motion[lo_f:hi_f]
        for path, arm, meshes, tops in fighters:
            key_motion(arm, clip, game_bind_of_file(path), None)
        frames = list(range(1, len(clip) + 1, step))
        scene.frame_start, scene.frame_end = 1, len(clip)
    else:
        frames = [1]
    if a['engine'] == 'workbench':
        scene.render.engine = 'BLENDER_WORKBENCH'
        scene.display.shading.light = 'STUDIO'
        scene.display.shading.color_type = 'TEXTURE'
    else:
        scene.render.engine = 'BLENDER_EEVEE_NEXT'
        scene.eevee.taa_render_samples = 32
        scene.view_settings.view_transform = 'Standard'
        lights(scene)
    size = int(a['size'])
    count = len(fighters)
    cam_data = bpy.data.cameras.new('cam')
    cam_data.type = 'ORTHO'
    cam = bpy.data.objects.new('cam', cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    # where each view looks from (the fighter faces +y at the clip's start)
    views = {'front': (0, 1, 0.05), 'side': (-1, 0, 0.05), 'three': (-0.7, 0.75, 0.2),
             'back': (0, -1, 0.05), 'face': (0, 1, 0.0), 'face_side': (-1, 0, 0.0),
             'face_three': (-0.6, 0.8, 0.05)}
    out = os.path.abspath(a['out']) if a['out'] else None
    if out:
        os.makedirs(out, exist_ok=True)
    every = [m for f in fighters for m in f[2]]
    homes = [[o.location.copy() for o in f[3]] for f in fighters]
    gap = 1.25 if count > 1 else 0.0
    written = []
    for f in frames:
        scene.frame_set(f)
        for view in a['views'].split(','):
            d = Vector(views[view]).normalized()
            right = Vector((0, 0, 1)).cross(d).normalized()
            # the models side by side across the view
            for k, fighter in enumerate(fighters):
                for o, home in zip(fighter[3], homes[k]):
                    o.location = home + right * (k - (count - 1) / 2) * (gap if not view.startswith("face") else 0.42)
            bpy.context.view_layer.update()
            lo, hi = bounds(every)
            middle = (lo + hi) / 2
            if view.startswith('face'):
                heads = head_middles(every)
                look = sum(heads, Vector()) / len(heads) if heads else Vector((middle.x, middle.y, hi.z - 0.15))
                tall = 0.36
                wide = 0.42 * (count - 1) + 0.4
            else:
                look = middle
                tall = (hi.z - lo.z) + 0.15
                wide = abs((hi - lo).dot(right)) + 0.3
            aspect = max(0.4, min(3.0, wide / tall))
            scene.render.resolution_y = size
            scene.render.resolution_x = int(size * aspect)
            cam_data.ortho_scale = max(wide, tall)
            cam.location = look + d * 6.0
            cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
            if out:
                scene.render.filepath = os.path.join(out, 'f%04d_%s.png' % (f, view))
                bpy.ops.render.render(write_still=True)
                written.append(scene.render.filepath)
    if a['blend']:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(a['blend']))
    if a['sheet'] and written:
        sheet(written, os.path.join(out, a['sheet']), len(a['views'].split(',')))
    print('fighter_scene: %d models, %d pictures' % (len(fighters), len(written)))


def game_bind_of_file(path):
    """Each bone's game world matrix at the bind pose: the inverse of the
    file's inverse bind matrix (the importer turns its bones to point at
    their children, so the rest pose is not it)."""
    import json
    import struct
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
    out = {}
    for j, node in enumerate(skin['joints']):
        name = gltf['nodes'][node].get('name', '')
        if not name.startswith('bone'):
            continue
        c = struct.unpack_from('<16f', blob, start + 64 * j)
        m = Matrix(((c[0], c[4], c[8], c[12]), (c[1], c[5], c[9], c[13]),
                    (c[2], c[6], c[10], c[14]), (c[3], c[7], c[11], c[15])))
        out[int(name[4:])] = m.inverted()
    return out


def sheet(paths, out, columns):
    """The pictures in rows of `columns`, each column as wide as its
    widest."""
    import numpy as np
    imgs = []
    for p in paths:
        img = bpy.data.images.load(p)
        w, h = img.size
        a = np.empty(w * h * 4, np.float32)
        img.pixels.foreach_get(a)
        imgs.append(a.reshape(h, w, 4))
    rows = (len(imgs) + columns - 1) // columns
    col_w = [max(i.shape[1] for i in imgs[c::columns]) for c in range(columns)]
    h = max(i.shape[0] for i in imgs)
    canvas = np.zeros((h * rows, sum(col_w), 4), np.float32)
    canvas[..., 3] = 1
    for k, a in enumerate(imgs):
        cx, cy = k % columns, rows - 1 - k // columns
        x0 = sum(col_w[:cx])
        canvas[cy * h:cy * h + a.shape[0], x0:x0 + a.shape[1]] = a
    big = bpy.data.images.new('sheet', canvas.shape[1], canvas.shape[0], alpha=False)
    big.pixels.foreach_set(canvas.ravel())
    big.filepath_raw = out
    big.file_format = 'PNG'
    big.save()


main()
