#!/usr/bin/env python3
"""The gallery: a test arena made procedurally, to hold the compiler's
textures to what it says (docs/MODS.md, "Custom arenas", "Textures").

    python3 tools/arenas/gallery/make_gallery.py FOLDER

writes a source folder that tools/arena_compile.py compiles: gallery.obj,
gallery.mtl, the pictures in textures/ and arena.txt. Nothing of it is
drawn by hand or taken from anywhere: every picture is worked out here.

A floor of 16 by 16 metres in one face whose texture repeats four times
each way, a picture of 128 by 128 (flagstones of 32 texels, a hairline
in each, which a 64 would lose); four walls 3 metres high, each one face
whose brick texture of 64 repeats four times along and once up; and 18
panels on the walls, three of 64 and fifteen of 32, each a picture of its
own (a colour of its own, its number in dots): twenty textures, as many
as the fog's row of colour tables holds, in all the room of the three
pages but one sixteenth. The collision is a floor of firm ground and
barriers at the walls. Fought in place of the Abandoned Station (25)."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
import arena_compile  # noqa: E402

SIDE = 8.0      # half the floor, metres
HEIGHT = 3.0    # the walls'
PANELS_64 = 3
PANELS_32 = 15


def hue(index, count):
    """A colour of its own for each of `count`, bright and apart."""
    h = index / count * 6.0
    k = int(h) % 6
    f = h - int(h)
    v, lo = 230, 40
    up = int(lo + (v - lo) * f)
    down = int(v - (v - lo) * f)
    return [(v, up, lo), (down, v, lo), (lo, v, up), (lo, down, v), (up, lo, v), (v, lo, down)][k]


def picture(size, texel):
    rgb = bytearray()
    for y in range(size):
        for x in range(size):
            rgb += bytes(texel(x, y))
    return arena_compile.write_png(size, size, bytes(rgb))


def flagstone(x, y):
    """128: flags of 32, each its own grey, a mortar line and a hairline."""
    fx, fy = x // 32, y // 32
    lx, ly = x % 32, y % 32
    if lx == 0 or ly == 0:
        return (60, 55, 50)
    if lx == ly:
        return (90, 85, 80)
    shade = 140 + ((fx * 37 + fy * 53) % 5) * 18
    return (shade, shade - 6, shade - 14)


def brick(x, y):
    """64: bricks of 16 by 8, staggered."""
    row = y // 8
    lx = (x + (8 if row % 2 else 0)) % 16
    if y % 8 == 0 or lx == 0:
        return (200, 195, 180)
    return (150 + (row % 3) * 10, 70, 50)


def panel(index, count, size):
    """A panel: its colour, a light border, and its number in dots of a
    4 by 4 grid, the low bit at the top left."""
    color = hue(index, count)
    cell = size // 4

    def texel(x, y):
        if x < 2 or y < 2 or x >= size - 2 or y >= size - 2:
            return (240, 240, 240)
        bit = (y // cell) * 4 + x // cell
        cx, cy = x % cell - cell // 2, y % cell - cell // 2
        if (index + 1) >> bit & 1 and cx * cx + cy * cy <= (cell // 3) ** 2:
            return (20, 20, 20)
        return color
    return picture(size, texel)


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


class Obj:
    def __init__(self):
        self.lines = ['# made by make_gallery.py', 'mtllib gallery.mtl']
        self.vertices = 0
        self.uvs = 0

    def face(self, obj, material, points, uvs, facing):
        """A face seen from `facing`: its corners turned round if they are
        not counter-clockwise about it."""
        n = cross([points[1][k] - points[0][k] for k in range(3)],
                  [points[2][k] - points[0][k] for k in range(3)])
        if sum(n[k] * facing[k] for k in range(3)) < 0:
            points = points[::-1]
            uvs = uvs[::-1]
        self.lines += ['o %s' % obj, 'usemtl %s' % material]
        refs = []
        for p, t in zip(points, uvs):
            self.lines.append('v %.4f %.4f %.4f' % tuple(p))
            self.lines.append('vt %.4f %.4f' % (t[0], 1.0 - t[1]))
            self.vertices += 1
            self.uvs += 1
            refs.append('%d/%d' % (self.vertices, self.uvs))
        self.lines.append('f ' + ' '.join(refs))


def walls():
    """Each wall: a point on it at the floor's left end seen from inside,
    the way along it, and the way into the yard."""
    return [((-SIDE, 0, -SIDE), (1, 0, 0), (0, 0, 1)),
            ((SIDE, 0, -SIDE), (0, 0, 1), (-1, 0, 0)),
            ((SIDE, 0, SIDE), (-1, 0, 0), (0, 0, -1)),
            ((-SIDE, 0, SIDE), (0, 0, -1), (1, 0, 0))]


def make(folder):
    os.makedirs(os.path.join(folder, 'textures'), exist_ok=True)
    obj = Obj()
    mtl = ['# made by make_gallery.py']
    textures = {'floor': picture(128, flagstone), 'wall': picture(64, brick)}
    sizes = [64] * PANELS_64 + [32] * PANELS_32
    count = len(sizes)
    for i, size in enumerate(sizes):
        textures['panel%02d' % i] = panel(i, count, size)
    for name, data in textures.items():
        with open(os.path.join(folder, 'textures', name + '.png'), 'wb') as f:
            f.write(data)
        mtl += ['newmtl %s' % name, 'map_Kd textures/%s.png' % name]
    mtl += ['newmtl firm', 'newmtl barrier']
    up = (0, 1, 0)
    floor = [(-SIDE, 0, -SIDE), (-SIDE, 0, SIDE), (SIDE, 0, SIDE), (SIDE, 0, -SIDE)]
    # the floor drawn: four repeats each way; and met, firm
    obj.face('floor', 'floor', floor, [(0, 0), (0, 4), (4, 4), (4, 0)], up)
    obj.face('collision_floor', 'firm', floor, [(0, 0)] * 4, up)
    place = 0
    for start, along, into in walls():
        corners = [start, [start[k] + 2 * SIDE * along[k] for k in range(3)]]
        quad = [corners[0], corners[1], [corners[1][0], HEIGHT, corners[1][2]],
                [corners[0][0], HEIGHT, corners[0][2]]]
        obj.face('wall', 'wall', quad, [(0, 1), (4, 1), (4, 0), (0, 0)], into)
        obj.face('collision_wall', 'barrier', quad, [(0, 0)] * 4, into)
        # its panels, 2 cm out from it, spread along it
        mine = [i for i in range(count) if i % 4 == place]
        for n, i in enumerate(mine):
            width = 1.5 if sizes[i] == 64 else 1.0
            middle = 2 * SIDE * (n + 1) / (len(mine) + 1)
            base = [start[k] + middle * along[k] + 0.02 * into[k] for k in range(3)]
            low, high = 0.9, 0.9 + width
            a = [base[k] - width / 2 * along[k] for k in range(3)]
            b = [base[k] + width / 2 * along[k] for k in range(3)]
            obj.face('panel', 'panel%02d' % i,
                     [[a[0], low, a[2]], [b[0], low, b[2]], [b[0], high, b[2]], [a[0], high, a[2]]],
                     [(0, 1), (1, 1), (1, 0), (0, 0)], into)
        place += 1
    with open(os.path.join(folder, 'gallery.obj'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(obj.lines) + '\n')
    with open(os.path.join(folder, 'gallery.mtl'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(mtl) + '\n')
    with open(os.path.join(folder, 'arena.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# The gallery: tools/arenas/gallery/make_gallery.py\n'
                'name = Gallery\n'
                'scene = gallery.obj\n'
                'scale = 1000\n'
                'stands_in_for = 25\n'
                'start1 = -2000 0 0 2000 0 0\n'
                'fog = on\n'
                'fog_span = 32000\n'
                'fog_farthest = 0\n')
    return folder


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    make(sys.argv[1])
    print('make_gallery: wrote %s' % sys.argv[1])
