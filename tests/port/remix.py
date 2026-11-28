"""The sound remastered, measured, for tests/port/remix.c.

The program prints powers, a reverb's energy every 10 ms and a head's
clicks; this works out from them what each part is for, from the
formulas and not from src/port/remix.c:

  - the resampler: a tone the voice can hold is heard at its level, and
    what else is heard (images above, the kernel's error) is 70 dB under
    it; a tone a voice played fast puts above what the output holds is
    taken out, not folded back. The chip's own table is measured beside
    it, which loses the top and lets images and fold through;
  - the reverb: each room falls 60 dB at low frequencies in its decay
    (Schroeder's backward integral, its slope from -5 to -35 dB), the
    whole sound a little faster for the damping; it dies to nothing; on
    noise at full scale nothing outgrows what a line holds; its level
    against the direct sound is what the room says;
  - the head: from the side, the far ear hears a click Woodworth's delay
    later, softer and darker; from the middle, both ears the same.
"""
import cmath
import math

RATE = 44100
STEP_ONE = 0x1000


def db(power):
    return 10 * math.log10(max(power, 1e-30))


def parts(value):
    return int(value) / 1e9


def decay_time(energies, block_seconds):
    """T30 from 10 ms blocks of energy: the time Schroeder's backward
    integral takes from -5 to -35 dB, doubled."""
    total = float(sum(energies))
    if total <= 0:
        return None
    left = total
    level = []
    for e in energies:
        level.append(db(left / total))
        left -= e

    def at(target):
        for i, v in enumerate(level):
            if v <= target:
                return i * block_seconds
        return None
    t5, t35 = at(-5), at(-35)
    if t5 is None or t35 is None:
        return None
    return 2 * (t35 - t5)


def response(samples, hz):
    """The power of a click's answer at `hz`."""
    total = 0
    for n, v in enumerate(samples):
        total += v * cmath.exp(-2j * math.pi * hz * n / RATE)
    return abs(total) ** 2


def onset(samples):
    """The first sample above a tenth of the largest."""
    top = max(abs(v) for v in samples)
    for n, v in enumerate(samples):
        if abs(v) >= top / 10:
            return n
    return None


def check(lines, checks):
    tones = {}
    folds = []
    rooms = {}
    energies = {}
    lows = {}
    tails = {}
    noises = {}
    ears = {}
    head = None
    chip = None
    rollback = None
    stages = 0
    for line in lines:
        w = line.split()
        if not w:
            continue
        if w[0] == 'tone':
            tones[(int(w[1], 16), int(w[2]), w[3])] = (parts(w[4]), parts(w[5]))
        elif w[0] == 'fold':
            folds.append((int(w[1], 16), int(w[2]) / 1000.0, w[3], parts(w[4])))
        elif w[0] == 'room':
            room = int(w[1])
            lines_of = [(int(w[i + 1]), int(w[i + 2])) for i in range(7, len(w), 3)]
            rooms[room] = {'name': w[2], 'decay': int(w[4]) / 1000.0, 'wet': int(w[6]), 'lines': lines_of}
        elif w[0] == 'energy':
            energies[int(w[1])] = [int(v) for v in w[2:]]
        elif w[0] == 'low':
            lows[int(w[1])] = [int(v) for v in w[2:]]
        elif w[0] == 'tail':
            tails[int(w[1])] = {'silent': int(w[5]), 'length': int(w[7]), 'whole': int(w[9]) / 1e6}
        elif w[0] == 'noise':
            noises[int(w[1])] = (int(w[3]), int(w[5]), int(w[7]))
        elif w[0] == 'ear':
            ears[(w[1], w[2])] = [int(v) for v in w[3:]]
        elif w[0] == 'head':
            head = (int(w[2]), int(w[4]), int(w[6]))
        elif w[0] == 'chip':
            chip = int(w[2])
        elif w[0] == 'rollback':
            rollback = (int(w[2]), int(w[4]))
        elif w[0] == 'stage':
            stages += 1

    # ---- the resampler ----
    checks.true('the tones were played', len(tones) > 150 and len(folds) > 20)
    worst_in = -999
    worst_flat = 0
    better = 0
    for (step, k, kind), (gain, rest) in sorted(tones.items()):
        if kind != 'sinc':
            continue
        f_out = k / 4096.0
        chip_gain, chip_rest = tones[(step, k, 'chip')]
        if f_out <= 0.35:
            worst_flat = max(worst_flat, abs(db(gain)))
            worst_in = max(worst_in, db(rest) - db(gain))
        if f_out >= 0.05 and db(rest) < db(chip_rest):
            better += 1
    checks.true('the sinc is flat to 0.35 of the output rate, within 0.1 dB at every pitch (worst %.3f dB)'
                % worst_flat, worst_flat < 0.1)
    checks.true('and what else it plays of a tone is 70 dB under it (worst %.1f dB)' % worst_in, worst_in < -70)
    checks.note('resampler: flat within %.3f dB to 0.35 of the output rate; images, fold and error %.1f dB '
                'at worst' % (worst_flat, worst_in))
    for step, k in [(0x0659, 410), (0x0800, 614), (0x1000, 1229)]:
        gain, rest = tones[(step, k, 'sinc')]
        chip_gain, chip_rest = tones[(step, k, 'chip')]
        checks.note('  step 0x%04X, a tone at %.2f of the waveform\'s rate: sinc %+.2f dB, %.1f dB else; the chip '
                    '%+.2f dB, %.1f dB else (-90: all the test sees)' % (step, k / 4096.0 * STEP_ONE / step, db(gain),
                                                                     max(db(rest), -90),
                                                db(chip_gain), db(chip_rest)))
    counted = sum(1 for (step, k, kind) in tones if kind == 'sinc' and k / 4096.0 >= 0.05)
    checks.true('above 0.05 of the rate the sinc plays less that is not the tone than the chip, every time '
                '(%d of %d)' % (better, counted), better == counted)
    worst_fold = -999
    for step, f, kind, power in folds:
        if kind != 'sinc':
            continue
        nyquist = 0.5 * STEP_ONE / step
        chip_power = [p for s, ff, kk, p in folds if s == step and ff == f and kk == 'chip'][0]
        checks.true('a tone at %.3f played at step 0x%04X folds back %.1f dB less than the chip\'s (%.1f, %.1f)'
                    % (f, step, db(chip_power) - db(power), db(power), db(chip_power)),
                    db(power) < db(chip_power) - 15)
        if f >= 1.6 * nyquist:
            worst_fold = max(worst_fold, db(power))
    checks.true('a tone 1.6 times above what the output holds is 60 dB down (worst %.1f dB)' % worst_fold,
                worst_fold < -60)
    checks.note('folding back: 1.6 times above the output\'s Nyquist %.1f dB at worst' % worst_fold)

    # ---- the reverb ----
    checks.equal("the rooms' count", len(rooms), 19)
    checks.equal('every stage has a room', stages, 38)
    drr = {}
    for room, it in sorted(rooms.items()):
        # each line's gain is 60 dB over the decay: 10^(-3 length / (rate decay))
        for length, gain in it['lines']:
            want = 32768 * 10 ** (-3.0 * length / (RATE * it['decay']))
            if abs(gain - want) > max(2, want * 0.001):
                checks.true('room %s: a line of %d samples has the gain %d, not %.0f' % (it['name'], length, gain, want),
                            False)
        low = decay_time(lows[room], 0.01)
        whole = decay_time(energies[room], 0.01)
        checks.true('%s: falls 60 dB at low frequencies in %.2f s, its decay %.2f s within 6%%'
                    % (it['name'], low or -1, it['decay']), low is not None and abs(low - it['decay']) < 0.06 * it['decay'])
        checks.true('%s: the whole of it a little faster, for the damping (%.2f s)' % (it['name'], whole or -1),
                    whole is not None and 0.5 * it['decay'] < whole <= 1.02 * low)
        tail = tails[room]
        checks.true('%s: dies to nothing, %d samples after the click' % (it['name'], tail['silent']),
                    tail['silent'] < tail['length'])
        drr[room] = db(tail['whole'] * (it['wet'] / 32768.0) ** 2)
        checks.true('%s: the reverb %.1f dB against the direct sound, between -22 and -5' % (it['name'], drr[room]),
                    -22 < drr[room] < -5)
        checks.note('room %-9s decay %.2f s (low %.2f, whole %.2f), reverb %.1f dB against the direct sound'
                    % (it['name'], it['decay'], low or -1, whole or -1, drr[room]))
    names = {it['name']: room for room, it in rooms.items()}
    checks.true('the cave the most reverberant and the open the least',
                drr[names['cave']] == max(drr.values()) and drr[names['open']] <= min(drr.values()) + 1)
    checks.equal('three rooms were given noise', len(noises), 3)
    for room, (peak, line_peak, silent) in sorted(noises.items()):
        checks.true('%s on noise at full scale: its lines stay far within a word (%d)' % (rooms[room]['name'], line_peak),
                    line_peak < (1 << 28))
        checks.true('%s: and its noise dies to nothing %d samples after it stops' % (rooms[room]['name'], silent),
                    0 < silent < 8 * RATE * rooms[room]['decay'])

    # ---- the head ----
    checks.true('the head printed its clicks', head is not None and len(ears) == 10)
    if head is None or len(ears) != 10:
        return
    most, pole, speaker = head
    woodworth = 0.0875 / 343 * RATE * (math.pi / 2 + 1) * 256
    checks.true('the far ear of a sound at the side is Woodworth\'s %.2f samples later (%.2f)'
                % (woodworth / 256, most / 256.0), abs(most - woodworth) < 0.01 * woodworth)
    want_pole = 32768 * (1 - math.exp(-2 * 343 / 0.0875 / RATE))
    checks.true('the head\'s low pass is at 2c/a (%d, %.1f)' % (pole, want_pole), abs(pole - want_pole) < 2)
    for near, far, name in [('left', 'right', 'hard_left'), ('right', 'left', 'hard_right')]:
        a, b = ears[(name, near)], ears[(name, far)]
        delay = onset(b) - onset(a)
        energy = db(sum(v * v for v in a)) - db(sum(v * v for v in b))
        high = db(response(a, 8000)) - db(response(b, 8000))
        low = db(response(a, 200)) - db(response(b, 200))
        checks.true('%s: the far ear hears it %d samples later (%.2f ms)' % (name, delay, delay * 1000.0 / RATE),
                    abs(delay - woodworth / 256) <= 1.5)
        checks.true('%s: the near ear %.1f dB the louder' % (name, energy), energy > 6)
        checks.true('%s: at 8 kHz the far ear %.1f dB down, the head\'s shadow' % (name, high), high > 10)
        checks.true('%s: at 200 Hz only %.1f dB, a head being small against it' % (name, low), 0 < low < 4)
        checks.note('head %s: far ear %d samples later (%.2f ms), %.1f dB down in all, %.1f dB at 8 kHz, %.1f dB '
                    'at 200 Hz' % (name, delay, delay * 1000.0 / RATE, energy, high, low))
    checks.equal('left and right are mirrors', ears[('hard_left', 'left')], ears[('hard_right', 'right')])
    checks.equal('... both ears', ears[('hard_left', 'right')], ears[('hard_right', 'left')])
    middle = ears[('middle', 'left')]
    checks.equal('a sound in the middle is the same at both ears', middle, ears[('middle', 'right')])
    checks.equal('and as it was: the click at its level and nothing after', (middle[0], sum(abs(v) for v in middle[1:])),
                 (16384 * 23170 // 32768, 0))
    leftish = db(sum(v * v for v in ears[('leftish', 'left')])) - db(sum(v * v for v in ears[('leftish', 'right')]))
    checks.true('a sound half to the left: nearer, less far (%.1f dB, %d samples)'
                % (leftish, onset(ears[('leftish', 'right')])),
                0 < onset(ears[('leftish', 'right')]) < onset(ears[('hard_left', 'right')]))
    disc = ears[('disc_left', 'left')], ears[('disc_left', 'right')]
    checks.true("the disc's left a speaker at 30 degrees: the right ear hears it %d samples later"
                % onset(disc[1]), 8 <= onset(disc[1]) <= 14)
    low_sum = sum(disc[0]) + sum(disc[1])
    checks.true("and music in the middle as loud at each ear as it was (the two ears' sum at 0 Hz %d of 16384)"
                % low_sum, abs(low_sum - 16384) < 200)

    # ---- the chip ----
    checks.equal('the chip with the remastered mix beside it: its samples, memory and registers to the bit', chip, 1)

    # ---- a rollback ----
    checks.true('a rollback with the mix kept and put back beside the chip is heard as the run with none, to the '
                'bit; without, the reverb hears its blanks twice (%s samples differ)'
                % (rollback[1] if rollback else '?'),
                rollback is not None and rollback[0] == 1 and rollback[1] > 735)
