"""math_PointInTriangleXZ and math_InvertMatrix against exact arithmetic.

A point is inside a triangle if it is on the inner side of all three edges.
The game's test answers for a point exactly on an edge by which way round
the triangle is wound, so those points are not judged here, only counted.

The inverse of a matrix with twelve fraction bits: the product of the two
is ONE on the diagonal and nothing off it."""
from fractions import Fraction

ONE = 0x1000
# How far an entry of matrix * inverse may be from ONE or from 0, of ONE.
INVERSE_TOLERANCE = 16


def side(a, b, p):
    """Positive on one side of the line from a to b, negative on the
    other, zero on it."""
    return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])


def inside(triangle, p):
    """True or False, or None for a point on the triangle's outline."""
    a, b, c = triangle
    signs = [side(a, b, p), side(b, c, p), side(c, a, p)]
    if all(s > 0 for s in signs) or all(s < 0 for s in signs):
        return True
    if all(s >= 0 for s in signs) or all(s <= 0 for s in signs):
        return None
    return False


def check(lines, checks):
    triangle = None
    on_outline = 0
    for line in lines:
        words = line.split()
        values = [int(w) for w in words[1:]]
        if words[0] == 'triangle':
            triangle = [values[0:2], values[2:4], values[4:6]]
        elif words[0] == 'point':
            want = inside(triangle, values[:2])
            if want is None:
                on_outline += 1
            else:
                checks.equal('point %s in triangle %s' % (values[:2], triangle),
                             values[2], int(want))
        else:
            matrix = [values[0:3], values[3:6], values[6:9]]
            inverse = [values[9:12], values[12:15], values[15:18]]
            worst = 0
            for i in range(3):
                for j in range(3):
                    entry = Fraction(sum(matrix[i][k] * inverse[k][j] for k in range(3)), ONE)
                    worst = max(worst, abs(entry - (ONE if i == j else 0)))
            checks.true('matrix %s times its inverse %s is %d from one'
                        % (matrix, inverse, worst), worst <= INVERSE_TOLERANCE)
    checks.note('%d points on a triangle\'s outline were not judged' % on_outline)
