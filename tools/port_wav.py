#!/usr/bin/env python3
"""Listen to a run's WAV by numbers, and draw it.

    python tools/port_wav.py FOLDER

FOLDER holds a run of the native port that kept its sound (docs/PORT.md,
"Sound"): sound.wav, and run.log, the program's output:

    build/port/rebushido --image IMAGE.cue --plan PLAN --wav FOLDER/sound.wav > FOLDER/run.log

Reads sound.wav and run.log of the folder. Prints, for each section of
the run (between the plan's checkpoints): its length, each side's peak
and RMS, how many samples are at full scale, the longest silence, the
voices started and the sounds the game asked for. Writes envelope.png
(the peak and RMS of every 20 ms, left above right, with the
checkpoints and the key-ons marked) and spectrogram.png (0 to 11 kHz of
the two sides mixed)."""
import cmath
import math
import os
import struct
import sys
import wave
import zlib

RATE = 44100
PER_BLANK = 737 + 341 / 2500.0
SECTIONS = ['page', 'movie', 'menu', 'select', 'fight', 'kill', 'replay', 'after_fight', 'menu_again']


def write_png(path, width, height, rgb):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + bytes(rgb[y * width * 3:(y + 1) * width * 3]) for y in range(height))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def fft(values):
    n = len(values)
    if n == 1:
        return values
    even, odd = fft(values[0::2]), fft(values[1::2])
    out = [0] * n
    for k in range(n // 2):
        t = cmath.exp(-2j * cmath.pi * k / n) * odd[k]
        out[k] = even[k] + t
        out[k + n // 2] = even[k] - t
    return out


def main():
    folder = sys.argv[1]
    w = wave.open(os.path.join(folder, 'sound.wav'), 'rb')
    assert w.getnchannels() == 2 and w.getsampwidth() == 2 and w.getframerate() == RATE
    frames = w.getnframes()
    data = struct.unpack('<%dh' % (frames * 2), w.readframes(frames))
    left, right = data[0::2], data[1::2]
    print('%s/sound.wav: %d samples, %.2f s, 44100 Hz stereo 16 bit' % (folder, frames, frames / RATE))

    cps, keys, asks = [], [], []
    for line in open(os.path.join(folder, 'run.log'), encoding='utf-8', errors='replace'):
        word = line.split()
        if not word:
            continue
        if word[0] == 'cp':
            cps.append((word[1], int(word[2])))
        elif word[:2] == ['sound', 'key'] and word[6] != '0x01000':
            keys.append((int(word[2]), int(word[4])))
        elif word[:2] == ['sound', 'ask']:
            asks.append(int(word[2]))
    marks = [(name, blank) for name, blank in cps if name in SECTIONS]
    bounds = [('start', 0)] + marks + [('end', int(frames / PER_BLANK) + 1)]

    def stats(side, a, b):
        part = side[a:b]
        if not part:
            return 0, 0.0, 0
        peak = max(max(part), -min(part))
        rms = math.sqrt(sum(v * v for v in part) / len(part))
        full = sum(1 for v in part if v >= 32765 or v <= -32765)
        return peak, rms, full

    def longest_silence(a, b):
        best = run = 0
        for i in range(a, b):
            if -8 <= left[i] <= 8 and -8 <= right[i] <= 8:
                run += 1
                if run > best:
                    best = run
            else:
                run = 0
        return best

    print('%-12s %7s %7s  %6s %7s %5s  %6s %7s %5s  %8s %5s %5s' % (
        'section', 'blank', 'secs', 'peak L', 'rms L', 'full', 'peak R', 'rms R', 'full', 'silence', 'keys', 'asks'))
    for (name, blank), (_, until) in zip(bounds, bounds[1:]):
        a, b = min(frames, int(blank * PER_BLANK)), min(frames, int(until * PER_BLANK))
        pl, rl, fl = stats(left, a, b)
        pr, rr, fr = stats(right, a, b)
        print('%-12s %7d %7.2f  %6d %7.1f %5d  %6d %7.1f %5d  %7.2fs %5d %5d' % (
            name, blank, (b - a) / RATE, pl, rl, fl, pr, rr, fr, longest_silence(a, b) / RATE,
            sum(1 for k, v in keys if blank <= k < until), sum(1 for k in asks if blank <= k < until)))
    pl, rl, fl = stats(left, 0, frames)
    pr, rr, fr = stats(right, 0, frames)
    print('%-12s %7s %7.2f  %6d %7.1f %5d  %6d %7.1f %5d' % ('whole', '', frames / RATE, pl, rl, fl, pr, rr, fr))

    # bursts of clipping: runs of three or more samples at full scale
    bursts = 0
    for side in (left, right):
        run = 0
        for v in side:
            if v >= 32765 or v <= -32765:
                run += 1
                if run == 3:
                    bursts += 1
            else:
                run = 0
    print('runs of 3 or more samples at full scale: %d' % bursts)
    # the largest jump between two samples: a click would be one
    jump = max(max(abs(side[i + 1] - side[i]) for i in range(frames - 1)) for side in (left, right))
    print('largest step between two samples: %d' % jump)
    tail = int(0.25 * RATE)
    print('last quarter second: peak %d / %d' % (stats(left, frames - tail, frames)[0],
                                                  stats(right, frames - tail, frames)[0]))

    # ---- the envelope ----
    step = 882  # 20 ms
    columns = frames // step
    width, lane = columns, 120
    height = lane * 2 + 12
    rgb = bytearray([16, 16, 24] * (width * height))

    def put(x, y, color):
        if 0 <= x < width and 0 <= y < height:
            rgb[(y * width + x) * 3:(y * width + x) * 3 + 3] = bytes(color)

    for x in range(columns):
        for index, side in enumerate((left, right)):
            part = side[x * step:(x + 1) * step]
            peak = max(max(part), -min(part))
            rms = math.sqrt(sum(v * v for v in part) / len(part))
            base = index * (lane + 12) + lane - 1
            for y in range(int(peak * (lane - 1) / 32768)):
                put(x, base - y, (60, 110, 190))
            for y in range(int(rms * (lane - 1) / 32768)):
                put(x, base - y, (150, 210, 255))
    for name, blank in cps:
        x = int(blank * PER_BLANK / step)
        for y in range(height):
            if y % 3 == 0:
                put(x, y, (230, 190, 60) if name in SECTIONS else (90, 80, 40))
    for blank, voice in keys:
        x = int(blank * PER_BLANK / step)
        for y in range(lane, lane + 12):
            put(x, y, (240, 80, 80))
    write_png(os.path.join(folder, 'envelope.png'), width, height, rgb)

    # ---- the spectrogram ----
    size, hop = 1024, 2205  # 50 ms a column
    bins = size // 4  # to 11 kHz
    window = [0.5 - 0.5 * math.cos(2 * math.pi * i / size) for i in range(size)]
    columns = (frames - size) // hop
    rgb = bytearray(columns * bins * 3)
    for x in range(columns):
        at = x * hop
        spectrum = fft([(left[at + i] + right[at + i]) * 0.5 * window[i] for i in range(size)])
        for k in range(bins):
            power = abs(spectrum[k]) / (size / 4) + 1e-9
            level = (20 * math.log10(power / 32768.0) + 90) / 90  # -90 dB to 0
            level = max(0.0, min(1.0, level))
            y = bins - 1 - k
            rgb[(y * columns + x) * 3:(y * columns + x) * 3 + 3] = bytes(
                (int(255 * level ** 0.8), int(255 * level ** 2), int(120 * (1 - level) * level * 4)))
    write_png(os.path.join(folder, 'spectrogram.png'), columns, bins, rgb)
    print('pictures: %s/envelope.png (%dx%d, 20 ms a column), %s/spectrogram.png (%dx%d, 50 ms a column, '
          '0 to 11 kHz upwards)' % (folder, width, height, folder, columns, bins))


if __name__ == '__main__':
    main()
