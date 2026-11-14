"""The C beside the assembly against real maths.

The program prints what the port's C made of numbers it made up (roots,
rotation matrices, products, blends); here the same things are worked out
in Python, with real square roots, sines and whole-number arithmetic, and
compared. (That the C does to the last bit what the assembly does is the
play test gte_model's business, and port/asm_vectors holds it to that.)
"""
import math
import re

ONE = 4096
TURN = 4096
SONY_TABLE = 'src/main/psxsdk/libgte/cstbl.c'


def sources():
    import glob
    modules = []
    for path in sorted(glob.glob('src/main/psxsdk/libgte/*.c')):
        with open(path, encoding='utf-8') as f:
            if '#if RB_PORT' in f.read():
                modules.append(path)
    return modules + ['src/main/psxsdk/libgte/cstbl.c', 'src/main/psxsdk/libgte/sqrtbl.c', 'src/port/gte.c']


def sony_table():
    """Sony's table of sines and cosines, a pair for each of 4096 angles.
    It is not the real sine's: it is up to seven parts in 4096 off, which
    is why Sony's rotations are compared with rotations made of its
    numbers, and only the game's with the real sine."""
    with open(SONY_TABLE, encoding='utf-8') as f:
        text = f.read()
    body = text[text.index('{') + 1:text.rindex('}')]
    numbers = [int(v, 0) for v in re.findall(r'-?(?:0[xX][0-9A-Fa-f]+|\d+)', body)]
    return [v - 0x10000 if v >= 0x8000 else v for v in (n & 0xFFFF for n in numbers)]


def rotation(axis, angle, table=None):
    """A turn of `angle` about an axis; with `table`, by Sony's sines."""
    if table:
        index = abs(angle) % TURN
        s, c = table[2 * index] / float(ONE), table[2 * index + 1] / float(ONE)
        s = -s if angle < 0 else s
    else:
        s, c = math.sin(angle * 2 * math.pi / TURN), math.cos(angle * 2 * math.pi / TURN)
    if axis == 'x':
        return [[1, 0, 0], [0, c, -s], [0, s, c]]
    if axis == 'y':
        return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    return [[c, -s, 0], [s, c, 0], [0, 0, 1]]


def times(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def rows(numbers):
    return [numbers[0:3], numbers[3:6], numbers[6:9]]


def close(checks, what, got, want, tolerance):
    for i in range(3):
        for j in range(3):
            checks.near('%s [%d][%d]' % (what, i, j), got[i][j], want[i][j], tolerance)


def check(lines, checks):
    angles = a = b = scale = first = second = weight = None
    zyx = None
    worst = {'math_Sqrt': 0.0, 'SquareRoot0': 0.0, 'SquareRoot12': 0.0}
    table = sony_table()
    checks.equal("the entries of Sony's table", len(table), 2 * TURN)
    off = max(abs(table[2 * i] - ONE * math.sin(i * 2 * math.pi / TURN)) for i in range(TURN))
    checks.true("Sony's sines are within 8 of the real ones (%.1f)" % off, off < 8)
    for line in lines:
        word = line.split()
        kind, n = word[0], [int(v) for v in word[1:]]
        if kind == 'root':
            x = n[0]
            true = math.sqrt(x)
            # a root from a table of 192 or 256 entries: never above the
            # real one (but for rounding in the last place), and below it
            # by what the bits the table does not see are worth
            for name, got, want in (('math_Sqrt', n[1], true), ('SquareRoot0', n[2], true),
                                    ('SquareRoot12', n[3], true * 64)):
                checks.true('%s(%d) = %d is not above the root' % (name, x, got), got <= want + 1)
                low = want * (1 - 1.0 / 64) - 1
                checks.true('%s(%d) = %d is within a 64th of %f' % (name, x, got, want), got >= low)
                if want > 1024:
                    worst[name] = max(worst[name], (want - got) / want)
        elif kind == 'angles':
            angles = n
            rx, ry, rz = (rotation(axis, angle) for axis, angle in zip('xyz', angles))
            sx, sy, sz = (rotation(axis, angle, table) for axis, angle in zip('xyz', angles))
        elif kind == 'xyz':
            want = times(sx, times(sy, sz))
            close(checks, 'RotMatrix %s' % angles, rows(n), [[v * ONE for v in row] for row in want], 3)
        elif kind == 'zyx':
            zyx = rows(n)
            want = times(sz, times(sy, sx))
            close(checks, 'RotMatrixZYX %s' % angles, zyx, [[v * ONE for v in row] for row in want], 3)
        elif kind == 'game':
            want = times(rz, times(ry, rx))
            # the game's table drops the sines' fractions, and so does
            # every product after it
            close(checks, 'math_RotMatrixZYX %s' % angles, rows(n), [[v * ONE for v in row] for row in want], 5)
        elif kind in ('x', 'y', 'z'):
            # the matrix just made, turned about one axis by another of
            # the three angles
            angle = {'x': angles[1], 'y': angles[2], 'z': angles[0]}[kind]
            close(checks, 'RotMatrix%s %d' % (kind.upper(), angle), rows(n),
                  times(rotation(kind, angle, table), zyx), 3)
        elif kind == 'a':
            a = rows(n)
        elif kind == 'b':
            b = rows(n)
        elif kind == 'translations':
            ta, tb = n[0:3], n[3:6]
        elif kind == 'product':
            want = [[sum(a[i][k] * b[k][j] for k in range(3)) >> 12 for j in range(3)] for i in range(3)]
            checks.equal('MulMatrix0', rows(n), want)
        elif kind == 'composed':
            want = [(sum(a[i][k] * tb[k] for k in range(3)) >> 12) + ta[i] for i in range(3)]
            checks.equal('CompMatrix translation', n, want)
        elif kind == 'scale':
            scale = n
        elif kind == 'columns':
            checks.equal('ScaleMatrix', rows(n), [[a[i][j] * scale[j] >> 12 for j in range(3)] for i in range(3)])
        elif kind == 'rows':
            checks.equal('ScaleMatrixL', rows(n), [[a[i][j] * scale[i] >> 12 for j in range(3)] for i in range(3)])
        elif kind == 'long':
            # the vector goes through in two halves, so the result is the
            # true one to within the two halves' rounding
            for i in range(3):
                true = sum(a[i][k] * n[k] for k in range(3)) / 4096.0
                checks.near('ApplyMatrixLV [%d]' % i, n[3 + i], true, 1.0)
        elif kind == 'short':
            want = [sum(a[i][k] * n[k] for k in range(3)) >> 12 for i in range(3)]
            checks.equal('ApplyMatrix', n[3:6], want)
        elif kind == 'weight':
            weight = n[0]
        elif kind == 'first':
            first = rows(n)
        elif kind == 'second':
            second = rows(n)
        elif kind == 'blend':
            blend = rows(n)
            # a rotation: rows of length one, square to each other, the
            # third the first crossed with the second
            for i in range(3):
                length = math.sqrt(sum(v * v for v in blend[i]))
                checks.near('blend row %d length' % i, length, ONE, 48)
                dot = sum(blend[i][k] * blend[(i + 1) % 3][k] for k in range(3)) / float(ONE)
                checks.near('blend rows %d and %d' % (i, (i + 1) % 3), dot, 0, 48)
            cross = [blend[0][1] * blend[1][2] - blend[0][2] * blend[1][1],
                     blend[0][2] * blend[1][0] - blend[0][0] * blend[1][2],
                     blend[0][0] * blend[1][1] - blend[0][1] * blend[1][0]]
            for k in range(3):
                checks.near('blend row 2 [%d]' % k, blend[2][k], cross[k] / float(ONE), 2)
            # and it lies between the two: no further from either than
            # they are from each other
            def apart(p, q):
                return math.sqrt(sum((p[i][k] - q[i][k]) ** 2 for i in range(3) for k in range(3)))
            span = apart(first, second)
            checks.true('blend at %d is between the two' % weight,
                        apart(blend, first) <= span + 64 and apart(blend, second) <= span + 64)
        else:
            checks.true('a line the test does not know: ' + line, False)
    for name in sorted(worst):
        checks.note('%s is at most %.2f%% below the real root' % (name, 100 * worst[name]))
