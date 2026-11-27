#!/usr/bin/env python3
"""How quickly a CPU answers an attack, from the logs the native port writes
with --cpu-log (src/port/cpu_run.c; docs/CPU.md, "How quickly it answers").

  python3 tools/port_cpu_react.py LOG...

Each log line is a fighter's pad as the game is given it, and his state as
the frame begins for him: `f FRAME FIGHTER WHO KIND MOTION MOVE_FRAME
HIT_START ATTACK_KIND X Z LEGS ARM BODY OFF_ARM HELD DEFEATED SCRIPT`, WHO 0
for a player's pad, 1 for the game's CPU, 2 for the port's; SCRIPT the
CPU's pad script, its kind 1 up (enum CpuScriptKind), or 0.

An attack is counted where one fighter's move becomes an attack while the
other stands or walks within 6000 of him. Its frame is the frame the
attacker's update began it (the frame before the first line that shows it).
The answer is the frame the other's CPU begins a guard or a step (its pad
script's kind), counted from that frame: 0 is the same frame, before the
frame was drawn; the second fighter is played after the first, so 0 can
only be his. (The game's CPU presses a guard's button the frame after it
decides on it, the port's in the same frame.) Also: of the guards, how
many frames before the blade's first frame they were decided on.

Prints, for each kind of CPU and each place, how many attacks, how many
answered within 30 frames, and the frames to the answer (least, median,
the quartiles), and the guards' frames before the blow."""
import argparse
import statistics
import sys

ATTACK_KINDS = {2, 27, 40, 38}  # MOVE_IS_ATTACK
FREE_KINDS = {21, 25}           # standing in a stance, walking
FRONTAL = 0x20
REVERSE = 0x40
AWAY = 0x8000
# the CPU's pad scripts (enum CpuScriptKind, 1 up): a guard, footwork (a
# step back or aside)
GUARD = 4
FOOTWORK = 5
ANSWERS = (GUARD, FOOTWORK)
WHO = {0: 'player', 1: 'original CPU', 2: "port's CPU"}


def read(path):
    """{frame: {fighter: fields}}"""
    frames = {}
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            w = line.split()
            if len(w) != 18 or w[0] != 'f':
                continue
            v = [int(x) for x in w[1:]]
            keys = ('frame', 'fighter', 'who', 'kind', 'motion', 'move_frame', 'hit_start', 'attack_kind',
                    'x', 'z', 'legs', 'arm', 'body', 'off_arm', 'held', 'defeated', 'script')
            row = dict(zip(keys, v))
            # a frame run again (a rollback) keeps its last
            frames.setdefault(row['frame'], {})[row['fighter']] = row
    return frames


def answers(frames):
    """[(who, place, frames to the answer or None, guard's frames before the blow or None)]"""
    out = []
    order = sorted(frames)
    for i in range(1, len(order)):
        t, before = order[i], order[i - 1]
        for attacker in (0, 1):
            defender = 1 - attacker
            now = frames[t].get(attacker)
            prev = frames[before].get(attacker)
            them = frames[before].get(defender)
            if not now or not prev or not them:
                continue
            if now['kind'] not in ATTACK_KINDS:
                continue
            if prev['kind'] in ATTACK_KINDS and prev['motion'] == now['motion']:
                continue
            if them['kind'] not in FREE_KINDS or them['who'] == 0:
                continue
            dx, dz = now['x'] - them['x'], now['z'] - them['z']
            if dx * dx + dz * dz > 6000 * 6000:
                continue
            start = before  # the attacker's update began it in this frame
            answer = None
            guard = None
            before_script = them['script']
            for u in range(start, start + 31):
                row = frames.get(u, {}).get(defender)
                if not row:
                    break
                begun = row['script'] in ANSWERS and before_script not in ANSWERS
                before_script = row['script']
                if begun:
                    answer = u - start
                    attacker_row = frames.get(u, {}).get(attacker)
                    if row['script'] == GUARD and attacker_row and attacker_row["kind"] in ATTACK_KINDS and attacker_row['hit_start'] != 255:
                        guard = attacker_row['hit_start'] - attacker_row['move_frame']
                    break
            out.append((them['who'], defender, answer, guard, now['hit_start'] if now['hit_start'] != 255 else None))
    return out


def summary(values):
    if not values:
        return 'none'
    values = sorted(values)
    q = statistics.quantiles(values, n=4) if len(values) > 1 else [values[0]] * 3
    return 'least %d, quartiles %g / %g / %g, most %d' % (values[0], q[0], q[1], q[2], values[-1])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('logs', nargs='+')
    args = parser.parse_args()
    found = {}
    for path in args.logs:
        for who, place, answer, guard, blade in answers(read(path)):
            found.setdefault((who, place), []).append((answer, guard, blade))
    if not found:
        print('port_cpu_react: no attacks met by a CPU in the logs')
        return 1
    for (who, place), rows in sorted(found.items()):
        answered = [a for a, _, _ in rows if a is not None]
        guards = [g for _, g, _ in rows if g is not None]
        blades = [b for _, _, b in rows if b is not None]
        print('%s, place %d: %d attacks, %d answered within 30 frames' % (WHO[who], place + 1, len(rows),
                                                                         len(answered)))
        print('  frames from the attack to the answer: %s' % summary(answered))
        print('  same frame (0): %d, next frame (1): %d, under a fifth of a second (6): %d'
              % (answered.count(0), answered.count(1), sum(1 for a in answered if a < 6)))
        print("  the attacks' frames to the blade's first: %s" % summary(blades))
        print('  guards: %d, frames before the blade\'s first: %s' % (len(guards), summary(guards)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
