"""The sine table against math.sin: every entry is ONE times the sine of its
angle with the fraction dropped (towards zero, not to the nearest: Sony's
rsin table is the same)."""
import math

ANGLE_TURN = 0x1000
ONE = 0x1000
# what a sine that is a whole number may be out by, as a float
FLOAT_SLACK = 1e-6


def check(lines, checks):
    checks.equal('the number of entries', len(lines), ANGLE_TURN)
    for angle, line in enumerate(lines):
        exact = ONE * math.sin(2 * math.pi * angle / ANGLE_TURN)
        entry = int(line)
        short = abs(exact) - abs(entry)  # the fraction that was dropped
        checks.true('sin_table[%d] is %d, and %d times the sine is %.3f' % (angle, entry, ONE, exact),
                    -FLOAT_SLACK <= short < 1 and (entry == 0 or (entry > 0) == (exact > 0)))
