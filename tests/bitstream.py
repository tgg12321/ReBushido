"""The bit reader against Python's: the stream is words of 32 bits, read
from each word's top bit down, a field's first bit its most significant.

A track is six values; a frame of it is, for each value, four bits that say
how wide its change is (0 for sixteen) and then the change, signed."""

TRACK_VALUES = 6


class Bits:
    """The stream as one long string of '0' and '1'."""

    def __init__(self, words):
        self.text = ''.join(format(word, '032b') for word in words)
        self.at = 0

    def read(self, width):
        field = self.text[self.at:self.at + width]
        self.at += width
        return int(field, 2)


def signed(value, width):
    return value - (1 << width) if value >> (width - 1) else value


def check(lines, checks):
    rows = [line.split() for line in lines]
    words = [int(row[1]) for row in rows if row[0] == 'word']

    bits = Bits(words)
    for index, row in enumerate(r for r in rows if r[0] == 'bits'):
        width, got = int(row[1]), int(row[2])
        checks.equal('read %d (%d bits)' % (index, width), got, bits.read(width))

    bits = Bits(words)
    values = [0] * TRACK_VALUES
    for frame, row in enumerate(r for r in rows if r[0] == 'track'):
        for i in range(TRACK_VALUES):
            width = bits.read(4) or 16
            # a value is 16 bits and wraps
            values[i] = signed((values[i] + signed(bits.read(width), width)) & 0xFFFF, 16)
        checks.equal('the track after frame %d' % frame, [int(w) for w in row[1:]], values)
