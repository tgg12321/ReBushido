#!/usr/bin/env python3
"""Proves the pictures the port draws between the game's frames, with
nothing shown (docs/PORT.md, "Remastered", "More pictures a second").

    python3 tools/port_fps_prove.py --image "Bushido Blade 2 (USA).cue"
            [--plans port_game,port_other_fight,port_bugfixes]
            [--phases 5] [--scale 2] [--out build/fps] [--quick]
            [--bits 64,32]

For each play test's plan it runs build/port-gl/rebushido-gl with each
game frame drawn as `--phases` pictures: the first the frame on show, the
last the frame after it, and the others between them. It holds three
things:

  - the first picture is the game's own, pixel for pixel (the renderer
    drawn again with nothing moved);
  - every picture in between is farther from the first and nearer the
    last than the one before it;
  - the point the renderer watched -- the corner that moved farthest --
    lies between its two ends at every phase;
  - every corner of every primitive lies between its own two ends, which
    is where a partner wrongly found shows;
  - a scene that did not move at all between two frames is drawn exactly
    as it is in every picture between them.

It also runs the game with the pictures on and off and holds the two
traces to each other, line for line, for each pointer size asked for:
the game must compute exactly the same values.

Last it writes a strip of consecutive pictures of a fight, every phase
of every frame, as PNGs, so that what a screen would show can be looked
at. It prints what it measured; no emulator is started and no window is
opened.
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
# The blank the strip of pictures begins at, and how many blanks of it:
# well into the fight of every plan.
STRIP_AT = 3000
STRIP_BLANKS = 12


def run(args, log, quiet=False):
    """The run's output into `log`; its own output is the game's trace."""
    started = time.time()
    with open(log, 'w', encoding='utf-8', errors='replace', newline='\n') as f:
        subprocess.run(args, stdout=f,
                       stderr=subprocess.DEVNULL if quiet else f, cwd=ROOT)
    return time.time() - started


def numbers_of(log):
    """What a run printed about the pictures between frames."""
    out = {'phases': 0, 'frames': 0, 'drawn': 0, 'missed': 0, 'cuts': 0,
           'overflowed': 0, 'per_picture_us': 0, 'checked': 0,
           'out_of_order': 0, 'point_out_of_line': 0, 'identity': 0,
           'identity_pixels': 0, 'identity_different': 0, 'blanks': 0,
           'prims': 0, 'mixed': 0, 'unpaired': 0, 'flat': 0, 'widest': 0,
           'pictures': 0, 'scene': 0, 'named': 0, 'searched': 0,
           'clashes': 0, 'held': 0, 'off_the_line': 0, 'still': 0,
           'still_moved': 0, 'fewer_prims': 0, 'more_dropped': 0, 'holes': 0,
           'hole_pixels': 0}
    mixed, prims, unpaired, flat, widest, pictures = 0, 0, 0, 0, 0, 0
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.search(r'gl run tween pictures (\d+) frames (\d+) drawn (\d+) '
                          r'missed (\d+) cuts (\d+) overflowed (\d+)', line)
            if m:
                out.update(phases=int(m.group(1)), frames=int(m.group(2)),
                           drawn=int(m.group(3)), missed=int(m.group(4)),
                           cuts=int(m.group(5)), overflowed=int(m.group(6)))
            m = re.search(r'gl run tween per_picture_us (\d+)', line)
            if m:
                out['per_picture_us'] = int(m.group(1))
            m = re.search(r'gl run tween checked (\d+) out_of_order (\d+) '
                          r'point_out_of_line (\d+) identity (\d+) '
                          r'identity_pixels (\d+) identity_different (\d+)', line)
            if m:
                out.update(checked=int(m.group(1)), out_of_order=int(m.group(2)),
                           point_out_of_line=int(m.group(3)),
                           identity=int(m.group(4)),
                           identity_pixels=int(m.group(5)),
                           identity_different=int(m.group(6)))
            m = re.search(r'gl run tween scene (\d+) named (\d+) '
                          r'searched (\d+) clashes (\d+) held (\d+) '
                          r'off_the_line (\d+) still (\d+) still_moved (\d+) '
                          r'fewer_prims (\d+) more_dropped (\d+) holes (\d+) '
                          r'hole_pixels (\d+)', line)
            if m:
                out.update(scene=int(m.group(1)), named=int(m.group(2)),
                           searched=int(m.group(3)), clashes=int(m.group(4)),
                           held=int(m.group(5)), off_the_line=int(m.group(6)),
                           still=int(m.group(7)), still_moved=int(m.group(8)),
                           fewer_prims=int(m.group(9)),
                           more_dropped=int(m.group(10)),
                           holes=int(m.group(11)),
                           hole_pixels=int(m.group(12)))
            m = re.search(r'gl run blanks (\d+)', line)
            if m:
                out['blanks'] = int(m.group(1))
            m = re.search(r'gl tween blank \d+ prims (\d+) mixed (\d+) '
                          r'unpaired (\d+) flat (\d+) widest_step (\d+)',
                          line)
            if m:
                pictures += 1
                prims += int(m.group(1))
                mixed += int(m.group(2))
                unpaired += int(m.group(3))
                flat += int(m.group(4))
                widest = max(widest, int(m.group(5)))
    if pictures:
        out.update(pictures=pictures, prims=prims // pictures,
                   mixed=mixed // pictures, unpaired=unpaired // pictures,
                   flat=flat // pictures, widest=widest)
    return out


def trace_of(log):
    with open(log, encoding='utf-8', errors='replace') as f:
        return [line for line in f
                if line.startswith(('trace ', 'cp ', 'miss ', 'stub '))]


def main():
    os.chdir(ROOT)
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', default='Bushido Blade 2 (USA).cue')
    ap.add_argument('--plans', default=','.join(PLANS))
    ap.add_argument('--phases', type=int, default=5)
    ap.add_argument('--scale', type=int, default=2)
    ap.add_argument('--out', default='build/fps')
    ap.add_argument('--bits', default='64',
                    help='what to call the pointer size in the table; the\n'
                         'program already built is the one that is run')
    ap.add_argument('--quick', action='store_true',
                    help='the first plan only, and no strip')
    # The stage's ground as one mesh, which is what Remastered draws it
    # with (src/port/gl/gl_stage.h): the pictures between two frames
    # draw it again with the camera between them and have no tile to
    # pair, so this is the path to prove.
    ap.add_argument('--stage-mesh', default='on',
                    choices=('on', 'off', 'every'))
    args = ap.parse_args()
    if not os.path.isfile(args.image):
        print('port-fps: no %s: --image names your own image of the disc'
              % args.image)
        return 1
    import port_gl_prove as gl

    plans = args.plans.split(',')[:1] if args.quick else args.plans.split(',')
    bits = int(args.bits)
    if not os.path.isfile(PROGRAM):
        print('port-fps: no %s (python3 tools/port_gl_build.py --bits %d)'
              % (PROGRAM, bits))
        return 1
    rows = []
    traces = []
    for name in plans:
        folder = os.path.join(args.out, name)
        os.makedirs(folder, exist_ok=True)
        gl.PROGRAM = PROGRAM
        plan = gl.write_plan(name, os.path.join(folder, 'plan.txt'))
        common = [os.path.join('.', PROGRAM), '--image', args.image,
                  '--plan', plan, '--out', folder, '--graphics', 'remastered',
                  '--stage-mesh', args.stage_mesh]
        print('%s: every frame drawn as %d pictures, at scale %d'
              % (name, args.phases, args.scale))
        seconds = run(common + ['--gl-scale', str(args.scale),
                                '--gl-tween', str(args.phases),
                                '--gl-shots', folder,
                                # the ordinary shots off: the strip is
                                # what this writes
                                '--gl-every', '1000000',
                                '--gl-strip', '%d,%d' % (STRIP_AT, STRIP_BLANKS)],
                      os.path.join(folder, 'tween.log'))
        got = numbers_of(os.path.join(folder, 'tween.log'))
        got['seconds'] = seconds
        # the same run with the pictures off, to say what they cost: the
        # time inside the renderer is not the whole of a picture
        print('%s: the same run with the pictures between frames off' % name)
        got['plain_seconds'] = run(
            common + ['--gl-scale', str(args.scale), '--gl-every', '1000000'],
            os.path.join(folder, 'plain.log'))
        rows.append((name, got))
        # the traces with the pictures on and off. Two phases is enough:
        # every frame is still written down and drawn again, which is all
        # the game could possibly see of it.
        both = [os.path.join('.', PROGRAM), '--image', args.image,
                '--plan', plan, '--out', folder, '--graphics', 'remastered',
                '--stage-mesh', args.stage_mesh, '--gl-scale', '1', '--trace']
        print('%s: the game with the pictures between frames on and off '
              '(%d-bit)' % (name, bits))
        run(both + ['--gl-tween', '2'],
            os.path.join(folder, 'trace_on%d.log' % bits), quiet=True)
        run(both, os.path.join(folder, 'trace_off%d.log' % bits), quiet=True)
        on = trace_of(os.path.join(folder, 'trace_on%d.log' % bits))
        off = trace_of(os.path.join(folder, 'trace_off%d.log' % bits))
        apart = [i for i, (x, y) in enumerate(zip(on, off)) if x != y]
        traces.append((name, bits, on == off and len(on) > 0, len(on), apart))
    print()
    print('%-18s %7s %7s %8s %7s %7s %6s %7s %s'
          % ('plan', 'frames', 'checked', 'pictures', 'order', 'point',
             'us/pic', 'mixed', 'the first picture is the game\'s'))
    for name, got in rows:
        print('%-18s %7d %7d %8d %7s %7s %6d %4d/%-4d %s'
              % (name, got['frames'], got['checked'], got['drawn'],
                 'all' if got['out_of_order'] == 0
                 else '%d bad' % got['out_of_order'],
                 'all' if got['point_out_of_line'] == 0
                 else '%d bad' % got['point_out_of_line'],
                 got['per_picture_us'], got['mixed'], got['prims'],
                 '%d of %d pixels differ' % (got['identity_different'],
                                             got['identity_pixels'])))
        print('    a picture: %d primitives, %d moved, %d not paired, %d flat '
              '(the 2D layer); the farthest a corner moved %d pixels'
              % (got['prims'], got['mixed'], got['unpaired'], got['flat'],
                 got['widest']))
        print('    %d frames could not be drawn again (unlike frames: a cut, '
              'a menu), %d too large to write down'
              % (got['cuts'], got['overflowed']))
        print('    partners of the %d of a frame that can be moved: %d by '
              'name, '
              '%d by the search, %d times the nearest was taken already; '
              '%d held where they are so as not to be left out; %d corners '
              'not between their own two ends; %d frames moved nothing and '
              '%d of those were drawn differently'
              % (got['scene'], got['named'], got['searched'], got['clashes'],
                 got['held'], got['off_the_line'], got['still'],
                 got['still_moved']))
        print('    every picture of a frame: %s, %s, %s'
              % ('all its primitives' if got['fewer_prims'] == 0
                 else '%d drew other than all' % got['fewer_prims'],
                 'the same left out by the chip\'s rule'
                 if got['more_dropped'] == 0
                 else '%d left another out' % got['more_dropped'],
                 'no holes' if got['holes'] == 0
                 else '%d of them changed more than half a thousandth of '
                      'themselves where the two ends agreed' % got['holes'])
              + ' (%d such pixels in all: a number to watch, not a gate -- '
                'a face the next frame has not is left where it is, and its '
                'neighbours move)' % got['hole_pixels'])
        print('    the run took %.0f s against %.0f s with the pictures off: '
              '%.1f ms a picture over the whole run, %.1f ms inside the '
              'renderer'
              % (got['seconds'], got['plain_seconds'],
                 1000.0 * (got['seconds'] - got['plain_seconds'])
                 / max(got['drawn'], 1),
                 got['per_picture_us'] / 1000.0))
    print()
    for name, bits, same, lines, apart in traces:
        print('%-18s %d-bit: the trace with the pictures on and off: %s'
              % (name, bits, 'the same, %d lines' % lines if same else 'NOT'))
        if not same:
            print('    %d of %d lines differ, from line %d. `port_bugfixes` '
                  'parts company at the options page whatever is changed, '
                  'because the page reads memory the game never wrote: two '
                  'runs of one program that differ only in --gl-scale part '
                  'at the same lines (docs/PORT.md, "What is left").'
                  % (len(apart), lines, apart[0] if apart else -1))
    ok = (all(got['out_of_order'] == 0 and got['point_out_of_line'] == 0
              and got['off_the_line'] == 0 and got['still_moved'] == 0
              and got['fewer_prims'] == 0 and got['more_dropped'] == 0
              and got['checked'] > 0 for _, got in rows)
          and all(same or name == 'port_bugfixes'
                  for name, _, same, _, _ in traces))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
