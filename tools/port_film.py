#!/usr/bin/env python3
"""A replay's film held to the replay (docs/PORT.md, "A replay as a film"):

    python3 tools/port_film.py [--out FOLDER] [--size 640x360]   Linux (WSL):
        build/port-sdl/linux/rebushido, offscreen on Mesa's llvmpipe; an .avi
    python tools/port_film.py [--size 1280x720]                  Windows:
        build/port-sdl/windows/rebushido.exe, on a window never shown and the
        machine's card; an .mp4 and an .avi

The window's program records a fight of two CPUs as a replay (--fight
... --fight-cpu --replays), makes a film of it (--replay FILE --export
OUT), and plays it once more with --tap-window, its window's own pixels
every TAP_EVERY blanks, and --wav, the run's sound. The film is read back
by the program itself (--film-read: its own AVI reader and stb_image, or
Media Foundation for an MP4), and then:

  - each frame is a blank: frame k's time is k blanks of the console's
    (4,500,000 / 75,218 a second), and the film is as long as its sound;
  - frame k is the picture --tap-window took at blank B + k, B the blank
    the film says it begins at: as near to it as the codec allows (a mean
    difference of a channel under PICTURE_MOST of 255), and nearer to it
    than the frames two blanks either side are (the game's frame before
    and after) wherever those are another picture;
  - its sound is --wav's from the sample the film says it begins at: an
    AVI's sample for sample; an MP4's, whose AAC is not the same samples,
    as nearly the same wave at the same time (the lag of the best match,
    and how alike they are there).

It needs the disc's image (the .cue at the checkout's root). Exit 0 if
every check passes, 1 if one fails; with no program or image it says so
and passes."""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE = os.path.join(REPO, 'Bushido Blade 2 (USA).cue')
PROGRAMS = {'nt': 'build/port-sdl/windows/rebushido.exe', 'posix': 'build/port-sdl/linux/rebushido'}
FIGHT = ['--fight', 'tatara-shrine', 'kannuki', 'mikado', '--fight-cpu']
RECORD_FRAMES = '4000'
TAP_EVERY = 50
TAP_MAGIC_BYTES = 16
RATE_NUMERATOR = 4500000
RATE_DENOMINATOR = 75218
SAMPLE_RATE = 44100
HUNDRED_NANOSECONDS = 10000000
# a channel's mean difference, of 255, a frame may have from the tap
PICTURE_MOST = {'avi': 3.0, 'mp4': 6.0}
# a frame's time as the film has it from its blank's, in 100 ns: an MP4's
# own time scale rounds it
TIME_MOST = 10000
# the frames read back to compare: so many taps, each with the frames two
# blanks either side
TAPS_COMPARED = 6
NEIGHBOUR = 2
# the AAC's lag found within so many samples, its wave this alike
LAG_SEARCH = 4096
LAG_MOST = 2112 + 64
LIKENESS_LEAST = 0.95
SECONDS = 900


class Report:
    def __init__(self):
        self.failed = 0

    def check(self, ok, what):
        print('  %s %s' % ('ok  ' if ok else 'FAIL', what), flush=True)
        if not ok:
            self.failed += 1
        return ok


def environment():
    env = dict(os.environ)
    env['SDL_AUDIODRIVER'] = 'dummy'
    if os.name != 'nt':
        # no display: SDL's offscreen driver on Mesa in software
        env['SDL_VIDEODRIVER'] = 'offscreen'
        env['LIBGL_ALWAYS_SOFTWARE'] = '1'
    return env


def run(cmd, log, env):
    started = time.time()
    with open(log, 'wb') as out:
        code = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env,
                              cwd=REPO, timeout=SECONDS).returncode
    with open(log, encoding='utf-8', errors='replace') as f:
        return code, f.read(), time.time() - started


def read_raw(path):
    with open(path, 'rb') as f:
        data = f.read()
    _, w, h, _ = struct.unpack('<4I', data[:TAP_MAGIC_BYTES])
    return w, h, data[TAP_MAGIC_BYTES:]


def difference(a, b):
    """The mean difference of a channel, of 255, between two pictures of
    0xFFRRGGBB words; None where their sizes differ."""
    if a[0] != b[0] or a[1] != b[1]:
        return None
    try:
        import numpy

        x = numpy.frombuffer(a[2], numpy.uint8).reshape(-1, 4)[:, :3].astype(numpy.int16)
        y = numpy.frombuffer(b[2], numpy.uint8).reshape(-1, 4)[:, :3].astype(numpy.int16)
        return float(numpy.abs(x - y).mean())
    except ImportError:
        total = 0
        for i in range(0, len(a[2]), 4):
            total += abs(a[2][i] - b[2][i]) + abs(a[2][i + 1] - b[2][i + 1]) + abs(a[2][i + 2] - b[2][i + 2])
        return total / (3 * a[0] * a[1])


def read_wav(path):
    with open(path, 'rb') as f:
        data = f.read()
    at = 12
    while at + 8 <= len(data):
        tag, size = data[at:at + 4], struct.unpack('<I', data[at + 4:at + 8])[0]
        if tag == b'data':
            return data[at + 8:at + 8 + size]
        at += 8 + size + (size & 1)
    return b''


def check_sound(report, kind, film, wav, first, frames):
    """The film's samples against --wav's from `first`."""
    want = frames * RATE_DENOMINATOR * SAMPLE_RATE // RATE_NUMERATOR
    have = len(film) // 4
    report.check(abs(have - want) < 4096,
                 '%s: its sound is as long as its pictures (%d samples, %d for %d frames)' % (kind, have, want, frames))
    if kind == 'avi':
        # the last blank's sound is made at the blank after it, which the
        # replay does not reach: silence there
        tail = 1024
        same = film[:(have - tail) * 4] == wav[first * 4:(first + have - tail) * 4]
        report.check(same, 'avi: its sound is --wav\'s, sample for sample, from sample %d (%d samples)'
                     % (first, have - tail))
        return
    try:
        import numpy
    except ImportError:
        report.check(True, 'mp4: its sound not compared (no numpy here)')
        return
    a = numpy.frombuffer(film, numpy.int16).reshape(-1, 2).astype(numpy.float64).mean(axis=1)
    b = numpy.frombuffer(wav, numpy.int16).reshape(-1, 2).astype(numpy.float64).mean(axis=1)
    b = b[first:first + len(a)]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    size = 1 << int(numpy.ceil(numpy.log2(2 * n)))
    cross = numpy.fft.irfft(numpy.fft.rfft(a, size) * numpy.conj(numpy.fft.rfft(b, size)), size)
    lags = list(range(0, LAG_SEARCH)) + list(range(-LAG_SEARCH, 0))
    best = max(lags, key=lambda lag: cross[lag])
    if best >= 0:
        x, y = a[best:], b[:n - best]
    else:
        x, y = a[:n + best], b[-best:]
    likeness = float(numpy.dot(x, y) / (numpy.sqrt(numpy.dot(x, x) * numpy.dot(y, y)) + 1e-9))
    report.check(abs(best) <= LAG_MOST and likeness >= LIKENESS_LEAST,
                 'mp4: its sound is --wav\'s from sample %d, %d samples later (%.1f ms: the AAC\'s own delay), '
                 'alike %.4f' % (first, best, best * 1000.0 / SAMPLE_RATE, likeness))


def check_film(report, program, env, out, kind, replay, size, taps, wav):
    film = os.path.join(out, 'film.' + kind)
    code, said, seconds = run([program, '--image', IMAGE, '--config', os.path.join(out, 'none.cfg'), '--no-cards',
                               '--no-log', '--replay', replay, '--export', film, '--window', size],
                              os.path.join(out, 'export-%s.out' % kind), env)
    begun = re.search(r'film: .* begins at blank (\d+) and sample (\d+)', said)
    written = re.search(r'film: written .*, (\d+) frames .*, (\d+) bytes, in ([\d.]+) seconds', said)
    if not report.check(code == 0 and begun and written, '%s: made (exit %d)%s' % (
            kind, code, ': %s frames, %s bytes, %s seconds' % written.groups() if written else '')):
        print(said[-2000:])
        return
    first_blank, first_sample = int(begun.group(1)), int(begun.group(2))
    frames = int(written.group(1))
    # the taps within the film, spread over it, and their neighbours
    within = sorted(t for t in taps if first_blank + NEIGHBOUR <= t < first_blank + frames - NEIGHBOUR)
    step = max(1, len(within) // TAPS_COMPARED)
    chosen = within[step // 2::step][:TAPS_COMPARED]
    wanted = sorted({t - first_blank + d for t in chosen for d in (-NEIGHBOUR, 0, NEIGHBOUR)})
    read = os.path.join(out, 'read-' + kind)
    os.makedirs(read)
    code, said, _ = run([program, '--film-read', film, read, '--film-at', ','.join(str(k) for k in wanted)],
                        os.path.join(out, 'read-%s.out' % kind), env)
    if not report.check(code == 0, '%s: read back (exit %d)' % (kind, code)):
        print(said[-2000:])
        return
    with open(os.path.join(read, 'frames.txt')) as f:
        times = [tuple(int(x) for x in line.split()) for line in f if line.strip()]
    late = [(k, t) for k, t in times
            if abs(t - k * RATE_DENOMINATOR * HUNDRED_NANOSECONDS // RATE_NUMERATOR) > TIME_MOST]
    report.check(len(times) == frames and not late,
                 '%s: %d frames read back, each within a millisecond of its blank\'s time (%d not)' % (kind, len(times), len(late)))
    for tap in chosen:
        k = tap - first_blank
        shot = read_raw(os.path.join(out, 'tap', 'window-%06u.raw' % tap))
        own = difference(read_raw(os.path.join(read, 'frame-%06u.raw' % k)), shot)
        others = [difference(read_raw(os.path.join(read, 'frame-%06u.raw' % (k + d))), shot)
                  for d in (-NEIGHBOUR, NEIGHBOUR)]
        # a neighbour that is the same picture (the fight standing still)
        # says nothing of where the frame is
        unlike = [o for o in others if o is not None and o > own + 0.5]
        alike = [o for o in others if o is not None and o <= own + 0.5]
        report.check(own is not None and own <= PICTURE_MOST[kind] and not [o for o in alike if o < own - 0.01],
                     '%s: frame %d is blank %d\'s picture (differs %.2f of 255; the frames two blanks either side '
                     '%s)' % (kind, k, tap, own if own is not None else -1,
                              ', '.join('%.2f' % o for o in others if o is not None)))
        if not unlike:
            print('       (the fight stood still there: its neighbours are the same picture)')
    check_sound(report, kind, read_wav(os.path.join(read, 'sound.wav')), wav, first_sample, frames)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'film'))
    ap.add_argument('--size', default='1280x720' if os.name == 'nt' else '640x360')
    ap.add_argument('--kinds', default='mp4,avi' if os.name == 'nt' else 'avi')
    args = ap.parse_args(argv)
    program = os.path.join(REPO, PROGRAMS[os.name])
    if not os.path.isfile(program) or not os.path.isfile(IMAGE):
        print('port_film: no %s or no disc image: nothing checked' % program)
        return 0
    out = os.path.abspath(args.out)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, 'replays'))
    os.makedirs(os.path.join(out, 'tap'))
    # the settings built in, whatever a settings file beside the program says
    open(os.path.join(out, 'none.cfg'), 'w').close()
    env = environment()
    record_env = dict(env)
    report = Report()
    print('port_film: %s, %s' % (os.path.relpath(program, REPO), args.size), flush=True)
    code, said, _ = run([program, '--image', IMAGE, '--config', os.path.join(out, 'none.cfg'), '--no-cards',
                         '--no-log', '--fast', '--hidden', '--graphics', 'original', '--window', '320x240',
                         '--frames', RECORD_FRAMES, '--replays',
                         os.path.join(out, 'replays')] + FIGHT, os.path.join(out, 'record.out'), record_env)
    replays = sorted(os.listdir(os.path.join(out, 'replays')))
    if not report.check(code == 0 and len(replays) == 1, 'a fight of two CPUs recorded (exit %d, %d replays)'
                        % (code, len(replays))):
        print(said[-2000:])
        return 1
    replay = os.path.join(out, 'replays', replays[0])
    wav = os.path.join(out, 'tap.wav')
    code, said, _ = run([program, '--image', IMAGE, '--config', os.path.join(out, 'none.cfg'), '--no-cards',
                         '--no-log', '--fast', '--hidden', '--no-fps', '--no-vsync', '--run-ahead', '0',
                         '--frame-rate', 'original', '--window', args.size, '--replay', replay, '--tap-window',
                         os.path.join(out, 'tap'), '--tap-every', str(TAP_EVERY), '--wav', wav],
                        os.path.join(out, 'tap.out'), env)
    taps = [int(name[7:13]) for name in os.listdir(os.path.join(out, 'tap')) if name.startswith('window-')]
    if not report.check(code == 0 and taps, 'played back with --tap-window and --wav (exit %d, %d pictures)'
                        % (code, len(taps))):
        print(said[-2000:])
        return 1
    samples = read_wav(wav)
    for kind in args.kinds.split(','):
        check_film(report, program, env, out, kind, replay, args.size, taps, samples)
    print('port_film: %s' % ('every check passes' if report.failed == 0 else '%d checks fail' % report.failed))
    return 1 if report.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
