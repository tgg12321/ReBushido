"""The rotation matrices against the same rotations in floating point.

A function's name gives the order its three turns are multiplied in:
math_RotMatrixZXY makes Rz * Rx * Ry, where Rx, Ry and Rz are the usual
matrices of a turn about one axis. Applied to a point (as a column) that is
the turn about y first and the one about z last, about the fixed axes; or,
which is the same, about z first, then about the x and the y axes as the
turns before have left them."""
import math

ANGLE_TURN = 0x1000
ONE = 0x1000
# How far an entry may be from the exact one: the sines are cut to twelve
# bits and so is each product of two of them.
TOLERANCE = 5
# The angles read back out of a matrix come from a table of arctangents, of
# a matrix that was already rounded: the matrix made from them again is this
# close to the first.
BACK_TOLERANCE = 12


def turn(axis, angle):
    s = math.sin(2 * math.pi * angle / ANGLE_TURN)
    c = math.cos(2 * math.pi * angle / ANGLE_TURN)
    if axis == 'X':
        return [[1, 0, 0], [0, c, -s], [0, s, c]]
    if axis == 'Y':
        return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    return [[c, -s, 0], [s, c, 0], [0, 0, 1]]


def product(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def rotation(order, angles):
    """The product of the three turns in the order of the name."""
    by_axis = {'X': angles[0], 'Y': angles[1], 'Z': angles[2]}
    result = turn(order[0], by_axis[order[0]])
    for axis in order[1:]:
        result = product(result, turn(axis, by_axis[axis]))
    return result


def check(lines, checks):
    for line in lines:
        words = line.split()
        label, values = words[0], [int(w) for w in words[1:]]
        if label == '2D':
            # a point turned about the origin: x towards y
            x, y, angle, got_x, got_y = values
            s = math.sin(2 * math.pi * angle / ANGLE_TURN)
            c = math.cos(2 * math.pi * angle / ANGLE_TURN)
            checks.near('math_Rotate2D(%d, %d, %d): x' % (x, y, angle), got_x, x * c - y * s, 2)
            checks.near('math_Rotate2D(%d, %d, %d): y' % (x, y, angle), got_y, x * s + y * c, 2)
            continue
        angles, got = values[:3], values[3:]
        order, tolerance = (label, TOLERANCE) if label != 'back' else ('YXZ', BACK_TOLERANCE)
        want = rotation(order, angles)
        worst = max(abs(got[3 * i + j] - ONE * want[i][j]) for i in range(3) for j in range(3))
        checks.true('%s of %s is %s, %d from the exact matrix' % (label, angles, got, round(worst)),
                    worst <= tolerance)
