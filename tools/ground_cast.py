#!/usr/bin/env python3
"""The game's cast of a segment across a stage's collision grid
(src/main/collision.c), in Python, number for number: what a fighter's
feet, a blade or the camera would meet in a ground file. The arena
compiler (tools/arena_compile.py) stands the fighters' starts on the floor
with it, and the validator (tools/arena_validate.py) walks an arena with it.

    grid = Grid(collision_section)     # stage_ground's collision section
    hit = grid.cast((x, y, z), (x2, y2, z2))
    if hit: point, normal, surface = hit

`cast` is collision_CastWith with collision_TestCell (the projection on
the face as the GTE makes it); `cast_move` is collision_CastMoveWith,
which casts a short move from further back with the fine test. The
world's y grows downwards; a floor's normal points up, its y below 0.
"""
import struct

CELLS = 32
CELL = 2000
HALF = 32000
NO_HIT = 0x7FFFFFFF
SHORT_MOVE_SQ = 40000
FLOOR_NORMAL_Y = -4096 // 2      # FIGHTER_FLOOR_NORMAL_Y: of ONE (4096)

# surfaces (enum Surface, include/collision.h)
PLAIN, FIRM, FIRM_2, SAND, SAND_2, WET, WET_2, BARRIER = range(8)
SURFACE_NAMES = ['plain', 'firm', 'firm2', 'sand', 'sand2', 'wet', 'wet2', 'barrier']


def s16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def s32(x):
    x &= 0xFFFFFFFF
    return x - 0x100000000 if x & 0x80000000 else x


def cdiv(a, b):
    """C's division: toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def cmod(a, b):
    return a - b * cdiv(a, b)


def shift_magnitude(x, n):
    return -((-x) >> n) if x < 0 else x >> n


def half_rounded(x):
    return (-1 if x < 0 else 1) * (((x + 1) if x >= 0 else (-x + 1)) >> 1)


def length_squared(x, y, z):
    """math_LengthSquared: the GTE squares 16 bits of each."""
    return s32(s16(x) ** 2 + s16(y) ** 2 + s16(z) ** 2)


def ir(x):
    """A GTE IR register's saturation (lm = 0)."""
    return max(-0x8000, min(0x7FFF, x))


class Grid:
    def __init__(self, collision):
        """`collision`: stage_ground's section ({'cells': {index: [face]}})."""
        self.cells = {int(k): v for k, v in collision['cells'].items()}

    # ---- one cell ----

    def _test_cell(self, cast, cx, cz, fine):
        if cx < 0 or cz < 0 or cx >= CELLS or cz >= CELLS:
            return False
        faces = self.cells.get(cz * CELLS + cx)
        if not faces:
            return False  # an empty cell: 0, whatever was hit before
        corner_x = cx * CELL - HALF
        corner_z = cz * CELL - HALF
        s = cast['start']
        e = cast['end']
        ls = (s16(s[0] - corner_x), s16(s[1]), s16(s[2] - corner_z))
        le = (s16(e[0] - corner_x), s16(e[1]), s16(e[2] - corner_z))
        shift = 10 if fine else 14
        for f in faces:
            nx, ny, nz = f['normal']
            d = f['d']
            sd = s32(nx * ls[0] + ny * ls[1] + nz * ls[2] + d)
            ed = s32(nx * le[0] + ny * le[1] + nz * le[2] + d)
            sd = shift_magnitude(sd, shift)
            ed = shift_magnitude(ed, shift)
            if not (sd >= 0 and ed < 0):
                continue
            work = sd - ed
            if fine:
                sd2 = sd * 2
                px = half_rounded(cdiv((le[0] - ls[0]) * sd2, work)) + ls[0]
                py = half_rounded(cdiv((le[1] - ls[1]) * sd2, work)) + ls[1]
                pz = half_rounded(cdiv((le[2] - ls[2]) * sd2, work)) + ls[2]
                ox, oy, oz = f['origin']
                au = f['u']
                av = f['v']
                pu = ((px - ox) * au[0] + (py - oy) * au[1] + (pz - oz) * au[2]) >> 14
                pv = ((px - ox) * av[0] + (py - oy) * av[1] + (pz - oz) * av[2]) >> 14
            else:
                px = cdiv((le[0] - ls[0]) * sd, work) + ls[0]
                py = cdiv((le[1] - ls[1]) * sd, work) + ls[1]
                pz = cdiv((le[2] - ls[2]) * sd, work) + ls[2]
                ox, oy, oz = f['origin']
                rx, ry, rz = s16(px - ox), s16(py - oy), s16(pz - oz)
                au = f['u']
                av = f['v']
                pu = ir((au[0] * rx + au[1] * ry + au[2] * rz) >> 12) >> 2
                pv = ir((av[0] * rx + av[1] * ry + av[2] * rz) >> 12) >> 2
            outline = f['outline']
            inside = True
            fu, fv = outline[0]
            for tu, tv in outline[1:]:
                if (pu - fu) * (tv - fv) - (pv - fv) * (tu - fu) > 0:
                    inside = False
                    break
                fu, fv = tu, tv
            if not inside:
                continue
            dist = length_squared(px - ls[0], py - ls[1], pz - ls[2])
            if dist < cast['nearest']:
                cast['nearest'] = dist
                cast['hit'] = (px + corner_x, py, pz + corner_z)
                cast['normal'] = (nx >> 2, ny >> 2, nz >> 2)
                cast['surface'] = f['surface']
                cast['face'] = f
        return cast['nearest'] != NO_HIT

    # ---- the walk ----

    def _walk(self, start, end, fine):
        cast = {'start': start, 'end': end, 'nearest': NO_HIT}
        su = start[0] + HALF
        v = start[2] + HALF
        eu = end[0] + HALF
        ev = end[2] + HALF
        du = eu - su
        dv = ev - v
        wx, wz = cdiv(su, CELL), cdiv(v, CELL)
        ex, ez = cdiv(eu, CELL), cdiv(ev, CELL)
        test = lambda x, z: self._test_cell(cast, x, z, fine)
        if wx == ex and wz == ez:
            if tuple(start) == tuple(end):
                return None
            test(wx, wz)
        else:
            cxc = wx * CELL + CELL // 2
            czc = wz * CELL + CELL // 2
            su -= cxc
            v -= czc
            eu -= cxc
            ev -= czc
            if du < 0:
                xdir = -1
                du, su, eu = -du, -su, -eu
            else:
                xdir = 1
            if dv < 0:
                zdir = -1
                dv, v, ev = -dv, -v, -ev
            else:
                zdir = 1
            swapped = dv > du
            if swapped:
                du, dv = dv, du
                su, v = v, su
                eu, ev = ev, eu
            eu += CELL // 2
            su += CELL // 2
            v += CELL // 2
            steps = s16(cdiv(eu, CELL) - cdiv(su, CELL) + 1)
            slope = cdiv(dv << 12, du)
            v -= (su * slope) >> 12
            minor = (slope * CELL) >> 12
            tmp = 0
            while True:
                steps = s16(steps - 1)
                if steps == -1:
                    break
                tmp = 1 if test(wx, wz) else 0
                if tmp:
                    break
                v = cmod(v, CELL)
                v += minor
                if steps == 0:
                    break
                if v > CELL:
                    if swapped:
                        wx += -1 if xdir < 0 else 1
                    else:
                        wz += -1 if zdir < 0 else 1
                    tmp = 1 if test(wx, wz) else 0
                    if tmp:
                        break
                if swapped:
                    wz += -1 if zdir < 0 else 1
                else:
                    wx += -1 if xdir < 0 else 1
            if tmp == 0 and (wx != ex or wz != ez):
                test(ex, ez)
        if cast['nearest'] == NO_HIT:
            return None
        return cast['hit'], cast['normal'], cast['surface']

    def cast(self, start, end):
        """collision_CastWith: (point, normal of ONE, surface) or None."""
        return self._walk(tuple(start), tuple(end), False)

    def cast_move(self, start, end):
        """collision_CastMoveWith."""
        start = list(start)
        if length_squared(end[0] - start[0], end[1] - start[1], end[2] - start[2]) < SHORT_MOVE_SQ:
            for i in range(3):
                start[i] -= (end[i] - start[i]) << 1
            return self._walk(tuple(start), tuple(end), True)
        return self._walk(tuple(start), tuple(end), False)

    def floor_under(self, x, z, y_from, depth=8000):
        """The floor a fighter at (x, z) would stand on, cast down from
        `y_from`, `depth` down: (y, normal, surface) of the first face met,
        or None. A cast's ends are halfwords in the cell's measure (its y
        as it is), so neither end may be beyond 32767."""
        hit = self.cast((x, y_from, z), (x, y_from + depth, z))
        if hit is None:
            return None
        point, normal, surface = hit
        return point[1], normal, surface
