"""The game's random numbers against the rule main.c's comment gives: each
number is the last one's high half mixed (exclusive or) with the last one
times 5497 plus 0x7FA9, in 32 signed bits; what is handed out is its low 15
bits. And the projection distance against the tangent: half the screen's
640 over the tangent of half the field of view."""
import math

MULTIPLIER = 5497
INCREMENT = 0x7FA9


def signed32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


def check(lines, checks):
    state = {}
    counts = [0] * 64
    draws = 0
    for line in lines:
        kind, a, b = line.split()
        a, b = int(a), int(b)
        if kind == 'rng':
            last = state.get(a, a)   # the first of a seed starts from the seed
            now = signed32((last >> 16) ^ (last * MULTIPLIER + INCREMENT))
            state[a] = now
            checks.equal('the next number after %d' % last, b, now & 0x7FFF)
            counts[b & 0x3F] += 1
            draws += 1
        else:
            exact = 320 / math.tan(math.radians(a) / 2)
            # the angle is cut to a 4096th of a turn and the sine to twelve
            # bits: a narrow view, whose tangent is small, loses the most
            checks.near('the distance for a view of %d degrees' % a, b, exact, exact / 50 + 1)
    # the game mostly uses a few low bits of a number (& 3, & 7, & 0x3F):
    # every value of the low six should come up about as often as the rest
    expected = draws / 64
    for value, count in enumerate(counts):
        checks.true('the low six bits were %d in %d of %d numbers' % (value, count, draws),
                    0.6 * expected < count < 1.4 * expected)
