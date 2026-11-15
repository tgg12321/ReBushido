#!/usr/bin/env python3
"""Proves the port's remastered renderer, with nothing shown
(docs/PORT.md, "Remastered").

    python3 tools/port_gl_prove.py --image "Bushido Blade 2 (USA).cue"
            [--plans port_game,port_other_fight,port_bugfixes]
            [--scale 4] [--every 400] [--out build/remaster] [--quick]

For each play test's plan it runs build/port-gl/rebushido-gl twice:

  - at scale 1 with the three things that change values turned off
    (--gl-exact: the corners where the PlayStation held them, the
    texture flat across a polygon, five bits a channel with the chip's
    dither), comparing every picture with the software GPU's own,
    pixel for pixel;
  - at the scale asked for, writing the hardware's picture and the
    software GPU's side by side as PNGs.

It also runs the game once with the remastered renderer on and once
with it off and holds the two traces to each other: the game must
compute the same values either way.

It prints a table of what it measured; no emulator is started and no
window is opened. The plans are written from tests/play/*.toml for this
program's own addresses (tools/playtest.py writes them).
"""
import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

PROGRAM = 'build/port-gl/rebushido-gl'
PLANS = ['port_game', 'port_other_fight', 'port_bugfixes']


def write_plan(name, out):
    """The port's side of tests/play/NAME.toml, for this program."""
    import playtest

    playtest.PORT_PROGRAM = os.path.abspath(PROGRAM)
    playtest.PORT_SYMBOLS = playtest.PORT_PROGRAM + '.sym'
    test = playtest.Test(os.path.join('tests', 'play', name + '.toml'))
    order, lines = test.plan(playtest.Disc('port'), playtest.header_names(), 1)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return out


def run(args, log, quiet=False):
    """The run's output into `log`. The program's own output is the
    game's trace; what the prover itself prints goes to the error
    output, so that a trace can be held to another run's."""
    started = time.time()
    with open(log, 'w', encoding='utf-8', errors='replace', newline='\n') as f:
        subprocess.run(args, stdout=f, stderr=subprocess.DEVNULL if quiet else f, cwd=ROOT)
    return time.time() - started


def numbers_of(log):
    """What a run printed: the comparison and the counts."""
    out = {'pictures': 0, 'pixels': 0, 'different': 0, 'close': 0, 'worst': 0,
           'per_frame_us': 0, 'blanks': 0, 'kinds': [], 'missing': []}
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.search(r'gl run compared (\d+) pictures (\d+) pixels different (\d+) close (\d+) worst (\d+)', line)
            if m:
                out.update(pictures=int(m.group(1)), pixels=int(m.group(2)), different=int(m.group(3)),
                           close=int(m.group(4)), worst=int(m.group(5)))
            m = re.search(r'gl run per_frame_us (\d+)', line)
            if m:
                out['per_frame_us'] = int(m.group(1))
            m = re.search(r'gl run blanks (\d+)', line)
            if m:
                out['blanks'] = int(m.group(1))
            m = re.search(r'gl run kind (\S+) seen (\d+) drawn (\d+)', line)
            if m:
                kind, seen, drawn = m.group(1), int(m.group(2)), int(m.group(3))
                out['kinds'].append((kind, seen, drawn))
                if drawn != seen and kind not in ('fill', 'copy', 'load', 'store', 'setting'):
                    out['missing'].append('%s %d of %d' % (kind, drawn, seen))
            m = re.search(r'gl run with_depth (\d+) without_depth (\d+) points (\d+) faces (\d+) '
                         r'attached (\d+) misses (\d+) full (\d+) masked (\d+) texture_from_drawn (\d+)', line)
            if m:
                for i, name in enumerate(['with_depth', 'without_depth', 'points', 'faces', 'attached',
                                          'misses', 'full', 'masked', 'texture_from_drawn']):
                    out[name] = int(m.group(i + 1))
    return out


def trace_of(log):
    """A run's trace lines alone."""
    with open(log, encoding='utf-8', errors='replace') as f:
        return [line for line in f if line.startswith(('trace ', 'cp ', 'miss ', 'stub '))]


def main():
    os.chdir(ROOT)
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', default='Bushido Blade 2 (USA).cue')
    ap.add_argument('--plans', default=','.join(PLANS))
    ap.add_argument('--scale', type=int, default=4)
    ap.add_argument('--every', type=int, default=400)
    ap.add_argument('--out', default='build/remaster')
    ap.add_argument('--quick', action='store_true', help='the first plan only, and no pictures')
    args = ap.parse_args()
    if not os.path.isfile(PROGRAM):
        print('port-gl: no %s (python3 tools/port_gl_build.py)' % PROGRAM)
        return 1
    if not os.path.isfile(args.image):
        print('port-gl: no %s: --image names your own image of the disc' % args.image)
        return 1
    plans = args.plans.split(',')[:1] if args.quick else args.plans.split(',')
    rows = []
    for name in plans:
        folder = os.path.join(args.out, name)
        os.makedirs(folder, exist_ok=True)
        plan = write_plan(name, os.path.join(folder, 'plan.txt'))
        # --out: the plan asks for pieces of memory at its checkpoints,
        # and they go into the run's own folder, not the tree's root
        common = [os.path.join('.', PROGRAM), '--image', args.image, '--plan', plan,
                  '--out', folder]
        print('%s: scale 1, as the console, comparing every picture' % name)
        seconds = run(common + ['--graphics', 'remastered', '--gl-scale', '1', '--gl-exact',
                                '--gl-compare', '--gl-every', '20'],
                      os.path.join(folder, 'exact.log'))
        exact = numbers_of(os.path.join(folder, 'exact.log'))
        exact['seconds'] = seconds
        if not args.quick:
            print('%s: scale %d, the pictures' % (name, args.scale))
            run(common + ['--graphics', 'remastered', '--gl-scale', str(args.scale), '--gl-shots', folder,
                          '--gl-every', str(args.every), '--gl-original'],
                os.path.join(folder, 'pictures.log'))
        print('%s: the game with the remastered renderer on and off' % name)
        run(common + ['--graphics', 'remastered', '--trace'], os.path.join(folder, 'trace_on.log'), quiet=True)
        run(common + ['--graphics', 'original', '--trace'], os.path.join(folder, 'trace_off.log'), quiet=True)
        on = trace_of(os.path.join(folder, 'trace_on.log'))
        off = trace_of(os.path.join(folder, 'trace_off.log'))
        same = on == off and len(on) > 0
        rows.append((name, exact, same, len(on)))
    print()
    print('%-18s %8s %9s %10s %8s %7s %8s %6s %s'
          % ('plan', 'blanks', 'pictures', 'pixels', 'differ', 'close', 'worst', 'us/f', 'trace on == off'))
    for name, exact, same, lines in rows:
        print('%-18s %8d %9d %10d %8d %7d %8d %6d %s'
              % (name, exact['blanks'], exact['pictures'], exact['pixels'], exact['different'],
                 exact['close'], exact['worst'], exact['per_frame_us'],
                 'yes (%d lines)' % lines if same else 'NO'))
        if exact['missing']:
            print('    primitives the hardware did not draw: %s' % ', '.join(exact['missing']))
        print('    kinds: %s' % ', '.join('%s %d' % (kind, seen) for kind, seen, _ in exact['kinds']))
        print('    with depth %d, without %d, points %d, faces %d, attached %d, misses %d, table full %d, '
              'masked %d, textures from drawn pixels %d'
              % (exact.get('with_depth', 0), exact.get('without_depth', 0), exact.get('points', 0),
                 exact.get('faces', 0), exact.get('attached', 0), exact.get('misses', 0), exact.get('full', 0),
                 exact.get('masked', 0), exact.get('texture_from_drawn', 0)))
    return 0 if all(row[2] for row in rows) else 1


if __name__ == '__main__':
    sys.exit(main())
