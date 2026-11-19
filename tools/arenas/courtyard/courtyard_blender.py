"""The stone courtyard, ReBushido's sample arena (docs/MODS.md, "Custom
arenas"), built in Blender and exported as courtyard.glb beside this file:

    blender -b --factory-startup --python tools/arenas/courtyard/courtyard_blender.py

(make_textures.py first, for textures/). What it makes is what an author
would draw by hand, so that the arena compiler is shown a scene as Blender
writes one:

  - a floor of flagstones 16 by 13 metres, two beds of sand in it, three
    steps of 15 cm up to a dais along the north wall, and a stone wall
    3 metres high all round;
  - two wooden posts in the south corners;
  - `collision_...` objects, the surfaces by their materials' names: the
    floor (firm, and sand under the beds), the steps as one ramp (the
    fighters walk up it; the steps are drawn), the dais, the walls' inner
    faces and the posts' sides (plain);
  - `start1a` ... `start4b`, empties where the fighters start, and an
    `obstacle_...` empty on each post's top.

Blender's Z is up and its metres are the scene's; the compiler makes them
the game's units (arena.txt, `scale`)."""
import os

import bpy
import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
TEXTURES = os.path.join(HERE, 'textures')

FLOOR_X = (-8, 8)
FLOOR_Y = (-8, 5)
STEP_DEPTH = 0.6
STEP_RISE = 0.15
STEPS = 3
DAIS_Y = FLOOR_Y[1] + STEPS * STEP_DEPTH   # 6.8
WALL = 8.0
WALL_HEIGHT = 3.0
WALL_THICK = 0.5
SAND_BEDS = [((-6, -4), (-6, -2)), ((4, 6), (-6, -2))]
POSTS = [(-6.0, -6.0), (6.0, -6.0)]
POST_HALF = 0.15
POST_HEIGHT = 2.0
# collision reaches past the walls' faces, so that no foot finds an edge
REACH = 8.3


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, picture=None):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    if picture:
        tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
        tex.image = bpy.data.images.load(os.path.join(TEXTURES, picture + '.png'))
        tex.interpolation = 'Closest'
        mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    return mat


def add_object(name, faces):
    """faces: (corners, material, uvs), the corners counter-clockwise seen
    from the front."""
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.new('UVMap')
    mats = []
    for corners, mat, uvs in faces:
        if mat not in mats:
            mats.append(mat)
        verts = [bm.verts.new(c) for c in corners]
        face = bm.faces.new(verts)
        face.material_index = mats.index(mat)
        for loop, uv in zip(face.loops, uvs):
            loop[uv_layer].uv = uv
    bm.to_mesh(mesh)
    bm.free()
    for mat in mats:
        mesh.materials.append(mat)
    return obj


def empty(name, at):
    obj = bpy.data.objects.new(name, None)
    obj.location = at
    bpy.context.scene.collection.objects.link(obj)


UV1 = [(0, 0), (1, 0), (1, 1), (0, 1)]


def floor_quad(x0, y0, x1, y1, z, mat, uv=UV1):
    """A level quad, its front up."""
    return ([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], mat, uv)


def wall_face(side, a, b, z0, z1):
    """A wall's inner face from a to b along it, its front to the middle:
    south (y = -8), north, west (x = -8), east."""
    if side == 0:
        return [(b, -WALL, z0), (a, -WALL, z0), (a, -WALL, z1), (b, -WALL, z1)]
    if side == 1:
        return [(a, WALL, z0), (b, WALL, z0), (b, WALL, z1), (a, WALL, z1)]
    if side == 2:
        return [(-WALL, a, z0), (-WALL, b, z0), (-WALL, b, z1), (-WALL, a, z1)]
    return [(WALL, b, z0), (WALL, a, z0), (WALL, a, z1), (WALL, b, z1)]


def in_sand(x, y):
    return any(a[0] <= x < a[1] and b[0] <= y < b[1] for a, b in SAND_BEDS)


def build():
    clear()
    flag = material('flagstone', 'flagstone')
    sand = material('sand', 'sand')
    wall = material('wall', 'wall')
    step = material('step', 'step')
    wood = material('wood', 'wood')
    firm = material('firm')
    sand_c = material('sand_ground')
    plain = material('plain')

    # ---- what is drawn ----
    floor = []
    # flagstones of two metres, the last row one (each face its whole
    # texture: the game's textures do not repeat across a face)
    for x in range(FLOOR_X[0], FLOOR_X[1], 2):
        for y in range(FLOOR_Y[0], FLOOR_Y[1], 2):
            y1 = min(y + 2, FLOOR_Y[1])
            floor.append(floor_quad(x, y, x + 2, y1, 0.0, sand if in_sand(x, y) else flag,
                                    [(0, 0), (1, 0), (1, (y1 - y) / 2), (0, (y1 - y) / 2)]))
    add_object('floor', floor)

    steps = []
    for s in range(STEPS):
        y0 = FLOOR_Y[1] + s * STEP_DEPTH
        z0, z1 = s * STEP_RISE, (s + 1) * STEP_RISE
        for x in range(FLOOR_X[0], FLOOR_X[1], 2):
            # the riser, its front to the south (-y)
            steps.append(([(x, y0, z0), (x + 2, y0, z0), (x + 2, y0, z1), (x, y0, z1)], step,
                          [(0, 0), (1, 0), (1, 0.15), (0, 0.15)]))
            steps.append(floor_quad(x, y0, x + 2, y0 + STEP_DEPTH, z1, step,
                                    [(0, 0), (1, 0), (1, 0.6), (0, 0.6)]))
    add_object('steps', steps)

    dais = []
    top = STEPS * STEP_RISE
    for x in range(FLOOR_X[0], FLOOR_X[1], 2):
        dais.append(floor_quad(x, DAIS_Y, x + 2, WALL, top, flag, [(0, 0), (1, 0), (1, 0.6), (0, 0.6)]))
    add_object('dais', dais)

    walls = []
    panel_w, panel_h = 2.0, 3.0
    rows = int(WALL_HEIGHT / panel_h)
    for side in range(4):
        for i in range(int(2 * WALL / panel_w)):
            a = -WALL + i * panel_w
            b = a + panel_w
            for r in range(rows):
                z0, z1 = r * panel_h, (r + 1) * panel_h
                # each wall's inner face, its front to the middle
                c = wall_face(side, a, b, z0, z1)
                walls.append((c, wall, UV1))
        # the wall's top, a cap of the steps' stone
        for i in range(int(2 * WALL / panel_w)):
            a = -WALL + i * panel_w
            b = a + panel_w
            t, o = WALL, WALL + WALL_THICK
            if side == 0:
                walls.append(floor_quad(a, -o, b, -t, WALL_HEIGHT, step, [(0, 0), (1, 0), (1, 0.25), (0, 0.25)]))
            elif side == 1:
                walls.append(floor_quad(a, t, b, o, WALL_HEIGHT, step, [(0, 0), (1, 0), (1, 0.25), (0, 0.25)]))
            elif side == 2:
                walls.append(floor_quad(-o, a, -t, b, WALL_HEIGHT, step, [(0, 0), (1, 0), (1, 0.25), (0, 0.25)]))
            else:
                walls.append(floor_quad(t, a, o, b, WALL_HEIGHT, step, [(0, 0), (1, 0), (1, 0.25), (0, 0.25)]))
    add_object('walls', walls)

    posts = []
    for px, py in POSTS:
        x0, x1, y0, y1 = px - POST_HALF, px + POST_HALF, py - POST_HALF, py + POST_HALF
        h = POST_HEIGHT
        uv = [(0, 0), (0.3, 0), (0.3, 1), (0, 1)]
        posts += [([(x0, y0, 0), (x1, y0, 0), (x1, y0, h), (x0, y0, h)], wood, uv),   # south face
                  ([(x1, y0, 0), (x1, y1, 0), (x1, y1, h), (x1, y0, h)], wood, uv),   # east
                  ([(x1, y1, 0), (x0, y1, 0), (x0, y1, h), (x1, y1, h)], wood, uv),   # north
                  ([(x0, y1, 0), (x0, y0, 0), (x0, y0, h), (x0, y1, h)], wood, uv),   # west
                  (floor_quad(x0, y0, x1, y1, h, wood, [(0, 0), (0.3, 0), (0.3, 0.3), (0, 0.3)]))]
    add_object('posts', posts)

    # ---- what is met ----
    # the floor as it is drawn, two metres at a time, sand under the beds;
    # the rows and columns at the walls reach past them
    ground = []
    for x in range(FLOOR_X[0], FLOOR_X[1], 2):
        for y in range(FLOOR_Y[0], FLOOR_Y[1], 2):
            x0 = -REACH if x == FLOOR_X[0] else x
            x1 = REACH if x + 2 == FLOOR_X[1] else x + 2
            y0 = -REACH if y == FLOOR_Y[0] else y
            y1 = min(y + 2, FLOOR_Y[1])
            ground.append(floor_quad(x0, y0, x1, y1, 0.0, sand_c if in_sand(x, y) else firm))
    # the steps as one ramp, and the dais
    ground.append(([(-REACH, FLOOR_Y[1], 0.0), (REACH, FLOOR_Y[1], 0.0), (REACH, DAIS_Y, top),
                    (-REACH, DAIS_Y, top)], firm, UV1))
    ground.append(floor_quad(-REACH, DAIS_Y, REACH, REACH, top, firm))
    add_object('collision_ground', ground)

    sides = []
    low, high = -0.5, WALL_HEIGHT
    for side in range(4):
        sides.append((wall_face(side, -WALL, WALL, low, high), plain, UV1))
    for px, py in POSTS:
        x0, x1, y0, y1 = px - POST_HALF, px + POST_HALF, py - POST_HALF, py + POST_HALF
        h = POST_HEIGHT
        sides += [([(x0, y0, low), (x1, y0, low), (x1, y0, h), (x0, y0, h)], plain, UV1),
                  ([(x1, y0, low), (x1, y1, low), (x1, y1, h), (x1, y0, h)], plain, UV1),
                  ([(x1, y1, low), (x0, y1, low), (x0, y1, h), (x1, y1, h)], plain, UV1),
                  ([(x0, y1, low), (x0, y0, low), (x0, y0, h), (x0, y1, h)], plain, UV1),
                  floor_quad(x0, y0, x1, y1, h, firm)]
    add_object('collision_walls', sides)

    # ---- where the fighters start, and the posts' tops ----
    for name, at in (('start1a', (-2, 0, 0)), ('start1b', (2, 0, 0)),
                     ('start2a', (0, -2, 0)), ('start2b', (0, 2, 0)),
                     ('start3a', (-2, -4.5, 0)), ('start3b', (2, -4.5, 0)),
                     ('start4a', (-2, 3, 0)), ('start4b', (2, 3, 0))):
        empty(name, at)
    for i, (px, py) in enumerate(POSTS):
        empty('obstacle_post%d' % (i + 1), (px, py, POST_HEIGHT))

    bpy.ops.export_scene.gltf(filepath=os.path.join(HERE, 'courtyard.glb'), export_format='GLB',
                              export_yup=True, export_apply=True, export_texcoords=True,
                              export_normals=False, export_materials='EXPORT')


build()
