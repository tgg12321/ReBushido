"""The sound chip's rules, worked out a second time for tests/port/spu.c.

What the program printed of the decoder, the interpolation and the
envelope is made again here from the rules as they are written down for
the hardware, in a few lines each, and not from src/port/spu.c. The
interpolation's table has no formula: it is held to what such a table
must be. The reverb is held to what any reverb must do.
"""

FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def clamp(value):
    return max(-0x8000, min(0x7FFF, value))


def signed16(value):
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def decode(header, data, old, older):
    """A block's 28 samples: four bits each, the low four of a byte first,
    at the top of 16 bits, shifted down; then the filter's share of the two
    samples before, in 64ths, rounded."""
    shift = header & 0xF
    if shift > 12:
        shift = 9
    f = (header >> 4) & 7
    first, second = FILTERS[f] if f < 5 else (0, 0)
    out = []
    for i in range(28):
        nibble = (data[i // 2] >> 4) if i & 1 else (data[i // 2] & 0xF)
        sample = signed16(nibble << 12) >> shift
        sample += (old * first + older * second + 32) >> 6
        sample = clamp(sample)
        out.append(sample)
        older, old = old, sample
    return out, old, older


def levels(start, rate, down, exponential, samples, every):
    """A level stepped `samples` times: the step is 7 less the rate's low
    two bits going up, 8 less them going down, doubled for each of the
    rate's shift under 11, and taken every 2^(shift - 11) samples above
    it; exponential, a fall's step is the level's share of it and a rise
    waits four times as long above 0x6000."""
    level, waited, out = start, 0, []
    for i in range(1, samples + 1):
        shift = rate >> 2
        step = (rate & 3) - 8 if down else 7 - (rate & 3)
        cycles = 1
        if shift < 11:
            step *= 1 << (11 - shift)
        else:
            cycles = 1 << (shift - 11)
        if exponential:
            if down:
                step = (step * level) >> 15
            elif level > 0x6000:
                cycles *= 4
        waited += 1
        if waited >= cycles:
            waited = 0
            level = max(0, min(0x7FFF, level + step))
        if i % every == 0:
            out.append(level)
    return out


def envelope(low, high, runs, per_run, off_at):
    """A voice's envelope register after each run of samples: attack to
    the top, decay to the sustain level, sustain, and from the key off
    the release."""
    phase, level, waited, out = 'attack', 0, 0, []

    def step(rate, down, exponential):
        nonlocal level, waited
        got = levels_one(level, waited, rate, down, exponential)
        level, waited = got

    for run in range(runs):
        if run == off_at and phase != 'off':
            phase, waited = 'release', 0
        for _ in range(per_run):
            before = phase
            if phase == 'attack':
                step((low >> 8) & 0x7F, False, bool(low & 0x8000))
                if level >= 0x7FFF:
                    phase = 'decay'
            elif phase == 'decay':
                step(((low >> 4) & 0xF) << 2, True, True)
                if level <= ((low & 0xF) + 1) * 0x800:
                    phase = 'sustain'
            elif phase == 'sustain':
                step((high >> 6) & 0x7F, bool(high & 0x4000), bool(high & 0x8000))
            elif phase == 'release':
                step((high & 0x1F) << 2, True, bool(high & 0x20))
                if level == 0:
                    phase = 'off'
            if phase != before:
                waited = 0
        out.append(level)
    return out


def levels_one(level, waited, rate, down, exponential):
    shift = rate >> 2
    step = (rate & 3) - 8 if down else 7 - (rate & 3)
    cycles = 1
    if shift < 11:
        step *= 1 << (11 - shift)
    else:
        cycles = 1 << (shift - 11)
    if exponential:
        if down:
            step = (step * level) >> 15
        elif level > 0x6000:
            cycles *= 4
    waited += 1
    if waited >= cycles:
        return max(0, min(0x7FFF, level + step)), 0
    return level, waited


INTERPOLATION_CASES = (
    (0, 0, 0, 0x7FFF),
    (0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF),
    (-0x8000, -0x8000, -0x8000, -0x8000),
    (1000, -2000, 30000, -32768),
    (-123, 4567, -8901, 23456),
)


def check(lines, checks):
    data, gauss = None, None
    for line in lines:
        word = line.split()
        if word[0] == 'adpcm_data':
            data = [int(v) for v in word[1:]]
        elif word[0] == 'gauss':
            gauss = [int(v) for v in word[1:]]
    checks.true('the test printed its waveform and the table', data is not None and gauss is not None)

    # ---- the decoder: two blocks in a row for each header ----
    old = older = 0
    blocks = 0
    for line in lines:
        word = line.split()
        if word[0] != 'adpcm':
            continue
        header = int(word[1])
        if blocks % 2 == 0:
            old = older = 0
        want, old, older = decode(header, data, old, older)
        checks.equal('a block with header 0x%02X' % header, [int(v) for v in word[2:]], want)
        blocks += 1
    checks.true('every filter was decoded', blocks >= 20)

    # ---- the table: 512 weights that rise smoothly from nothing, and
    # whose four for any place add up to one ----
    checks.equal('the table has 512 weights', len(gauss), 512)
    checks.true('the table never falls', all(b >= a for a, b in zip(gauss, gauss[1:])))
    checks.equal('it starts under nothing and ends at its top', (gauss[0], gauss[511]), (-1, 0x59B3))
    second = [gauss[i + 2] - 2 * gauss[i + 1] + gauss[i] for i in range(510)]
    third = [b - a for a, b in zip(second, second[1:])]
    checks.true('it is smooth: its slope changes by 2 at most', max(abs(v) for v in second) <= 2)
    checks.true('and that change by 3 at most', max(abs(v) for v in third) <= 3)
    sums = [gauss[255 - i] + gauss[511 - i] + gauss[256 + i] + gauss[i] for i in range(256)]
    checks.true("a place's four weights add up to one, within a part in 32,000 (they are %d to %d of 32768)"
                % (min(sums), max(sums)), min(sums) >= 32639 and max(sums) <= 32641)

    # ---- the interpolation: each sample by its weight, each product's
    # fraction dropped ----
    seen = 0
    for line in lines:
        word = line.split()
        if word[0] != 'interp':
            continue
        case, index, got = int(word[1]), int(word[2]), int(word[3])
        s = INTERPOLATION_CASES[case]
        want = (((gauss[0xFF - index] * s[0]) >> 15) + ((gauss[0x1FF - index] * s[1]) >> 15)
                + ((gauss[0x100 + index] * s[2]) >> 15) + ((gauss[index] * s[3]) >> 15))
        checks.equal('samples %s at %d/256' % (s, index), got, want)
        seen += 1
    checks.equal('interpolations seen', seen, 30)

    # ---- the envelope ----
    seen = 0
    for line in lines:
        word = line.split()
        if word[0] == 'level':
            start, rate, down, exponential, samples, every = [int(v) for v in word[1:7]]
            checks.equal('a level from %d at rate 0x%02X%s%s' % (start, rate, ' down' if down else ' up',
                                                                 ' exponential' if exponential else ''),
                         [int(v) for v in word[7:]], levels(start, rate, down, exponential, samples, every))
            seen += 1
        elif word[0] == 'adsr':
            low, high = int(word[1]), int(word[2])
            got = [int(v) for v in word[3:]]
            want = envelope(low, high, 60, 200, 40)
            checks.equal("a voice's whole envelope", got, want)
            checks.true('it rose to the top, fell to its sustain level, and was released to nothing',
                        max(got) > 0x7800 and got[-1] == 0 and 0 < got[39] < 0x6000)
            seen += 1
    checks.equal('envelopes seen', seen, 11)

    # ---- the reverb: something comes back after the click, never louder
    # than the click, and it dies away ----
    modes = 0
    for line in lines:
        word = line.split()
        if word[0] != 'reverb':
            continue
        mode = int(word[1])
        peaks = [int(v) for v in word[2:14]]
        first = int(word[15])
        checks.true('mode %d: something comes back (first at sample %d)' % (mode, first), first > 8 and max(peaks) > 0)
        checks.true('mode %d: never louder than the click (%d of 24576)' % (mode, max(peaks)), max(peaks) < 0x6000)
        # the echo keeps nearly all of itself each time round, by its
        # settings (a wall of -0x7F00): it is only asked not to grow
        if mode == 7:
            checks.true('mode 7, the echo, does not grow: %s' % peaks, max(peaks[6:]) <= max(peaks[:6]))
        else:
            checks.true('mode %d dies away: %s' % (mode, peaks),
                        max(peaks[8:]) * 4 <= max(peaks[:4]) and peaks[11] <= max(peaks[:2]))
        modes += 1
    checks.equal("Sony's nine modes were run", modes, 9)
