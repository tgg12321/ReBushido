"""The port's picture decompressor, checked a second way.

Three things the C prints are worked out again here:

  - every code of the bitstream, against the table the original library
    itself looks codes up in (`movovl/asm/data/libpress_vlc.data.s`, which
    the decompilation of the movie overlay holds): the port's table has to
    name the same run and value for the same bits, and no others;
  - the blocks of coefficients, by the chip's arithmetic written out again
    from the rules, and by a real inverse cosine transform in floating
    point, which the chip's fixed point has to come within a few of;
  - the step from brightness and colour difference to red, green and blue.
"""
import re

LISTING = 'movovl/asm/data/libpress_vlc.data.s'
# How many bits of the bitstream the original library's own table is
# looked up by. (Not `BITS`, which tests/run.py reads as the sizes of
# pointer a test is for.)
LOOKUP_BITS = 13

# Sony's own quantisation and cosine tables, which the port has a copy of
# (src/port/mdec.c): the bytes of libpress's mdec_iq and mdec_coef.
IQ_WORDS = [0x13101002, 0x16161310, 0x16161616, 0x1B1A181A, 0x1A1A1B1B,
            0x1B1B1A1A, 0x1D1D1D1B, 0x1D222222, 0x1B1B1D1D, 0x20201D1D,
            0x26252222, 0x22232325, 0x28262623, 0x30302828, 0x38382E2E,
            0x5345453A]
COEF_WORDS = [0x5A825A82, 0x5A825A82, 0x5A825A82, 0x5A825A82, 0x6A6D7D8A,
              0x18F8471C, 0xB8E3E707, 0x82759592, 0x30FB7641, 0x89BECF04,
              0xCF0489BE, 0x764130FB, 0xE7076A6D, 0xB8E38275, 0x7D8A471C,
              0x959218F8, 0xA57D5A82, 0x5A82A57D, 0xA57D5A82, 0x5A82A57D,
              0x8275471C, 0x6A6D18F8, 0xE7079592, 0xB8E37D8A, 0x89BE30FB,
              0xCF047641, 0x7641CF04, 0x30FB89BE, 0xB8E318F8, 0x82756A6D,
              0x95927D8A, 0xE707471C]

ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
          12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
          35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
          58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63]


def bytes_of(words):
    out = []
    for w in words:
        out += [w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, (w >> 24) & 0xFF]
    return out


def halfwords_of(words):
    out = []
    for w in words:
        for h in (w & 0xFFFF, w >> 16):
            out.append(h - 0x10000 if h >= 0x8000 else h)
    return out


QUANT = bytes_of(IQ_WORDS)
SCALE = halfwords_of(COEF_WORDS)


def listing_codes():
    """{code as a string of bits: run-level halfword} out of the original
    library's own tables. The first is read for the next 13 bits; every code
    longer than that begins with eight zeroes and is read for the nine bits
    after them."""
    tables, current = {}, None
    for line in open(LISTING, encoding='utf-8'):
        m = re.match(r'\s*dlabel (\w+)', line)
        if m:
            current = m.group(1)
            tables[current] = []
            continue
        m = re.search(r'\.word 0x([0-9A-Fa-f]{8})', line)
        if m and current:
            tables[current].append(int(m.group(1), 16))
    half = {}
    for name, words in tables.items():
        flat = []
        for w in words:
            flat.append(w & 0xFFFF)
            flat.append(w >> 16)
        half[name] = flat
    short = [tuple(half['D_801E4638'][4 * i:4 * i + 4]) for i in range(1 << LOOKUP_BITS)]
    long_ = [tuple(half['D_801F4638'][4 * i:4 * i + 4]) for i in range(1 << 8)]
    found = {}
    # a code of 13 bits or fewer: the run of windows that begins with it all
    # say the same thing, and the shortest of them is its length
    for length in range(1, LOOKUP_BITS + 1):
        for prefix in range(1 << length):
            low = prefix << (LOOKUP_BITS - length)
            group = short[low:low + (1 << (LOOKUP_BITS - length))]
            if any(e[0] == 0 for e in group):
                continue
            if min(e[0] for e in group) != length:
                continue
            if len({e[1] for e in group}) != 1:
                continue
            code = format(prefix, '0%db' % length)
            if any(code.startswith(c) for c in found):
                continue
            found[code] = group[0][1]
    # a longer one: eight zeroes and then what the second table says
    for index, entry in enumerate(long_):
        if entry[0] == 0:
            continue
        if entry[0] == 9:
            head = '00000000' + format(index, '08b')
            found[head + '0'] = entry[1]
            found[head + '1'] = entry[3]
        else:
            found['00000000' + format(index >> (8 - entry[0]), '0%db' % entry[0])] = entry[1]
    return found


def rl(value):
    run = (value >> 10) & 0x3F
    level = value & 0x3FF
    return run, level - 0x400 if level >= 0x200 else level


def clamp(v, low, high):
    return low if v < low else (high if v > high else v)


def idct(block):
    """The chip's transform as it is described: two passes of the rows times
    the cosine table, each leaving the answer turned about its diagonal."""
    src = list(block)
    for _ in range(2):
        dst = [0] * 64
        for a in range(8):
            for b in range(8):
                total = 0
                for z in range(8):
                    total += src[a * 8 + z] * int(SCALE[z * 8 + b] / 8)
                dst[b * 8 + a] = (total + 0x0FFF) >> 13
        src = dst
    return src


def real_idct(block):
    """The same transform in real arithmetic, for a second opinion."""
    import math
    out = []
    for y in range(8):
        for x in range(8):
            total = 0.0
            for v in range(8):
                for u in range(8):
                    if block[v * 8 + u] == 0:
                        continue
                    cu = math.sqrt(0.5) if u == 0 else 1.0
                    cv = math.sqrt(0.5) if v == 0 else 1.0
                    total += (cu * cv * block[v * 8 + u]
                              * math.cos((2 * x + 1) * u * math.pi / 16)
                              * math.cos((2 * y + 1) * v * math.pi / 16))
            out.append(total / 4.0)
    return out


def decode_block(scale, chroma, dc, runs, levels):
    """The run-level pairs to a block, by the rules: the lowest coefficient
    times the first entry of the table, the rest times the table and the
    frame's scale, each held to ten bits with a sign, laid out along the
    zigzag."""
    quant = QUANT  # the two tables start the same
    assert chroma in (0, 1)
    work = [0] * 64
    value = dc * quant[0]
    k = 0
    raw = dc
    for i in range(4):
        if scale == 0:
            value = raw * 2
        value = clamp(value, -0x400, 0x3FF)
        work[ZIGZAG[k] if scale else k] = value
        if i == 3:
            break
        k += runs[i] + 1
        if k >= 64:
            break
        raw = levels[i]
        value = (levels[i] * quant[k] * scale + 4) >> 3
    return work


def check(lines, checks):
    for line in lines:
        words = line.split()
        if words[0] == 'quant':
            checks.equal("the chip's quantisation table is the library's",
                         [int(w) for w in words[1:]], QUANT)
        elif words[0] == 'scale':
            checks.equal("the chip's cosine table is the library's",
                         [int(w) for w in words[1:]], SCALE)

    table = listing_codes()
    seen = {}
    for line in lines:
        words = line.split()
        if words[0] != 'code':
            continue
        if words[-1] in ('end', 'escape'):
            bits, length = int(words[1]), int(words[2])
            value = 0xFE00 if words[-1] == 'end' else 0x7C1F
        else:
            bits, length, runv, level = (int(w) for w in words[1:5])
            value = ((runv & 0x3F) << 10) | (level & 0x3FF)
        code = format(bits, '0%db' % length)
        seen[code] = value
    checks.equal('as many codes as the original library has', len(seen), len(table))
    bad = [c for c in sorted(table) if seen.get(c) != table[c]]
    checks.equal("every code of the library is the port's too", bad[:4], [])
    extra = [c for c in sorted(seen) if c not in table]
    checks.equal('the port has no code the library has not', extra[:4], [])

    blocks = 0
    for line in lines:
        words = line.split()
        if words[0] != 'block':
            continue
        scale, chroma, dc = int(words[1]), int(words[2]), int(words[3])
        runs = [int(words[4]), int(words[6]), int(words[8])]
        levels = [int(words[5]), int(words[7]), int(words[9])]
        got = [int(w) for w in words[10:74]]
        coefficients = decode_block(scale, chroma, dc, runs, levels)
        checks.equal("block %d: the chip's arithmetic" % blocks, got, idct(coefficients))
        real = real_idct(coefficients)
        worst = max(abs(a - b) for a, b in zip(got, real))
        checks.true('block %d: within 2 of the real transform (worst %.2f)'
                    % (blocks, worst), worst <= 2)
        blocks += 1
    checks.true('the blocks were run', blocks >= 5)

    colors = 0
    for line in lines:
        words = line.split()
        if words[0] != 'color':
            continue
        y, cb, cr = (int(w) for w in words[1:4])
        got = [int(w) for w in words[4:7]]
        want = [clamp(y + ((22970 * cr) >> 14), -128, 127) + 128,
                clamp(y - ((5631 * cb) >> 14) - ((11703 * cr) >> 14), -128, 127) + 128,
                clamp(y + ((29032 * cb) >> 14), -128, 127) + 128]
        checks.equal('colour %d: %d %d %d' % (colors, y, cb, cr), got, want)
        colors += 1
    checks.true('the colours were run', colors >= 4)
