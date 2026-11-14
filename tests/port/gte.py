"""The port's GTE: its division against a division done here.

The coprocessor divides by multiplying with a reciprocal it gets from a
small table and one step of Newton's rule, so its quotient is not always
the true one. The program prints a sample of what the model's division
gave; each must be within three of h / depth with 16 bits of fraction,
rounded to nearest, and most must be that number itself. (Three is the
most the model was ever seen to be off by, over 46 million pairs. The
program itself holds every quotient of sixteen dividends to within four
divisors of the dividend.)
"""


def check(lines, checks):
    rows = [line.split() for line in lines]
    checks.true('a sample of the division was printed', len(rows) > 500)
    exact = 0
    for row in rows:
        h, depth, quotient = int(row[1]), int(row[2]), int(row[3])
        true = min(0x1FFFF, (h * 0x20000 // depth + 1) // 2)
        checks.near('%d / %d' % (h, depth), quotient, true, 3)
        exact += quotient == true
    # most are the true quotient to the last bit
    checks.true('%d of %d quotients are exact' % (exact, len(rows)), exact * 10 > len(rows) * 6)
