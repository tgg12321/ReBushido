#!/usr/bin/env python3
"""The native port's replays held to the fights they were recorded from
(docs/PORT.md, "Replays"; include/port_replay.h), in both sizes of the
program:

    python3 tools/port_replay.py [--out FOLDER] [--only PLAN...]
    python3 tools/port_replay.py --show FILE...     what replays say

Each plan is run on both programs `make port` builds with `--replays`
into a folder of the plan's own: port_game's (power on, versus, a fight to
the kill, the game's own replay, the menu) and the plans of
tools/port_stage_select.py, whose fights are won on the stage select's
kinds of stage, an arena, versus the CPU through two fights, and training.
Every fight is a file. Then every file is played back headlessly by both
programs with `--replay-check`: the program that recorded it must play it
to its end with every state hash the one recorded, and the other must
refuse it as another build's. The two sizes must have recorded as many
fights, each as long and with the same inputs. What each file holds is
printed: its frames, its size and its start's.

An online session's replays, from both machines, are tools/port_net.py's
(`session`): the two files of each fight must have the same inputs and
hashes, and each must play back.

It needs the retail disc's image (REBUSHIDO_RETAIL, or the .cue at the
checkout's root), in WSL; no emulator. Without the image it says so and
passes. Exit 0 if every check passes, 1 if one fails."""
import argparse
import os
import shutil
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

import playtest  # noqa: E402
import port_rollback  # noqa: E402
import port_stage_select  # noqa: E402

PROGRAMS = {64: 'build/port/rebushido', 32: 'build/port32/rebushido'}
SECONDS = 1800
MAGIC = b'RBREPLAY'


def read_replay(path):
    """A replay's chunks ({tag: bytes}) and its META ({key: value})."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != MAGIC:
        raise ValueError('%s is not a replay' % path)
    chunks = {}
    at = 12
    while at + 8 <= len(data):
        tag, size = data[at:at + 4].decode('ascii'), struct.unpack('<I', data[at + 4:at + 8])[0]
        chunks[tag] = data[at + 8:at + 8 + size]
        at += 8 + size
    meta = {}
    for line in chunks.get('META', b'').decode('utf-8', 'replace').splitlines():
        key, _, value = line.partition(' ')
        meta[key] = value
    return chunks, meta


def describe(path):
    """A line of what a replay holds."""
    chunks, meta = read_replay(path)
    start = len(chunks.get('STRT', b''))
    runs = struct.unpack('<I', chunks['DISC'][:4])[0] if 'DISC' in chunks else 0
    return ('%s: %s frames (kill at %s), mode %s, stage %s, winner %s; %d bytes: the start %d, '
            'the inputs %d, the hashes %d, %d runs of the disc'
            % (os.path.basename(path), meta.get('frames'), meta.get('kill'), meta.get('mode'),
               meta.get('stage'), meta.get('winner'), os.path.getsize(path), start,
               len(chunks.get('INPT', b'')), len(chunks.get('HASH', b'')), runs))


def check_replay(program, path, extra):
    """`--replay-check` of one file: (exit code, what it said)."""
    cmd = [program, '--image', playtest.RETAIL_CUE, '--no-cards'] + extra + ['--replay-check', path]
    done = subprocess.run(cmd, cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                          stdin=subprocess.DEVNULL, timeout=SECONDS)
    said = [line for line in done.stderr.decode('utf-8', 'replace').splitlines() if line.startswith('replay:')]
    return done.returncode, said[-1] if said else '(nothing said)'


def record_port_game(report, out):
    """port_game's plan on both sizes, recording: the folder of replays."""
    folder = os.path.join(out, 'port_game')
    replays = os.path.join(folder, 'replays')
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    os.makedirs(replays)
    for bits, program in sorted(PROGRAMS.items()):
        program = os.path.join(REPO, program)
        run = os.path.join(folder, '%d' % bits)
        os.makedirs(run)
        plan = port_rollback.write_plan(program, 'port_game', run)
        cmd = [program, '--image', playtest.RETAIL_CUE, '--plan', plan, '--no-cards', '--out', run,
               '--replays', replays]
        with open(os.path.join(run, 'rec.out'), 'wb') as o, open(os.path.join(run, 'rec.err'), 'wb') as e:
            code = subprocess.run(cmd, cwd=REPO, stdout=o, stderr=e, stdin=subprocess.DEVNULL,
                                  timeout=SECONDS).returncode
        report.check(code == 0, 'port_game: the %d-bit port ran the plan to its end (exit %d)' % (bits, code))
    return replays


def check_folder(report, name, replays, extra):
    """Every replay of a folder played back by both sizes: by the one that
    recorded it with every hash the same, refused by the other; and as
    many fights of each size, alike."""
    files = sorted(os.path.join(replays, f) for f in os.listdir(replays) if f.endswith('.rbr'))
    report.check(files != [], '%s: fights were recorded (%d files)' % (name, len(files)))
    by_bits = {64: [], 32: []}
    for path in files:
        print('    ' + describe(path), flush=True)
        played = []
        for bits, program in sorted(PROGRAMS.items()):
            code, said = check_replay(os.path.join(REPO, program), path, extra)
            if code == 0:
                played.append(bits)
                print('    %d-bit: %s' % (bits, said))
            else:
                report.check('another build' in said,
                             '%s: %s: the %d-bit port refuses it as another build\'s (%s)'
                             % (name, os.path.basename(path), bits, said))
        report.check(len(played) == 1 and 'DESYNC' not in said,
                     '%s: %s: played to its end by the program that recorded it, every hash the same'
                     % (name, os.path.basename(path)))
        if len(played) == 1:
            by_bits[played[0]].append(path)
    report.check(len(by_bits[64]) == len(by_bits[32]),
                 '%s: both sizes recorded as many fights (%d, %d)' % (name, len(by_bits[64]), len(by_bits[32])))
    for first, second in zip(by_bits[64], by_bits[32]):
        a, b = read_replay(first)[0], read_replay(second)[0]
        report.check(a['INPT'] == b['INPT'], '%s: %s and %s: the same inputs'
                     % (name, os.path.basename(first), os.path.basename(second)))
    return len(files)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'replays'))
    parser.add_argument('--only', nargs='*', help='the plans: port_game, and those of tools/port_stage_select.py')
    parser.add_argument('--show', nargs='*', help='what these replay files hold, and nothing else')
    args = parser.parse_args(argv[1:])
    if args.show:
        for path in args.show:
            print(describe(path))
        return 0
    if not os.path.isfile(playtest.RETAIL_CUE):
        print('port_replay: skipped: no %s (your own image of the retail disc; REBUSHIDO_RETAIL names it)'
              % playtest.RETAIL_CUE)
        return 0
    report = port_stage_select.Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    for program in PROGRAMS.values():
        if not report.check(os.path.isfile(os.path.join(REPO, program)), '%s is built (make port)' % program):
            return 1
    wanted = lambda name: not args.only or name in args.only  # noqa: E731
    count = 0
    if wanted('port_game'):
        count += check_folder(report, 'port_game', record_port_game(report, out), [])
    mods = port_stage_select.make_mods(out)
    values = {'host': port_stage_select.ARENA_HOST, 'all': port_stage_select.ALL_UNLOCKS,
              'default': port_stage_select.DEFAULT_UNLOCKS, 'rooftop': port_stage_select.ROOFTOP}
    plans = [
        ('stage_select', port_stage_select.TOML, ['--mods', mods]),
        ('vscpu', port_stage_select.TOML_VSCPU, ['--mods', 'off', '--stage-select', '--unlock-all']),
        ('practice', port_stage_select.TOML_PRACTICE, ['--mods', 'off', '--stage-select']),
        ('vscpu_next', port_stage_select.TOML_VSCPU_NEXT,
         ['--mods', 'off', '--stage-select', '--unlock-all', '--fight-music', 'stage']),
    ]
    # a Crew match of versus (tools/port_match.py): two fights, the second
    # of the winner and the loser's next fighter, loaded afresh (no draw:
    # the plans' draws are written into memory, which no replay holds)
    import port_match
    values.update(port_match.VALUES, opening=', '.join(port_stage_select.OPENING),
                  presses=port_stage_select.fight_presses(),
                  presses_crew=port_stage_select.fight_presses(port_match.CREW_ROUNDS))
    plans.append(port_match.CREW_REPLAY_PLAN)
    for plan in plans:
        name, text, extra = plan[:3]
        pokes = plan[3] if len(plan) > 3 else ()
        if not wanted(name):
            continue
        folder = os.path.join(out, name)
        replays = os.path.join(folder, 'replays')
        if os.path.isdir(folder):
            shutil.rmtree(folder)
        os.makedirs(replays)
        if port_stage_select.run_plan(playtest, report, folder, name, text % values,
                                      extra + ['--replays', replays], None, pokes) is None:
            return 1
        count += check_folder(report, name, replays, extra)
    print('port_replay: %d replays, %d checks failed' % (count, report.failed))
    print('%s: port_replay' % ('FAILED' if report.failed else 'OK'))
    return 1 if report.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
