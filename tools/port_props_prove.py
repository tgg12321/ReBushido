#!/usr/bin/env python3
"""Measures how still the stage's pieces stand in the pictures drawn
between two of the game's frames, with nothing shown (docs/PORT.md,
"The stage's pieces as meshes").

    python3 tools/port_props_prove.py --image "Bushido Blade 2 (USA).cue"
            [--stages 5,13,19,21,22,29] [--at 3020] [--blanks 12]
            [--phases 5] [--scale 2] [--still] [--out build/stageprops-jitter]

The railings, fences, walls and roofs of a stage are its pieces. Drawn
from the game's own primitives they are projected again every frame and
paired between two frames to draw the pictures between them; drawn from
the ground's mesh (`stage_props_mesh`) they are moved by the camera
alone. For each stage this runs build/port-gl/rebushido-gl over the
stage tour of tools/port_remaster_prove.py three times, each frame drawn
as `--phases` pictures, writing every picture of `--blanks` blanks from
blank `--at`:

  - `off`: the pieces from the game's primitives (the ground the mesh's);
  - `on`: the pieces from the mesh;
  - `hide`: the pieces taken out of the picture, which says which pixels
    are theirs (a pixel `off` and `hide` differ in).

Over the pixels that are a piece's in either of two neighbouring
pictures it counts, for each run:

  - `churn`: the mean change of a pixel from one picture to the next, the
    three channels summed. With `--still` (no one moves in the fight, so
    the camera does not either) everything of it is jitter;
  - `jolt`: the mean of the second difference, |a - 2b + c| over three
    pictures in a row: what moves smoothly has little of it, a corner
    that wobbles or a face that is held and then jumps a lot;
  - `jumps`: pixels of a piece whose second difference is over 96, a
    thousandth of them a picture;
  - `wobble`: how much the game's primitives' picture of the pieces moves
    other than the mesh's from one picture to the next, the mean of
    |(a_off - a_on) - (b_off - b_on)|. The mesh's picture is the pieces
    projected exactly with the camera of that picture, so this is the
    old path's own wobble and holding: what `on` takes away.

The traces of the three runs are held to each other: the game computes
the same either way. It prints a table and writes `jitter.tsv`.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_png  # noqa: E402
import port_remaster_prove as remaster  # noqa: E402

PROGRAM = remaster.PROGRAM
STAGE_NAMES = remaster.STAGE_NAMES
RUNS = ('off', 'on', 'hide')
JUMP = 96


def tour(path, frames, still):
    """The stage tour; with `still`, no one is moved in the fight."""
    remaster.write_tour(path, frames, 20)
    if still:
        with open(path, encoding='utf-8') as f:
            lines = f.read().split('\n')
        lines = [ln for ln in lines if not ln.strip().startswith('"fight+')]
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines))
    return path


def pictures(folder, phases):
    """The strip's pictures in the order a screen shows them: each frame's
    phases but the last, which is the next frame's first."""
    names = sorted(n for n in os.listdir(folder)
                   if n.endswith('.png') and '-p' in n)
    keep = []
    for name in names:
        phase = int(name.split('-p')[1].split('.')[0])
        if phase < phases - 1:
            keep.append(name)
    return keep


def pairing(err):
    """The mean of a picture's primitives to pair between two frames and
    of those left unpaired (gl_tween.c's own line)."""
    prims = unpaired = count = 0
    with open(err, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.search(r'gl tween blank \d+ prims (\d+) mixed \d+ '
                          r'unpaired (\d+)', line)
            if m:
                prims += int(m.group(1))
                unpaired += int(m.group(2))
                count += 1
    return prims // max(count, 1), unpaired // max(count, 1)


def measure(folders, names):
    """churn, jolt and jumps of each run over the pieces' pixels."""
    got = {run: {'churn': 0, 'jolt': 0, 'jumps': 0, 'pixels': 0,
                 'jolt_pixels': 0} for run in ('off', 'on')}
    got['wobble'] = 0
    frames = {}

    def load(run, name):
        key = (run, name)
        if key not in frames:
            frames[key] = port_png.read_png(os.path.join(folders[run], name))
        return frames[key]

    def mask_of(name):
        _, _, a = load('off', name)
        _, _, b = load('hide', name)
        _, _, c = load('on', name)
        n = len(a) // 3
        mask = bytearray(n)
        for i in range(n):
            j = 3 * i
            if (a[j] != b[j] or a[j + 1] != b[j + 1] or a[j + 2] != b[j + 2]
                    or c[j] != b[j] or c[j + 1] != b[j + 1]
                    or c[j + 2] != b[j + 2]):
                mask[i] = 1
        return mask

    masks = {}
    for name in names:
        masks[name] = mask_of(name)
    for k in range(1, len(names)):
        one, two = names[k - 1], names[k]
        both = bytes(x | y for x, y in zip(masks[one], masks[two]))
        count = sum(both)
        for run in ('off', 'on'):
            _, _, a = load(run, one)
            _, _, b = load(run, two)
            total = 0
            for i, m in enumerate(both):
                if m:
                    j = 3 * i
                    total += (abs(a[j] - b[j]) + abs(a[j + 1] - b[j + 1])
                              + abs(a[j + 2] - b[j + 2]))
            got[run]['churn'] += total
            got[run]['pixels'] += count
        _, _, a_off = load('off', one)
        _, _, b_off = load('off', two)
        _, _, a_on = load('on', one)
        _, _, b_on = load('on', two)
        total = 0
        for i, m in enumerate(both):
            if m:
                for j in range(3 * i, 3 * i + 3):
                    total += abs((a_off[j] - a_on[j]) - (b_off[j] - b_on[j]))
        got['wobble'] += total
    for k in range(1, len(names) - 1):
        one, two, three = names[k - 1], names[k], names[k + 1]
        both = bytes(x | y | z for x, y, z in
                     zip(masks[one], masks[two], masks[three]))
        for run in ('off', 'on'):
            _, _, a = load(run, one)
            _, _, b = load(run, two)
            _, _, c = load(run, three)
            total = 0
            jumps = 0
            count = 0
            for i, m in enumerate(both):
                if m:
                    j = 3 * i
                    d = (abs(a[j] - 2 * b[j] + c[j])
                         + abs(a[j + 1] - 2 * b[j + 1] + c[j + 1])
                         + abs(a[j + 2] - 2 * b[j + 2] + c[j + 2]))
                    total += d
                    count += 1
                    if d > JUMP:
                        jumps += 1
            got[run]['jolt'] += total
            got[run]['jumps'] += jumps
            got[run]['jolt_pixels'] += count
        # the pictures before are not needed again
        for run in RUNS:
            frames.pop((run, one), None)
    return got


def main():
    os.chdir(ROOT)
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', default='Bushido Blade 2 (USA).cue')
    ap.add_argument('--stages', default='5,13,19,21,22,29')
    ap.add_argument('--at', type=int, default=3020)
    ap.add_argument('--blanks', type=int, default=12)
    ap.add_argument('--phases', type=int, default=5)
    ap.add_argument('--scale', type=int, default=2)
    ap.add_argument('--aspect', default='16:9')
    ap.add_argument('--still', action='store_true')
    # measure the pictures a run before left, running nothing
    ap.add_argument('--again', action='store_true')
    ap.add_argument('--out', default='build/stageprops-jitter')
    args = ap.parse_args()
    if not os.path.isfile(PROGRAM):
        print('port_props_prove: no %s (python3 tools/port_gl_build.py)'
              % PROGRAM)
        return 1
    if not os.path.isfile(args.image):
        print('port_props_prove: no %s: --image names your own image of the '
              'disc' % args.image)
        return 1
    stages = [int(s) for s in args.stages.split(',')]
    os.makedirs(args.out, exist_ok=True)
    # the run ends with the strip: every picture of the fight up to it
    # is drawn as `--phases` pictures too, which is most of the time
    frames = args.at + args.blanks + 4
    toml_path = tour(os.path.join(args.out, 'tour%s.toml'
                                  % ('-still' if args.still else '')),
                     frames, args.still)
    rows = []
    for stage in stages:
        folder = os.path.join(args.out, '%02d-%s%s' % (
            stage, STAGE_NAMES[stage], '-still' if args.still else ''))
        os.makedirs(folder, exist_ok=True)
        plan = remaster.plan_for(
            toml_path, os.path.join(folder, 'plan.txt'), stage, frames)
        folders = {}
        traces = {}
        going = []
        for run in RUNS:
            shots = os.path.join(folder, run)
            if args.again:
                folders[run] = shots
                continue
            os.makedirs(shots, exist_ok=True)
            for old in os.listdir(shots):
                if old.endswith('.png'):
                    os.remove(os.path.join(shots, old))
            command = [os.path.join('.', PROGRAM), '--image', args.image,
                       '--plan', plan, '--out', shots,
                       '--graphics', 'remastered', '--draw-distance', 'max',
                       '--aspect', args.aspect,
                       '--gl-scale', str(args.scale),
                       '--gl-tween', str(args.phases), '--gl-shots', shots,
                       '--gl-every', '1000000',
                       '--gl-strip', '%d,%d' % (args.at, args.blanks),
                       '--stage-mesh', 'every', '--stage-props',
                       'every' if run == 'on' else run, '--trace']
            # the three runs at once: each is one program of its own
            log = open(os.path.join(folder, run + '.log'), 'w')
            err = open(os.path.join(folder, run + '.err'), 'w')
            going.append((run, command, subprocess.Popen(
                command, stdout=log, stderr=err, cwd=ROOT), log, err))
            folders[run] = shots
        if args.again:
            for run in RUNS:
                with open(os.path.join(folder, run + '.log'), 'rb') as f:
                    traces[run] = f.read()
        for run, command, process, log, err in going:
            process.wait()
            log.close()
            err.close()
            # a run that could not start (the system may not give the
            # scratchpad's address at once) is run again, alone
            for _ in range(2):
                if os.path.getsize(os.path.join(folder, run + '.log')) > 0:
                    break
                remaster.run(command, os.path.join(folder, run + '.log'),
                             os.path.join(folder, run + '.err'))
            with open(os.path.join(folder, run + '.log'), 'rb') as f:
                traces[run] = f.read()
        names = pictures(folders['off'], args.phases)
        names = [n for n in names
                 if all(os.path.exists(os.path.join(folders[r], n))
                        for r in RUNS)]
        got = measure(folders, names)
        for run in ('off', 'on'):
            got[run]['prims'], got[run]['unpaired'] = pairing(
                os.path.join(folder, run + '.err'))
        same = traces['off'] == traces['on'] == traces['hide']
        row = (stage, STAGE_NAMES[stage], same, len(names), got)
        rows.append(row)
        print_row(row)
        sys.stdout.flush()
    with open(os.path.join(args.out, 'jitter%s.tsv'
                           % ('-still' if args.still else '')),
              'w', encoding='utf-8', newline='\n') as f:
        f.write('stage\tname\tsame_trace\tpictures\trun\tchurn\tjolt\t'
                'jumps_per_mille\twobble\n')
        for stage, name, same, count, got in rows:
            for run in ('off', 'on'):
                one = got[run]
                f.write('%d\t%s\t%d\t%d\t%s\t%.3f\t%.3f\t%.3f\t%.3f\n'
                        % (stage, name, 1 if same else 0, count, run,
                           one['churn'] / max(one['pixels'], 1),
                           one['jolt'] / max(one['jolt_pixels'], 1),
                           1000.0 * one['jumps']
                           / max(one['jolt_pixels'], 1),
                           got['wobble'] / max(got['off']['pixels'], 1)
                           if run == 'off' else 0.0))
    return 0


def print_row(row):
    stage, name, same, count, got = row
    off = got['off']
    on = got['on']
    print('%-24s trace %-6s pictures %3d  churn %6.2f -> %6.2f  '
          'jolt %6.2f -> %6.2f  jumps %6.2f -> %6.2f per mille  '
          'paired a frame %d -> %d, unpaired %d -> %d  wobble %.2f'
          % (name, 'same' if same else 'DIFFER', count,
             off['churn'] / max(off['pixels'], 1),
             on['churn'] / max(on['pixels'], 1),
             off['jolt'] / max(off['jolt_pixels'], 1),
             on['jolt'] / max(on['jolt_pixels'], 1),
             1000.0 * off['jumps'] / max(off['jolt_pixels'], 1),
             1000.0 * on['jumps'] / max(on['jolt_pixels'], 1),
             off['prims'], on['prims'], off['unpaired'], on['unpaired'],
             got['wobble'] / max(off['pixels'], 1)))


if __name__ == '__main__':
    sys.exit(main())
