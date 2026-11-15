"""XA sound's format, worked out a second time for tests/port/xa.c.

The program prints each made-up sector's bytes and what the port decoded
of them; this decodes the same bytes from the format as it is written
down (a sector is 18 groups of 128 bytes: 16 of headers, of which the
blocks' own are from the fourth, then 28 rows of the blocks' samples),
not from src/port/xa.c.
"""

FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55))


def signed16(value):
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def decode(data, coding):
    """Every sample of a sector's 2304 bytes of groups: a channel's blocks
    one after another, and in stereo a left and a right by turns."""
    stereo, wide = bool(coding & 1), bool(coding & 0x10)
    blocks = 4 if wide else 8
    history = [[0, 0], [0, 0]]
    out = []
    for g in range(18):
        group = data[g * 128:(g + 1) * 128]
        decoded = []
        for block in range(blocks):
            header = group[4 + block]
            shift = header & 0xF
            if wide and shift > 8:
                shift = 8
            if not wide and shift > 12:
                shift = 9
            first, second = FILTERS[(header >> 4) & 3]
            side = history[block & 1] if stereo else history[0]
            samples = []
            for i in range(28):
                if wide:
                    sample = signed16(group[16 + i * 4 + block] << 8) >> shift
                else:
                    byte = group[16 + i * 4 + block // 2]
                    sample = signed16(((byte >> 4) if block & 1 else (byte & 0xF)) << 12) >> shift
                sample += (side[0] * first + side[1] * second + 32) >> 6
                sample = max(-0x8000, min(0x7FFF, sample))
                samples.append(sample)
                side[1], side[0] = side[0], sample
            decoded.append(samples)
        if stereo:
            for pair in range(0, blocks, 2):
                for i in range(28):
                    out += [decoded[pair][i], decoded[pair + 1][i]]
        else:
            for samples in decoded:
                out += samples
    return out


def check(lines, checks):
    sectors = 0
    for i, line in enumerate(lines):
        word = line.split()
        if word[0] != 'sector':
            continue
        name, coding, count, stereo, half_rate = word[1], int(word[2]), int(word[3]), int(word[4]), int(word[5])
        data = [int(v) for v in lines[i + 1].split()[1:]]
        got = [int(v) for v in lines[i + 2].split()[1:]]
        checks.equal('%s: the bytes of 18 groups' % name, len(data), 18 * 128)
        checks.equal('%s: stereo, half rate' % name, (stereo, half_rate), (coding & 1, (coding >> 2) & 1))
        want = decode(data, coding)
        checks.equal('%s: how many samples' % name, count, len(want))
        checks.equal('%s: the samples' % name, got, want)
        checks.true('%s: the made-up bytes reach both limits of 16 bits, and neither stays there'
                    % name, max(want) > 20000 and min(want) < -20000 and len(set(want)) > 500)
        sectors += 1
    checks.equal('sectors decoded', sectors, 4)
