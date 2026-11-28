#!/usr/bin/env python3
"""The CPU's levels measured against each other (docs/CPU.md, "Measuring
the levels"): fights of the CPU against the CPU, played headless on the
native port by plans of the play tests' kind, one program a fight.

  python3 tools/port_cpu_bench.py A B [--fights N] [-j N] [--bits 64|32]
      [--seed S] [--stages ...] [--fighters ...] [--out DIR] [--log]

A and B are the two fighters' CPUs: 0 the original's, 1 to 4 the port's
levels (`--cpu A,B`, src/port/cpu.c). Fight k is on a stage and between two
fighters drawn from the lists by k, with the port CPU's seed S + k; half of
the fights put A in the first place and half in the second, so neither has
the first fighter's side. Each fight ends when one falls (or at the limit,
a draw). It prints each fight (with its length in the fight's frames) and
the win rate of A over B, and how many fights went past three minutes,
where a tournament's judges would have decided them (docs/CPU.md, "Fights
to the clock"). `--program` runs another build (a copy of
build/port/rebushido kept to compare against).

`--log` keeps each fight's frame by frame record of both fighters
(`--cpu-log`, src/port/cpu.c) in the fight's folder, for
`tools/port_cpu_react.py`.

`--check` holds determinism instead: the same fight twice on each size,
and on both sizes, must print the same trace, byte for byte.

Runs in WSL with the programs `make port` builds and the player's disc
(--image, REBUSHIDO_RETAIL, or the image at the repository's root). Exit 0, or 1 if
a run failed (or `--check` found a difference)."""
import argparse
import concurrent.futures
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

TUNE = []
# the fight's frames (30 a second) after which a tournament's fight of two
# CPUs is judged (PORT_TOURNAMENT_JUDGE_FRAMES, include/port_tournament.h)
JUDGE_FRAMES = 5400
PROGRAMS = {64: os.path.join('build', 'port', 'rebushido'),
            32: os.path.join('build', 'port32', 'rebushido')}

# stages with open ground and no ring out to speak of, and fighters with
# blades (a gun's fight is another game)
STAGES = ['tatara-shrine', 'bamboo-thicket', 'meikyokan-dojo', 'orange-tree-fields',
          'terraced-fields', 'lumberyard', 'kimigaura-coast', 'outside-meikyokan']
FIGHTERS = ['kannuki', 'tatsumi', 'mikado', 'utsusemi', 'red-shadow', 'tsubame',
            'gengoro', 'kaun', 'jo']
WEAPONS = ['katana', 'long-sword', 'nodachi', 'broadsword', 'naginata', 'yari']

FIGHT_TOML = '''about = "The CPU against the CPU (tools/port_cpu_bench.py)"
discs = ["port"]
cards = "none"
limit = %(limit)d
seconds = 900
input = []

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 4000
shot = false

[[checkpoint]]
name = "end"
when = "winner != FIGHT_NOBODY"
at_frame = true
shot = false

# a little of the fall, for the log
[[checkpoint]]
name = "after"
frames = 20
shot = false
'''


def fight_args(k, a, b, stages, fighters, weapons=None, second_weapons=None):
    """The k-th fight: its stage, its two fighters and the CPUs of the first
    and the second place, with which of A and B is first. `weapons` are the
    first fighter's to draw from (WEAPONS if none), `second_weapons` the
    second's (the same if none)."""
    weapons = weapons or WEAPONS
    second_weapons = second_weapons or weapons
    stage = stages[k % len(stages)]
    first = fighters[(k * 7 + 1) % len(fighters)]
    second = fighters[(k * 5 + 3) % len(fighters)]
    w1 = weapons[(k * 3) % len(weapons)]
    w2 = second_weapons[(k * 5 + 2) % len(second_weapons)]
    swapped = (k // 1) % 2 == 1
    cpus = (b, a) if swapped else (a, b)
    return stage, '%s:%s' % (first, w1), '%s:%s' % (second, w2), cpus, swapped


def plan_for(program, out, limit):
    import playtest
    toml = os.path.join(out, 'cpu_fight.toml')
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write(FIGHT_TOML % {'limit': limit})
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    names, lines = test.plan(disc, playtest.header_names(), test.discs.index('port') + 1)
    plan = os.path.join(out, 'plan.txt')
    with open(plan, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return plan, names


def run_fight(program, plan, order, out, stage, f1, f2, cpus, seed, log):
    import playtest
    os.makedirs(out, exist_ok=True)
    cmd = [program, '--image', playtest.RETAIL_CUE, '--plan', plan, '--no-cards', '--mods', '',
           '--out', out, '--fight', stage, f1, f2, '--fight-cpu',
           '--cpu', '%d,%d' % cpus, '--cpu-seed', str(seed)] + [w for t in TUNE for w in ('--cpu-tune', t)]
    if log:
        cmd += ['--cpu-log', os.path.join(out, 'cpu_log.txt')]
    with open(os.path.join(out, 'trace.txt'), 'wb') as o, open(os.path.join(out, 'log.txt'), 'wb') as e:
        code = subprocess.run(cmd, cwd=REPO, stdout=o, stderr=e, stdin=subprocess.DEVNULL,
                              timeout=1800).returncode
    with open(os.path.join(out, 'trace.txt'), encoding='utf-8', errors='replace') as f:
        trace = f.read()
    winner = None
    frames = None
    for line in trace.splitlines():
        w = line.split()
        if len(w) > 3 and w[0] == 'cp' and w[1] == 'end':
            values = dict(zip(order, w[3:]))
            winner = int(values['winner'])
            frames = max(int(values.get('p1_frames_alive', -1)), int(values.get('p2_frames_alive', -1)))
    return code, winner, frames, trace


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('a', type=int, help='the first CPU: 0 the original, 1 to 4 the port\'s levels')
    parser.add_argument('b', type=int, help='the second CPU')
    parser.add_argument('--fights', type=int, default=20)
    parser.add_argument('-j', type=int, default=4, help='fights at once')
    parser.add_argument('--bits', type=int, default=64, choices=(64, 32))
    parser.add_argument('--program', help='the program to run (a copy of build/port/rebushido, say)')
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--first', type=int, default=0, help='the number of the first fight')
    parser.add_argument('--limit', type=int, default=24000, help='blanks before a fight is a draw')
    parser.add_argument('--stages', nargs='*', default=STAGES)
    parser.add_argument('--fighters', nargs='*', default=FIGHTERS)
    parser.add_argument('--weapons', nargs='*', default=WEAPONS,
                        help="the first place's weapons to draw from (long-gun, short-gun: a gun's fights)")
    parser.add_argument('--second-weapons', nargs='*',
                        help="the second place's, if not the first's")
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'cpu_bench'))
    parser.add_argument('--image', help="the player's disc (the play tests' own if not given)")
    parser.add_argument('--tune', action='append', default=[], help='LEVEL:FIELD=VALUE,... (--cpu-tune), to try numbers')
    parser.add_argument('--log', action='store_true', help='keep each fight\'s --cpu-log')
    parser.add_argument('--check', action='store_true', help='hold determinism: the same fight on both sizes, twice')
    args = parser.parse_args()
    TUNE.extend(args.tune)

    os.makedirs(args.out, exist_ok=True)
    if args.image:
        import playtest
        playtest.RETAIL_CUE = args.image
    if args.check:
        return check(args)
    program = os.path.abspath(args.program) if args.program else os.path.join(REPO, PROGRAMS[args.bits])
    if not os.path.exists(program):
        print('port_cpu_bench: no %s (make port)' % program)
        return 1
    plan, order_names = plan_for(program, args.out, args.limit)
    order = [line.split()[1] for line in open(plan) if line.startswith('var ')]
    jobs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as pool:
        for k in range(args.first, args.first + args.fights):
            stage, f1, f2, cpus, swapped = fight_args(k, args.a, args.b, args.stages, args.fighters,
                                                      args.weapons, args.second_weapons)
            out = os.path.join(args.out, 'fight%04d' % k)
            jobs.append((k, stage, f1, f2, cpus, swapped,
                         pool.submit(run_fight, program, plan, order, out, stage, f1, f2, cpus,
                                     args.seed + k, args.log)))
        wins = losses = draws = failed = judged = 0
        lengths = []
        for k, stage, f1, f2, cpus, swapped, job in jobs:
            code, winner, frames, trace = job.result()
            if winner is None:
                if code in (0, 1) and any(line.startswith('cp fight ') for line in trace.splitlines()):
                    result = 'draw'
                    draws += 1
                    judged += 1
                else:
                    result = 'FAILED (exit %d)' % code
                    failed += 1
            else:
                a_won = (winner == 1) if swapped else (winner == 0)
                result = 'A' if a_won else 'B'
                wins += a_won
                losses += not a_won
                lengths.append(frames)
                if frames > JUDGE_FRAMES:
                    judged += 1
                    result += ' (judged)'
            print('fight %4d  %-20s %-22s %-22s cpus %d,%d  frames %5s  winner %s'
                  % (k, stage, f1, f2, cpus[0], cpus[1], frames if winner is not None else '-', result), flush=True)
    decided = wins + losses
    lengths.sort()
    print('A (%d) against B (%d): %d of %d fights past %d frames (the tournament judges them), %d to the limit; '
          'the fights decided: median %d frames, three quarters within %d'
          % (args.a, args.b, judged, args.fights, JUDGE_FRAMES, draws,
             lengths[len(lengths) // 2] if lengths else 0, lengths[len(lengths) * 3 // 4] if lengths else 0))
    print('A (%d) against B (%d): %d fights, A won %d, B won %d, %d draws, %d failed; A\'s rate %.0f%%'
          % (args.a, args.b, args.fights, wins, losses, draws, failed,
             100.0 * wins / decided if decided else 0.0))
    return 1 if failed else 0


def check(args):
    """The same fight twice on each size: every trace the same."""
    traces = {}
    k = args.first
    stage, f1, f2, cpus, _ = fight_args(k, args.a, args.b, args.stages, args.fighters)
    for bits in (64, 32):
        program = os.path.join(REPO, PROGRAMS[bits])
        if not os.path.exists(program):
            print('port_cpu_bench: no %s (make port)' % program)
            return 1
        out = os.path.join(args.out, 'check%d' % bits)
        os.makedirs(out, exist_ok=True)
        plan, _ = plan_for(program, out, args.limit)
        order = [line.split()[1] for line in open(plan) if line.startswith('var ')]
        for run in (1, 2):
            log_out = os.path.join(out, 'run%d' % run)
            code, winner, frames, trace = run_fight(program, plan, order, log_out, stage, f1, f2, cpus,
                                                    args.seed + k, True)
            with open(os.path.join(log_out, 'cpu_log.txt'), 'rb') as f:
                log = f.read()
            traces[(bits, run)] = (trace, log)
            print('check: %d-bit run %d: exit %d, winner %s, %d trace lines, %d log bytes'
                  % (bits, run, code, winner, trace.count('\n'), len(log)), flush=True)
            if code not in (0, 1) or winner is None:
                print('check: FAILED: the fight did not end with a winner')
                return 1
    first = traces[(64, 1)]
    same = all(t == first for t in traces.values())
    print('check: %s: the fight %s %s %s, CPUs %d,%d, seed %d, on both sizes twice'
          % ('OK' if same else 'FAILED', stage, f1, f2, cpus[0], cpus[1], args.seed + k))
    return 0 if same else 1


if __name__ == '__main__':
    sys.exit(main())
