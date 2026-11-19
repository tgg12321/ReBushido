#!/usr/bin/env python3
"""The game's own motions of a fighter, as the port poses him: each frame's
21 bones of each fighter written by the headless program
(`--gl-fighter-motion`), over fights set up so that he walks, runs, strikes
high, middle and low, guards, changes stance, jumps, crouches, is struck
and falls. What the model pipeline's checks pose a model through, and what
Blender plays on a model for review (tools/port_models/README.md).

    python3 tools/port_models/motions.py --fighter tatsumi [--against utsusemi,...]
            [--out tmp/models/ref/motions] [--blanks 4800]

Runs in WSL (the program is build/port-gl/rebushido-gl, `python3
tools/port_gl_build.py`). For each opponent two fights: the fighter on the
first pad and the opponent on the second, both pressed by a script of
the same seed, then the two swapped, so that each side's motions and both
ends of a kill are seen. A press script and not the CPU: the CPU wins in
a few seconds and a fighter lying dead teaches nothing. Each fight's
folder holds `<model>.<slot>.motion.txt`, a line a frame (the frame's
serial, then each bone's nine turns in 4096ths and three places).

Nothing of the game is written into the repository: the motions are read
from the player's disc by the game, into a folder git ignores.
"""
import argparse
import os
import random
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROGRAM = os.path.join('build', 'port-gl', 'rebushido-gl')
# what a fighter is told to do, and for how many blanks the button is held:
# the strikes alone and with each direction, stance and guard, the moves
ACTIONS = (
    [('CIRCLE', 6)], [('CROSS', 6)], [('SQUARE', 6)],
    [('UP', 4), ('CIRCLE', 6)], [('DOWN', 4), ('CROSS', 6)], [('RIGHT', 4), ('SQUARE', 6)],
    [('LEFT', 4), ('CIRCLE', 6)], [('TRIANGLE', 6)], [('L1', 6)], [('R1', 6)],
    [('L2', 6)], [('R2', 6)], [('RIGHT', 40)], [('LEFT', 30)], [('UP', 30)], [('DOWN', 30)],
    [('RIGHT', 20), ('R2', 6)], [('DOWN', 50)], [('UP', 6), ('CROSS', 6)],
    [('L1', 4), ('CIRCLE', 6)], [('R1', 4), ('SQUARE', 6)],
)
# The fight is in by this blank (docs/PORT.md, "Straight into a fight").
FIGHT_AT = 600


def script(seed, blanks):
    """`in` lines of a plan for both pads: each pad's next action after a
    pause of its own, from the fight's start to `blanks`."""
    rng = random.Random(seed)
    lines = []
    for pad in (1, 2):
        at = FIGHT_AT + 30 + 17 * (pad - 1)
        while at < blanks - 60 and len(lines) < 1000:
            for button, hold in rng.choice(ACTIONS):
                lines.append('in 0 %d %d %s %d' % (at, pad, button, hold))
                at += min(hold, 6) + 1
            at += rng.randint(20, 70)
    return lines


def run(fighter, opponent, stage, seed, blanks, image, models, out):
    os.makedirs(out, exist_ok=True)
    plan = os.path.join(out, 'plan.txt')
    with open(plan, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(script(seed, blanks)) + '\n')
    cmd = ['./' + PROGRAM, '--image', image, '--fight', stage, fighter, opponent, '--fight-2p',
           '--frames', str(blanks), '--no-cards', '--graphics', 'remastered', '--gl-scale', '1',
           '--fighter-models', models, '--plan', plan, '--gl-fighter-motion', out]
    with open(os.path.join(out, 'run.log'), 'w', encoding='utf-8', errors='replace', newline='\n') as log:
        done = subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=log)
    if done.returncode != 0:
        sys.exit('motions: %s against %s ended with %d (%s)' % (fighter, opponent, done.returncode,
                                                                 os.path.join(out, 'run.log')))
    # the slots read before a model was loaded write a line or two
    for f in os.listdir(out):
        path = os.path.join(out, f)
        if f.endswith('.motion.txt'):
            with open(path, encoding='ascii') as m:
                lines = sum(1 for _ in m)
            if lines < 30:
                os.remove(path)
    return sorted(f for f in os.listdir(out) if f.endswith('.motion.txt'))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--fighter', required=True, help='a roster name (--fight takes it)')
    ap.add_argument('--against', default='utsusemi,gengoro',
                    help='opponents, a comma between (default: utsusemi,gengoro)')
    ap.add_argument('--stage', default='tatara-shrine')
    ap.add_argument('--blanks', type=int, default=4800)
    ap.add_argument('--image', default='Bushido Blade 2 (USA).cue')
    ap.add_argument('--models', default='tmp/models/ref',
                    help='a folder of model files: the fighters must be drawn from files for '
                         'their bones to be written (the reference export will do)')
    ap.add_argument('--out', default='tmp/models/ref/motions')
    args = ap.parse_args()
    os.chdir(ROOT)
    if not os.path.exists(PROGRAM):
        sys.exit('motions: no %s (python3 tools/port_gl_build.py)' % PROGRAM)
    for k, opponent in enumerate(args.against.split(',')):
        for side, (one, two) in enumerate(((args.fighter, opponent), (opponent, args.fighter))):
            out = os.path.join(args.out, '%s-%s' % (one, two))
            if os.path.isdir(out):
                shutil.rmtree(out)
            files = run(one, two, args.stage, 11 + 7 * k + side, args.blanks, args.image,
                        args.models, out)
            print('motions: %s against %s: %s' % (one, two, ', '.join(files)))


if __name__ == '__main__':
    main()
