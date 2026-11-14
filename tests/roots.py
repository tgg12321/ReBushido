"""The root tables and math_TableSqrt against math.isqrt and math.sqrt.

The tables hold their roots with the fraction dropped. math_TableSqrt is
exact below the table's size; above it the number is cut down to its top
nine or ten bits first, so the root comes out a little low: never above the
real root, and never more than a 32nd of it below. A number with the top bit
set is rooted as the unsigned number it is."""
import math

TABLE_SIZE = 0x400


def check(lines, checks):
    for line in lines:
        kind, x, got = line.split()
        x, got = int(x), int(got)
        if kind == 'sqrt8':
            # the root of x, times 8
            checks.equal('g_math_sqrt8_table[%d]' % x, got, math.isqrt(64 * x))
        elif kind == 'sqrt512':
            # the root of x, times 512
            checks.equal('g_math_sqrt_table[%d]' % x, got, math.isqrt(512 * 512 * x))
        elif kind == 'rsqrt':
            # 0x1000 over the root of x / 64
            checks.equal('g_math_rsqrt_table[%d]' % (x - 64), got, int(0x1000 / math.sqrt(x / 64)))
        else:
            exact = math.isqrt(x)
            low_by = exact - got
            allowed = 0 if x < TABLE_SIZE else exact / 32
            checks.true('math_TableSqrt(%d) is %d; the real root is %d' % (x, got, exact),
                        0 <= low_by <= allowed)
