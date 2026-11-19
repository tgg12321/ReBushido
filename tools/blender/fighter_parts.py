"""The fighters' new pieces, modelled in Blender with nothing shown: hands
with fingers closed round the game's grip, made in the hand bone's own
frame and held inside the game's hand piece (tools/port_models/README.md).

    blender --background --factory-startup --python tools/blender/fighter_parts.py -- \
            WORK/NAME [--look]

Reads WORK/NAME/pieces.json (tools/port_models/prepare.py) and writes
WORK/NAME/parts.json: each new piece's vertices (in its bone's frame, the
game's units and axes), normals, texture points and triangles, and the
texture it was painted into, WORK/NAME/parts_albedo.png with
parts_normal.png and parts_orm.png (occlusion, roughness, metal), laid
out to be placed in the fighter's atlas by tools/port_models/assemble.py.
`--look` renders the game's piece, the hd mod's and the new one side by
side from four sides into WORK/NAME/look_<bone>.png.
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

TEX = 1024  # the parts' own texture, square


def args_of():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    return os.path.abspath(argv[0]), '--look' in argv


# ---- the game's pieces, as objects ----

def object_of(name, points, triangles, uvs=None, image=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([Vector(p) for p in points], [], [tuple(t) for t in triangles])
    if uvs is not None:
        layer = me.uv_layers.new(name='uv')
        for poly in me.polygons:
            for li in poly.loop_indices:
                u, v = uvs[me.loops[li].vertex_index]
                layer.data[li].uv = (u, 1.0 - v)
    me.update()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    if image is not None:
        o.data.materials.append(textured(name, image))
    return o


def textured(name, path):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes['Principled BSDF']
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = bpy.data.images.load(path, check_existing=True)
    tex.interpolation = 'Closest'
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = 0.8
    return mat


def plain(name, color, rough=0.6):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes['Principled BSDF']
    bsdf.inputs['Base Color'].default_value = tuple(color) + (1.0,)
    bsdf.inputs['Roughness'].default_value = rough
    return mat


# ---- looking ----

def axes(parent, length):
    """The bone's axes as rods under `parent`: x red, y green, z blue."""
    out = []
    for k, color in enumerate(((1, 0, 0), (0, 1, 0), (0, 0, 1))):
        me = bpy.data.meshes.new('axis')
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:
            c = [v.co.x * 3, v.co.y * 3, v.co.z * 3]
            c[k] = (v.co[k] + 0.5) * length
            v.co = Vector(c)
        bm.to_mesh(me)
        o = bpy.data.objects.new('axis', me)
        o.data.materials.append(plain('axis', color, 0.9))
        o.parent = parent
        bpy.context.scene.collection.objects.link(o)
        out.append(o)
    return out

def look(work, data, built):
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE_NEXT'
    scene.view_settings.view_transform = 'Standard'
    world = bpy.data.worlds.new('w')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs[0].default_value = (0.25, 0.27, 0.3, 1)
    scene.world = world
    for name, rot, energy in (('key', (40, 10, 30), 3.0), ('fill', (60, 0, 200), 1.2)):
        d = bpy.data.lights.new(name, 'SUN')
        d.energy = energy
        o = bpy.data.objects.new(name, d)
        o.rotation_euler = [math.radians(a) for a in rot]
        scene.collection.objects.link(o)
    cam_data = bpy.data.cameras.new('cam')
    cam_data.type = 'ORTHO'
    cam = bpy.data.objects.new('cam', cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    for bone, p in data['pieces'].items():
        for o in built.values():
            o.hide_render = True
        objs = []
        for k, (label, piece) in enumerate((('game', p['game']), ('hd', p['hd']))):
            image = os.path.join(work, 'atlas_%s.png' % label)
            o = object_of('%s_%s' % (label, bone), piece['points'], piece['triangles'], piece['uvs'], image)
            objs.append(o)
        if bone in built:
            built[bone].hide_render = False
            objs.append(built[bone])
        hidden = list(objs)
        for o in objs:
            hidden += axes(o, 80.0)
        size = 0.0
        for o in objs:
            for v in o.data.vertices:
                size = max(size, v.co.length)
        shots = []
        for view, d in (('x', (1, 0, 0)), ('y', (0, 1, 0)), ('z', (0, 0, 1)), ('xyz', (0.6, -0.6, 0.5)), ('palm', (-0.8, 0.3, 0.5))):
            d = Vector(d).normalized()
            up = Vector((0, 0, 1)) if abs(d.z) < 0.9 else Vector((0, 1, 0))
            right = up.cross(d).normalized()
            for k, o in enumerate(objs):
                o.location = right * (k - (len(objs) - 1) / 2) * size * 2.2
            cam_data.ortho_scale = size * 2.3 * len(objs)
            cam_data.clip_end = size * 30
            cam.location = d * size * 10
            cam.rotation_euler = (-d).to_track_quat('-Z', 'Y' if abs(d.z) < 0.9 else 'X').to_euler()
            scene.render.resolution_x = 400 * len(objs)
            scene.render.resolution_y = 400
            path = os.path.join(work, 'look_%s_%s.png' % (bone, view))
            scene.render.filepath = path
            bpy.ops.render.render(write_still=True)
            shots.append(path)
        for o in hidden:
            o.hide_render = True
        print('fighter_parts: looked at bone %s: %s' % (bone, ', '.join(shots)))


# ---- a hand ----
#
# Made for the hand of bone 7, in its frame: the back of the hand towards
# +x, the fingers along +y, the thumb and the index finger on the +z side;
# the hand of bone 11 is its mirror (x turned round), as the game's are.
# The fingers close round the game's grip, a handle across the palm along
# z, then the whole is fitted into the box of the game's hand piece.

FINGERS = (
    # name, z of its knuckle, radius, the lengths of its three bones
    ('index', 27.0, 9.6, (31.0, 20.0, 17.0)),
    ('middle', 8.0, 9.8, (33.0, 22.0, 18.0)),
    ('ring', -11.5, 9.3, (31.0, 21.0, 17.0)),
    ('little', -29.5, 8.2, (25.0, 16.0, 15.0)),
)
KNUCKLE = (14.0, 108.0)  # x, y of the knuckles
CURL = (18.0, 95.0, 70.0)  # each joint's bend, degrees, round z


def finger_chain(z, radius, lengths, grip):
    """The finger's joints: from the knuckle, each bone turned further
    towards -x round z (the palm's side)."""
    x, y = KNUCKLE
    pts = [(Vector((x, y - 10.0, z)), radius * 1.05), (Vector((x, y, z)), radius)]
    angle = 0.0
    for k, length in enumerate(lengths):
        angle += math.radians(CURL[k]) * grip
        d = Vector((-math.sin(angle), math.cos(angle), 0.0))
        p = pts[-1][0] + d * length
        pts.append((p, radius * (0.92 - 0.1 * k)))
    return pts


THUMB = [(Vector((2.0, 28.0, 30.0)), 15.0), (Vector((-22.0, 58.0, 38.0)), 12.5),
         (Vector((-42.0, 82.0, 33.0)), 10.5), (Vector((-50.0, 100.0, 22.0)), 9.0),
         (Vector((-50.0, 112.0, 13.0)), 8.0)]


def skin_object(name, chain):
    """A tube through the chain's points, of each point's radius
    (Blender's skin modifier), applied and smoothed."""
    me = bpy.data.meshes.new(name)
    me.from_pydata([p for p, _ in chain], [(i, i + 1) for i in range(len(chain) - 1)], [])
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    mod = o.modifiers.new('skin', 'SKIN')
    mod.use_smooth_shade = True
    for v, (_, r) in zip(me.skin_vertices[0].data, chain):
        v.radius = (r, r)
    me.skin_vertices[0].data[0].use_root = True
    o.modifiers.new('sub', 'SUBSURF').levels = 2
    apply_all(o)
    return o


def apply_all(o):
    bpy.context.view_layer.objects.active = o
    for o2 in bpy.context.selected_objects:
        o2.select_set(False)
    o.select_set(True)
    for mod in list(o.modifiers):
        bpy.ops.object.modifier_apply(modifier=mod.name)


def box_object(name, lo, hi, bevel):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector([lo[k] + (v.co[k] + 0.5) * (hi[k] - lo[k]) for k in range(3)])
    bm.to_mesh(me)
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    b = o.modifiers.new('bevel', 'BEVEL')
    b.width = bevel
    b.segments = 4
    o.modifiers.new('sub', 'SUBSURF').levels = 2
    apply_all(o)
    return o


def cuff_object(name):
    """The glove's wrist: an oval tube along y."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=24, radius1=1.0, radius2=1.0, depth=1.0)
    for v in bm.verts:
        # the cone stands along z: lay it along y
        x, y, z = v.co
        v.co = Vector((8.0 + x * 23.0, -28.0 + (z + 0.5) * 44.0, -1.0 + y * 41.0))
    bm.to_mesh(me)
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    b = o.modifiers.new('bevel', 'BEVEL')
    b.width = 4.0
    b.segments = 3
    o.modifiers.new('sub', 'SUBSURF').levels = 1
    apply_all(o)
    return o


def join(objs, name):
    for o in bpy.context.selected_objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    objs[0].name = name
    return objs[0]


def segment_distance(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9)))
    return (p - (a + ab * t)).length, t


def hand_regions(o, chains, colors):
    """Each vertex's colour, roughness and metal as attributes, by what it
    is part of: the glove (palm, back, wrist and a band round each
    finger's first bone), the bare back of the hand within the glove's
    opening, the skin of the fingers, the nails, the stud on the wrist."""
    me = o.data
    cols, roughs, metals, grains = [], [], [], []
    skin, glove, nail, stud = (Vector(c) for c in (colors['skin'], colors['glove'], colors['nail'],
                                                    colors['stud']))
    for v in me.vertices:
        p, n = v.co, v.normal
        best = (1e9, None, 0, 0.0)
        for ci, chain in enumerate(chains):
            for k in range(len(chain) - 1):
                d, t = segment_distance(p, chain[k][0], chain[k + 1][0])
                d -= chain[k][1] + (chain[k + 1][1] - chain[k][1]) * t
                if d < best[0]:
                    best = (d, ci, k, t)
        d, ci, k, t = best
        c, r, m, g = glove, 0.42, 0.0, 1.0
        fingered = d < 3.0 and (k >= 1 or t > 0.9)
        if ci is not None and ci < 4 and fingered:
            # a finger: the glove's band on the first bone, then skin
            if k == 0 or (k == 1 and t < 0.22):
                c, r, g = glove, 0.42, 1.0
            else:
                c, r, g = skin, 0.55, 0.0
                chain = chains[ci]
                a, b = chain[k][0], chain[k + 1][0]
                axis = (b - a).normalized()
                # the back of the finger: the outside of its curl
                dorsal = Vector((axis.y, -axis.x, 0.0))
                if k == len(chain) - 2 and t > 0.3 and n.dot(dorsal) > 0.55:
                    c, r, g = nail, 0.3, 0.0
        elif ci == 4 and d < 4.0 and (k >= 1 or t > 0.7):
            # the thumb: bare beyond the glove's palm
            c, r, g = skin, 0.55, 0.0
            if k == 3 and t > 0.3 and n.z > 0.35 and n.x > -0.3:
                c, r, g = nail, 0.3, 0.0
        else:
            # the palm, the back and the wrist: the glove, open on the back
            if p.x > 16.0 and ((p.y - 62.0) / 24.0) ** 2 + (p.z / 17.0) ** 2 < 1.0:
                c, r, g = skin, 0.55, 0.0
            if p.x > 22.0 and -22.0 < p.y < -4.0 and abs(p.z) < 11.0:
                c, r, m, g = stud, 0.25, 1.0, 0.0
        cols += [c.x, c.y, c.z, 1.0]
        roughs.append(r)
        metals.append(m)
        grains.append(g)
    # made all at once: a new attribute moves the others' arrays
    me.color_attributes.new('rb_color', 'FLOAT_COLOR', 'POINT')
    for name in ('rb_rough', 'rb_metal', 'rb_grain'):
        me.attributes.new(name, 'FLOAT', 'POINT')
    me.attributes['rb_color'].data.foreach_set('color', cols)
    me.attributes['rb_rough'].data.foreach_set('value', roughs)
    me.attributes['rb_metal'].data.foreach_set('value', metals)
    me.attributes['rb_grain'].data.foreach_set('value', grains)
    print('fighter_parts: the colours of the hand: %d glove, %d skin, %d nail, %d metal of %d'
          % (grains.count(1.0), sum(1 for k in range(len(roughs)) if roughs[k] == 0.55),
             roughs.count(0.3), metals.count(1.0), len(roughs)))


def bake_material(o):
    """The high model's material: its colour attribute as emission (for
    the colour's bake), and the leather's grain and the skin's lines as
    bumps (for the normals')."""
    mat = bpy.data.materials.new('high')
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes['Principled BSDF']
    attr = nt.nodes.new('ShaderNodeAttribute')
    attr.attribute_name = 'rb_color'
    nt.links.new(attr.outputs['Color'], bsdf.inputs['Base Color'])
    g = nt.nodes.new('ShaderNodeAttribute')
    g.attribute_name = 'rb_grain'
    noise = nt.nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value = 0.9
    noise.inputs['Detail'].default_value = 8.0
    voronoi = nt.nodes.new('ShaderNodeTexVoronoi')
    voronoi.inputs['Scale'].default_value = 0.6
    mix = nt.nodes.new('ShaderNodeMath')
    mix.operation = 'MULTIPLY'
    nt.links.new(voronoi.outputs['Distance'], mix.inputs[0])
    nt.links.new(g.outputs['Fac'], mix.inputs[1])
    fine = nt.nodes.new('ShaderNodeMath')
    fine.operation = 'MULTIPLY_ADD'
    nt.links.new(noise.outputs['Fac'], fine.inputs[0])
    fine.inputs[1].default_value = 0.25
    nt.links.new(mix.outputs[0], fine.inputs[2])
    bump = nt.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.35
    bump.inputs['Distance'].default_value = 0.6
    nt.links.new(fine.outputs[0], bump.inputs['Height'])
    nt.links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])
    emit = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(attr.outputs['Color'], emit.inputs['Color'])
    out = nt.nodes['Material Output']
    o.data.materials.append(mat)
    return mat, emit, bsdf, out, nt


def bake(low, high, kind, image, extrusion=3.0):
    scene = bpy.context.scene
    for o in bpy.context.selected_objects:
        o.select_set(False)
    if high is not None:
        high.select_set(True)
    low.select_set(True)
    bpy.context.view_layer.objects.active = low
    node = low.active_material.node_tree.nodes['bake_target']
    node.image = image
    low.active_material.node_tree.nodes.active = node
    scene.render.bake.use_selected_to_active = high is not None
    scene.render.bake.cage_extrusion = extrusion
    scene.render.bake.margin = 8
    if kind == 'DIFFUSE':
        scene.render.bake.use_pass_direct = False
        scene.render.bake.use_pass_indirect = False
        scene.render.bake.use_pass_color = True
    bpy.ops.object.bake(type=kind)


def build_hand(piece, colors, size):
    """The hand of bone 7, high and low, fitted into the game's piece."""
    grip = 1.0
    chains = [finger_chain(z, r, lengths, grip) for _, z, r, lengths in FINGERS] + [THUMB]
    parts = [box_object('palm', (-14.0, 0.0, -40.0), (27.0, KNUCKLE[1] + 4.0, 38.0), 11.0),
             cuff_object('cuff')]
    parts += [skin_object('finger%d' % k, c) for k, c in enumerate(chains)]
    high = join(parts, 'hand_high')
    rem = high.modifiers.new('remesh', 'REMESH')
    rem.mode = 'VOXEL'
    rem.voxel_size = 1.1
    sm = high.modifiers.new('smooth', 'CORRECTIVE_SMOOTH')
    sm.iterations = 6
    sm.smooth_type = 'LENGTH_WEIGHTED'
    sm.use_only_smooth = True
    apply_all(high)
    hand_regions(high, chains, colors)
    # into the box of the game's piece, each way
    pts = piece['points']
    lo = Vector([min(p[k] for p in pts) for k in range(3)])
    hi = Vector([max(p[k] for p in pts) for k in range(3)])
    blo = Vector([min(v.co[k] for v in high.data.vertices) for k in range(3)])
    bhi = Vector([max(v.co[k] for v in high.data.vertices) for k in range(3)])
    scale = [(hi[k] - lo[k]) / max(bhi[k] - blo[k], 1e-6) for k in range(3)]
    s = [min(max(scale[k], 0.75), 1.25) for k in range(3)]
    fit = lambda q: Vector([lo[k] + (q[k] - blo[k]) * s[k] for k in range(3)])  # noqa: E731
    for v in high.data.vertices:
        v.co = fit(v.co)
    print('fighter_parts: the hand built %s to %s, fitted by %s into the game\'s %s to %s'
          % (tuple(round(c) for c in blo), tuple(round(c) for c in bhi), tuple(round(c, 2) for c in s),
             tuple(round(c) for c in lo), tuple(round(c) for c in hi)))
    for poly in high.data.polygons:
        poly.use_smooth = True
    # the low model: the high one made lighter, its texture laid out anew
    low = high.copy()
    low.data = high.data.copy()
    low.name = 'hand_low'
    bpy.context.scene.collection.objects.link(low)
    dec = low.modifiers.new('decimate', 'DECIMATE')
    # quads, two triangles each
    dec.ratio = min(1.0, size / max(1, 2 * len(high.data.polygons)))
    tri = low.modifiers.new('tri', 'TRIANGULATE')
    apply_all(low)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.012)
    bpy.ops.object.mode_set(mode='OBJECT')
    mat, emit, bsdf, out, nt = bake_material(high)
    return high, low, (mat, emit, bsdf, out, nt)


def bake_hand(work, high, low, hm):
    mat, emit, bsdf, out, nt = hm
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 16
    lm = bpy.data.materials.new('low')
    lm.use_nodes = True
    target = lm.node_tree.nodes.new('ShaderNodeTexImage')
    target.name = 'bake_target'
    low.data.materials.clear()
    low.data.materials.append(lm)
    images = {}
    for name, space in (('albedo', 'sRGB'), ('normal', 'Non-Color'), ('rough', 'Non-Color'),
                        ('metal', 'Non-Color'), ('ao', 'Non-Color')):
        img = bpy.data.images.new('parts_' + name, TEX, TEX, alpha=False, float_buffer=name == 'normal')
        img.colorspace_settings.name = space
        images[name] = img
    surface = out.inputs['Surface']
    # the colour: the attribute, as light
    nt.links.new(emit.outputs['Emission'], surface)
    bake(low, high, 'EMIT', images['albedo'])
    for name in ('rough', 'metal'):
        a = nt.nodes.new('ShaderNodeAttribute')
        a.attribute_name = 'rb_' + name
        nt.links.new(a.outputs['Fac'], emit.inputs['Color'])
        bake(low, high, 'EMIT', images[name])
    nt.links.new(bsdf.outputs['BSDF'], surface)
    bake(low, high, 'NORMAL', images['normal'])
    scene.cycles.samples = 64
    bake(low, None, 'AO', images['ao'])
    for name, img in images.items():
        img.filepath_raw = os.path.join(work, 'parts_%s.png' % name)
        img.file_format = 'PNG'
        img.save()
    # what the look shows: the low model with its colour
    lm.node_tree.links.new(target.outputs['Color'],
                           lm.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
    target.image = images['albedo']
    return images


def mesh_out(o, bone, mirror):
    """The low model's vertices (one for each place and texture point),
    normals and triangles, in the bone's frame; mirrored in x for the
    other hand, its triangles turned round to stay outward."""
    me = o.data
    uv = me.uv_layers.active.data
    me.calc_loop_triangles()
    key_of = {}
    points, normals, uvs, tris = [], [], [], []
    for lt in me.loop_triangles:
        t = []
        for li, vi in zip(lt.loops, lt.vertices):
            u = uv[li].uv
            key = (vi, round(u.x, 5), round(u.y, 5))
            if key not in key_of:
                key_of[key] = len(points)
                co = me.vertices[vi].co
                n = me.vertices[vi].normal
                s = -1.0 if mirror else 1.0
                points.append([s * co.x, co.y, co.z])
                normals.append([s * n.x, n.y, n.z])
                uvs.append([u.x, 1.0 - u.y])
            t.append(key_of[key])
        tris.append([t[0], t[2], t[1]] if mirror else t)
    return {'bone': bone, 'points': points, 'normals': normals, 'uvs': uvs, 'triangles': tris}


def main():
    work, looking = args_of()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    with open(os.path.join(work, 'pieces.json'), encoding='utf-8') as f:
        data = json.load(f)
    recipe = data['recipe']
    colors = data['colors']
    pieces = data['pieces']
    built = {}
    out = {'name': data['name'], 'pieces': []}
    hands = [b for b in (7, 11) if str(b) in pieces]
    if hands:
        high, low, hm = build_hand(pieces['7']['game'], colors, recipe['hand'].get('triangles', 3000))
        bake_hand(work, high, low, hm)
        high.hide_render = True
        for b in hands:
            out['pieces'].append(mesh_out(low, b, b == 11))
        # the lower levels of detail: the low model made lighter again,
        # its texture points kept
        for level, share in ((1, 0.3), (2, 0.08)):
            lighter = low.copy()
            lighter.data = low.data.copy()
            bpy.context.scene.collection.objects.link(lighter)
            dec = lighter.modifiers.new('decimate', 'DECIMATE')
            dec.ratio = share
            lighter.modifiers.new('tri', 'TRIANGULATE')
            apply_all(lighter)
            lighter.hide_render = True
            for b in hands:
                piece = mesh_out(lighter, b, b == 11)
                piece['level'] = level
                out['pieces'].append(piece)
        print('fighter_parts: the hand: high %d faces, low %d triangles'
              % (len(high.data.polygons), len(out['pieces'][0]['triangles'])))
        built['7'] = high if os.environ.get('RB_LOOK_HIGH') else low
        high.hide_render = not os.environ.get('RB_LOOK_HIGH')
        if '11' in pieces:
            mirrored = low.copy()
            mirrored.data = low.data.copy()
            mirrored.scale.x = -1.0
            bpy.context.scene.collection.objects.link(mirrored)
            built['11'] = mirrored
    with open(os.path.join(work, 'parts.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(out, f)
    if looking:
        look(work, data, built)


if __name__ == "__main__":
    main()
