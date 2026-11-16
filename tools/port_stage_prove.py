#!/usr/bin/env python3
"""Proves the stage's ground drawn as one mesh against the game's own
tiles, stage by stage, with nothing shown (docs/PORT.md, "Remastered",
"The ground as one mesh").

    python3 tools/port_stage_prove.py --image "Bushido Blade 2 (USA).cue"
            [--stages 0-31] [--scale 2] [--frames 4200] [--every 300]
            [--out build/stagemesh] [--pairs 2] [--distance max]

For each stage it runs build/port-gl/rebushido-gl twice over the same
tour -- the first fighter run across the stage, which swings the camera
right round -- once with `--stage-mesh off` and once with `on`, and
reports:

  - that the trace of every watched value is the same both ways, which is
    the whole claim that the game is untouched;
  - how many pixels of the pictures differ, how many by more than a
    twentieth of a channel, and how many are pixels where the tiles drew
    the stage and the mesh drew nothing (a hole, which would be the one
    unforgivable fault) or the other way about (ground the tiles left
    out, which is what the mesh is for);
  - what the frame cost each way.

With --pairs N it writes N pictures a stage of the two side by side
(tiles | mesh) into the stage's folder.

It needs the prover (tools/port_gl_build.py) and your own image of the
disc. No check runs it: CI has no graphics card.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_png
import port_remaster_prove as remaster

PROGRAM = remaster.PROGRAM
STAGE_NAMES = remaster.STAGE_NAMES


def read(path):
    width, height, pixels = port_png.read_png(path)
    return width, height, pixels


def look(a_dir, b_dir, pairs, folder):
    """The two runs' pictures against each other."""
    got = {'shots': 0, 'pixels': 0, 'differ': 0, 'far': 0, 'worst': 0,
           'holes': 0, 'filled': 0}
    if not os.path.isdir(a_dir) or not os.path.isdir(b_dir):
        return got
    names = sorted(n for n in os.listdir(a_dir) if n.endswith('.png'))
    made = 0
    for name in names:
        b_path = os.path.join(b_dir, name)
        if not os.path.exists(b_path):
            continue
        aw, ah, ap = read(os.path.join(a_dir, name))
        bw, bh, bp = read(b_path)
        if (aw, ah) != (bw, bh):
            continue
        got['shots'] += 1
        got['pixels'] += aw * ah
        for i in range(0, len(ap), 3):
            a = ap[i:i + 3]
            b = bp[i:i + 3]
            if a == b:
                continue
            got['differ'] += 1
            d = max(abs(a[j] - b[j]) for j in range(3))
            if d > 32:
                got['far'] += 1
            if d > got['worst']:
                got['worst'] = d
            # black is what the game clears its picture to: a pixel the
            # tiles drew and the mesh did not is a hole in the ground
            if b == b'\0\0\0' and a != b'\0\0\0':
                got['holes'] += 1
            elif a == b'\0\0\0':
                got['filled'] += 1
        if made < pairs:
            made += 1
            out = os.path.join(folder, 'pair-' + name)
            wide = bytearray(aw * 2 * ah * 3)
            for y in range(ah):
                at = y * aw * 2 * 3
                wide[at:at + aw * 3] = ap[y * aw * 3:(y + 1) * aw * 3]
                wide[at + aw * 3:at + aw * 6] = bp[y * aw * 3:(y + 1) * aw * 3]
            port_png.write_png(out, aw * 2, ah, bytes(wide))
    return got


def same_trace(a, b):
    if not os.path.exists(a) or not os.path.exists(b):
        return False
    with open(a, 'rb') as f:
        one = f.read()
    with open(b, 'rb') as f:
        two = f.read()
    return one == two


def main():
    os.chdir(ROOT)
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', default='Bushido Blade 2 (USA).cue')
    ap.add_argument('--stages', default='0-31')
    ap.add_argument('--scale', type=int, default=2)
    ap.add_argument('--tall', type=int, default=0)
    ap.add_argument('--frames', type=int, default=4200)
    ap.add_argument('--every', type=int, default=300)
    ap.add_argument('--distance', default='max')
    ap.add_argument('--aspect', default='4:3')
    ap.add_argument('--pairs', type=int, default=0)
    ap.add_argument('--out', default='build/stagemesh')
    args = ap.parse_args()
    if not os.path.isfile(PROGRAM):
        print('port_stage_prove: no %s (python3 tools/port_gl_build.py)'
              % PROGRAM)
        return 1
    if not os.path.isfile(args.image):
        print('port_stage_prove: no %s: --image names your own image of the '
              'disc' % args.image)
        return 1
    stages = []
    for piece in args.stages.split(','):
        if '-' in piece:
            first, last = piece.split('-')
            stages += list(range(int(first), int(last) + 1))
        else:
            stages.append(int(piece))
    stages = [s for s in stages if 0 <= s < remaster.FIGHT_STAGES]
    os.makedirs(args.out, exist_ok=True)
    toml_path = remaster.write_tour(
        os.path.join(args.out, 'tour.toml'), args.frames, 20)
    rows = []
    for stage in stages:
        folder = os.path.join(args.out, '%02d-%s' % (stage, STAGE_NAMES[stage]))
        os.makedirs(folder, exist_ok=True)
        plan = remaster.plan_for(
            toml_path, os.path.join(folder, 'plan.txt'), stage, args.frames)
        runs = {}
        for which in ('off', 'on'):
            shots = os.path.join(folder, which)
            os.makedirs(shots, exist_ok=True)
            for old in os.listdir(shots):
                if old.endswith('.png'):
                    os.remove(os.path.join(shots, old))
            common = [os.path.join('.', PROGRAM), '--image', args.image,
                      '--plan', plan, '--out', shots,
                      '--graphics', 'remastered',
                      '--draw-distance', args.distance,
                      '--aspect', args.aspect,
                      '--gl-scale', str(args.scale),
                      '--stage-mesh', which,
                      '--gl-shots', shots, '--gl-every', str(args.every),
                      '--trace']
            if args.tall:
                common += ['--gl-tall', str(args.tall)]
            log = os.path.join(folder, which + '.log')
            err = os.path.join(folder, which + '.err')
            for _ in range(3):
                seconds = remaster.run(common, log, err)
                got = remaster.numbers_of(log, err)
                if got['frames'] > 0:
                    break
                print('    (no frames; trying again)')
            got['seconds'] = seconds
            runs[which] = got
        same = same_trace(os.path.join(folder, 'off.log'),
                          os.path.join(folder, 'on.log'))
        seen = look(os.path.join(folder, 'off'), os.path.join(folder, 'on'),
                    args.pairs, folder)
        row = {'stage': stage, 'name': STAGE_NAMES[stage], 'same_trace': same,
               'off': runs['off'], 'on': runs['on'], 'pixels': seen}
        rows.append(row)
        print('%-24s trace %-5s  pictures %2d  differ %8d of %9d (%5.2f%%)  '
              'far %7d  holes %6d  filled %7d  worst %3d  '
              'vertices %6d -> %6d  %5.1f s -> %5.1f s'
              % (row['name'], 'same' if same else 'DIFFER', seen['shots'],
                 seen['differ'], seen['pixels'],
                 100.0 * seen['differ'] / max(seen['pixels'], 1), seen['far'],
                 seen['holes'], seen['filled'], seen['worst'],
                 runs['off']['vertices_mean'], runs['on']['vertices_mean'],
                 runs['off']['seconds'], runs['on']['seconds']))
        sys.stdout.flush()
    table = os.path.join(args.out, 'measured.tsv')
    with open(table, 'w', encoding='utf-8', newline='\n') as f:
        f.write('stage\tname\tsame_trace\tpictures\tpixels\tdiffer\tfar\t'
                'holes\tfilled\tworst\tvertices_off\tvertices_on\t'
                'seconds_off\tseconds_on\n')
        for row in rows:
            seen = row['pixels']
            f.write('%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%.1f\n'
                    % (row['stage'], row['name'], 1 if row['same_trace'] else 0,
                       seen['shots'], seen['pixels'], seen['differ'],
                       seen['far'], seen['holes'], seen['filled'],
                       seen['worst'], row['off']['vertices_mean'],
                       row['on']['vertices_mean'], row['off']['seconds'],
                       row['on']['seconds']))
    print()
    print('port_stage_prove: %d stages; %s' % (len(rows), table))
    return 0


if __name__ == '__main__':
    sys.exit(main())
