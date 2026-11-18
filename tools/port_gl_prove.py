#!/usr/bin/env python3
"""Proves the port's remastered renderer, with nothing shown
(docs/PORT.md, "Remastered").

    python3 tools/port_gl_prove.py --image "Bushido Blade 2 (USA).cue"
            [--plans port_game,port_other_fight,port_bugfixes]
            [--scale 4] [--every 400] [--out build/remaster] [--quick]
            [--gpu llvmpipe|card]

For each play test's plan it runs build/port-gl/rebushido-gl twice:

  - at scale 1 with the three things that change values turned off
    (--gl-exact: the corners where the PlayStation held them, the
    texture flat across a polygon, five bits a channel with the chip's
    dither), comparing every picture with the software GPU's own,
    pixel for pixel;
  - at the scale asked for, writing the hardware's picture and the
    software GPU's side by side as PNGs;
  - at two scales unlike each other with the wider shape, holding every
    picture that has no scene in it to the console's own picture scaled
    up by whole blocks (--gl-blocks): what the game draws flat must be
    the console's picture at any scale, which is what the first run on
    a real card found it was not (docs/PORT.md, "Remastered").

It also runs the game once with the remastered renderer on and once
with it off and holds the two traces to each other: the game must
compute the same values either way.

It prints a table of what it measured; no emulator is started and no
window is opened. `--gpu llvmpipe` (in Linux) draws with Mesa in
software, the exact reference; `--gpu card` (the default on Windows,
`python tools/port_gl_prove.py`) runs the Windows prover on the
machine's own card. What it holds is the same on both: the traces, the
flat screens' blocks and the fighters' places. The plans are written from tests/play/*.toml for this
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

# Where the provers draw (`--gpu`; docs/PORT.md, "The provers on a graphics
# card"): `llvmpipe` is the Linux prover through EGL on Mesa's software
# driver, the reference where a number has to be exact and the same on
# every machine; `card` is the Windows prover through SDL on the machine's
# own graphics card, on a window never shown (tools/port_gl_build.py
# --windows). On Windows only the card can run; in Linux (WSL) either.
GPU_PROGRAMS = {'llvmpipe': 'build/port-gl/rebushido-gl',
                'card': 'build/port-gl/windows/rebushido-gl.exe'}
GPU_BUILDS = {'llvmpipe': 'python3 tools/port_gl_build.py',
              'card': 'wsl bash tools/build.sh port-gl-windows'}
# What the child runs with: None is this process's own environment.
ENVIRONMENT = None


def add_gpu_option(ap):
    ap.add_argument('--gpu', choices=sorted(GPU_PROGRAMS),
                    default='card' if os.name == 'nt' else 'llvmpipe',
                    help='llvmpipe: Mesa in software, the exact reference (Linux); '
                         'card: the graphics card, through the Windows prover '
                         '(the default on Windows)')


def use_gpu(gpu):
    """Makes `gpu` the one this module's runs draw on: its program, and
    the environment its child is given. Returns the program's path."""
    global PROGRAM, ENVIRONMENT
    PROGRAM = GPU_PROGRAMS[gpu]
    ENVIRONMENT = dict(os.environ)
    if gpu == 'llvmpipe':
        # Mesa's software driver, whatever else the host has
        ENVIRONMENT['LIBGL_ALWAYS_SOFTWARE'] = '1'
    else:
        # nothing is ever shown or heard (the program opens no sound)
        ENVIRONMENT['SDL_AUDIODRIVER'] = 'dummy'
    return PROGRAM


def missing_program(gpu):
    """Why the prover of `gpu` cannot run here, or None."""
    if not os.path.isfile(GPU_PROGRAMS[gpu]):
        return 'no %s (%s)' % (GPU_PROGRAMS[gpu], GPU_BUILDS[gpu])
    if gpu == 'llvmpipe' and os.name == 'nt':
        return 'the llvmpipe prover is a Linux program: run this in WSL (python3)'
    return None


def command(program):
    """How a program is named to subprocess: from the tree's root, which
    every prover runs in."""
    return os.path.abspath(program) if program.endswith('.exe') else os.path.join('.', program)


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
    output, so that a trace can be held to another run's. A quiet run's error
    output goes beside it (`log` + `.err`) rather than nowhere, and a run
    that ends badly says so with the last of it: a run that stopped before
    its first frame otherwise leaves only an empty log."""
    started = time.time()
    with open(log, 'w', encoding='utf-8', errors='replace', newline='\n') as f, \
            open(log + '.err' if quiet else os.devnull, 'w', encoding='utf-8',
                 errors='replace', newline='\n') as err:
        done = subprocess.run(args, stdout=f, stderr=err if quiet else f, cwd=ROOT, env=ENVIRONMENT)
    if done.returncode != 0:
        with open(log + '.err' if quiet else log, encoding='utf-8', errors='replace') as f:
            last = f.read().splitlines()[-3:]
        print('%s: the run ended with %d: %s' % (log, done.returncode, ' / '.join(last)))
    return time.time() - started


def numbers_of(log):
    """What a run printed: the comparison and the counts."""
    out = {'pictures': 0, 'pixels': 0, 'different': 0, 'close': 0, 'worst': 0,
           'per_frame_us': 0, 'blanks': 0, 'kinds': [], 'missing': [],
           'block_pictures': 0, 'block_pixels': 0, 'block_uneven': 0,
           'block_different': 0, 'block_far': 0, 'block_worst': 0}
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.search(r'gl run compared (\d+) pictures (\d+) pixels different (\d+) close (\d+) worst (\d+)', line)
            if m:
                out.update(pictures=int(m.group(1)), pixels=int(m.group(2)), different=int(m.group(3)),
                           close=int(m.group(4)), worst=int(m.group(5)))
            m = re.search(r'gl run blocks pictures (\d+) pixels (\d+) uneven (\d+) '
                          r'different (\d+) far (\d+) worst (\d+)', line)
            if m:
                out.update(block_pictures=int(m.group(1)), block_pixels=int(m.group(2)),
                           block_uneven=int(m.group(3)), block_different=int(m.group(4)),
                           block_far=int(m.group(5)), block_worst=int(m.group(6)))
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
            m = re.search(r'gl run with_depth (\d+) without_depth (\d+) with_place (\d+) points (\d+) '
                          r'faces (\d+) attached (\d+) misses (\d+) full (\d+) masked (\d+) '
                          r'texture_from_drawn (\d+)', line)
            if m:
                for i, name in enumerate(['with_depth', 'without_depth', 'with_place', 'points', 'faces',
                                          'attached', 'misses', 'full', 'masked', 'texture_from_drawn']):
                    out[name] = int(m.group(i + 1))
            m = re.search(r'gl run fighter_faces (\d+) fighter_unplaced (\d+) walked_unplaced (\d+)', line)
            if m:
                out.update(fighter_faces=int(m.group(1)), fighter_unplaced=int(m.group(2)),
                           walked_unplaced=int(m.group(3)))
    return out


# Two of a five-bit channel's steps, in a byte's 255: what a pixel that
# mixes may be out by, since the card mixes at eight bits a channel and
# the chip at five.
BLOCK_WORST_MAX = 16


def blocks_hold(got):
    """Whether a run's flat screens are the console's own picture: no
    block of one of its pixels is broken up, and none is further from
    the console's colour than mixing can account for."""
    return got['block_uneven'] == 0 and got['block_worst'] <= BLOCK_WORST_MAX


def fighters_hold(got):
    """Whether every polygon of a fighter's own had the place the
    projection worked out for it. One that has none is drawn at whole
    pixels and stands still between two frames while the faces about it
    move, which is a joint torn open (port_RemasterCopyPoint)."""
    return got.get('fighter_faces', 0) > 0 and got.get('fighter_unplaced', 0) == 0


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
    add_gpu_option(ap)
    args = ap.parse_args()
    use_gpu(args.gpu)
    problem = missing_program(args.gpu)
    if problem:
        print('port-gl: %s' % problem)
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
        common = [command(PROGRAM), '--image', args.image, '--plan', plan,
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
        print('%s: scale 4 across and 3 down, the flat screens block for block'
              % name)
        run(common + ['--graphics', 'remastered', '--gl-scale', '4', '--gl-tall', '3',
                      '--aspect', '16:9', '--gl-blocks', '--gl-every', '20'],
            os.path.join(folder, 'blocks.log'))
        blocks = numbers_of(os.path.join(folder, 'blocks.log'))
        for key in ('block_pictures', 'block_pixels', 'block_uneven',
                    'block_different', 'block_far', 'block_worst'):
            exact[key] = blocks[key]
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
        # what the game draws flat, block for block against the console's
        # own picture at a scale of 4 across and 3 down
        print('    blocks: %d pictures, %d pixels, %d not of one colour, '
              '%d not the console\'s colour, %d by more than a step '
              '(worst %d): %s'
              % (exact['block_pictures'], exact['block_pixels'], exact['block_uneven'],
                 exact['block_different'], exact['block_far'], exact['block_worst'],
                 'ok' if blocks_hold(exact)
                 else 'NO (a flat screen is not the console\'s picture)'))
        print('    with depth %d, without %d, points %d, faces %d, attached %d, misses %d, table full %d, '
              'masked %d, textures from drawn pixels %d'
              % (exact.get('with_depth', 0), exact.get('without_depth', 0), exact.get('points', 0),
                 exact.get('faces', 0), exact.get('attached', 0), exact.get('misses', 0), exact.get('full', 0),
                 exact.get('masked', 0), exact.get('texture_from_drawn', 0)))
        print('    fighters: %d polygons, %d with no place: %s; other polygons of the walk with no place %d'
              % (exact.get('fighter_faces', 0), exact.get('fighter_unplaced', 0),
                 'ok' if fighters_hold(exact) else 'NO (a fighter is drawn torn at his joints)',
                 exact.get('walked_unplaced', 0)))
    traces_agree = all(row[2] for row in rows)
    blocks_agree = all(blocks_hold(row[1]) for row in rows)
    fighters_agree = all(fighters_hold(row[1]) for row in rows)
    return 0 if traces_agree and blocks_agree and fighters_agree else 1


if __name__ == '__main__':
    sys.exit(main())
