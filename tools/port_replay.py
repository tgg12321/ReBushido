#!/usr/bin/env python3
"""The native port's replays held to the fights they were recorded from
(docs/PORT.md, "Replays"; include/port_replay.h), in both sizes of the
program:

    python3 tools/port_replay.py [--out FOLDER] [--only PLAN...]
    python3 tools/port_replay.py --show FILE...     what replays say
    python3 tools/port_replay.py --window           a window's replay on builds whose window differs

Each plan is run on both programs `make port` builds with `--replays`
into a folder of the plan's own: port_game's (power on, versus, a fight to
the kill, the game's own replay, the menu) and the plans of
tools/port_stage_select.py, whose fights are won on the stage select's
kinds of stage, an arena, versus the CPU through two fights, and training.
Every fight is a file. Then every file is played back headlessly by both
programs with `--replay-check`: the program that recorded it must play it
from its start to its end with every state hash the one recorded, and
again from power on (`--replay-from-boot`, its lead-in) with every hash;
the other, a build of another size of pointer and so of another core,
must play it from power on to its end with every console hash the one
recorded where the file has its lead-in, and refuse it as another
build's where it has none (a plan that pokes the game's memory). The two
sizes must have recorded as many fights, each as long and with the same
inputs. What each file holds is printed: its frames, its size and its
start's.

And for port_game's first fight: played from power on with one input
changed (`--replay-differ`, as a game that went otherwise would), it must
stop at the first hash that differs and say that the game went otherwise
there, with exit 1 and no crash; the file with its lead-in taken out (as
a replay of before 2026-11-26 is) must still play on its own build and be
refused by the other.

`--window` records port_game's fight with the window's program (the
Linux one, or from Windows' Python the Windows one) and plays it with two
programs that differ from it in the window alone (`make port-sdl
PADDED=1`: room linked between the game's object and the window's
files): a little room moves the window's code and none of the game's,
and the replay plays with every hash the one recorded; much moves the
game's variables too, and the replay is refused as another build's. A
start holds the game's stack, whose frames return into the game's code:
a build that moved that code took the start and crashed (2026-11-25).

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


def check_replay(program, path, extra, env=None, from_boot=None):
    """`--replay-check` of one file: (exit code, what it said last). With
    `from_boot` a list, whether it was played from power on is appended
    to it."""
    cmd = [program, '--image', playtest.RETAIL_CUE, '--no-cards'] + extra + ['--replay-check', path]
    done = subprocess.run(cmd, cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                          stdin=subprocess.DEVNULL, timeout=SECONDS, env=env)
    said = [line for line in done.stderr.decode('utf-8', 'replace').splitlines() if line.startswith('replay:')]
    if from_boot is not None:
        from_boot.append(any(': from power on (' in line for line in said))
    return done.returncode, said[-1] if said else '(nothing said)'


# The command line's switches a replay's lead-in holds (its setup): the
# other build plays from power on without them, as a player with other
# settings would. The gameplay mods (--mods) stay: they are held to be the
# same, not held.
SETUP_SWITCHES = {'--stage-select': 0, '--unlock-all': 0, '--fight-music': 1, '--match': 1, '--teams': 1,
                  '--time-of-day': 1, '--menu': 1}


def without_setup(extra):
    """`extra` but for the switches a lead-in's setup holds."""
    out, i = [], 0
    while i < len(extra):
        if extra[i] in SETUP_SWITCHES:
            i += 1 + SETUP_SWITCHES[extra[i]]
            continue
        out.append(extra[i])
        i += 1
    return out


def has_lead(path):
    """Whether a replay keeps its run from power on (its lead-in)."""
    return read_replay(path)[1].get('lead') == '1'


def strip_lead(path, out):
    """A copy of a replay without its lead-in, as a file of before it."""
    with open(path, 'rb') as f:
        data = f.read()
    parts, at = [data[:12]], 12
    while at + 8 <= len(data):
        tag, size = data[at:at + 4], struct.unpack('<I', data[at + 4:at + 8])[0]
        if tag == b'META':
            body = data[at + 8:at + 8 + size].replace(b'\nlead 1\n', b'\nlead 0\nlead_why taken out\n')
            parts.append(tag + struct.pack('<I', len(body)) + body)
        elif tag not in (b'LEAD', b'CARD'):
            parts.append(data[at:at + 8 + size])
        at += 8 + size
    with open(out, 'wb') as f:
        f.write(b''.join(parts))
    return out


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


def check_window(report, out):
    """A replay of the window's program played by builds whose window
    alone differs (tools/port_sdl_build.py --padded: room between the
    game's object and the window's files): with a little, every hash the
    one recorded; with much, which moves the game's variables, played from
    power on (its lead-in) to its end, every console hash the one
    recorded, never a crash. Windows' program from Windows, the Linux one
    elsewhere; the three are built first (make port-sdl PADDED=1, through
    tools/build.sh)."""
    windows = os.name == 'nt'
    folder = os.path.join(REPO, 'build', 'port-sdl', 'windows' if windows else 'linux')
    exe = '.exe' if windows else ''
    build = (['wsl'] if windows else []) + ['bash', 'tools/build.sh',
                                            'port-sdl-windows' if windows else 'port-sdl', 'PADDED=1']
    code = subprocess.run(build, cwd=REPO, stdin=subprocess.DEVNULL).returncode
    if not report.check(code == 0, "window: the window's program and the two padded beside it were built (exit %d)"
                        % code):
        return 0
    program = os.path.join(folder, 'rebushido' + exe)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    run = os.path.join(out, 'window')
    replays = os.path.join(run, 'replays')
    if os.path.isdir(run):
        shutil.rmtree(run)
    os.makedirs(replays)
    plan = port_rollback.write_plan(program, 'port_game', run)
    cmd = [program, '--image', playtest.RETAIL_CUE, '--plan', plan, '--no-cards', '--out', run,
           '--replays', replays]
    with open(os.path.join(run, 'rec.out'), 'wb') as o, open(os.path.join(run, 'rec.err'), 'wb') as e:
        code = subprocess.run(cmd, cwd=REPO, stdout=o, stderr=e, stdin=subprocess.DEVNULL, env=env,
                              timeout=SECONDS).returncode
    report.check(code == 0, "window: port_game's plan recorded by the window's program (exit %d)" % code)
    files = sorted(os.path.join(replays, f) for f in os.listdir(replays) if f.endswith('.rbr'))
    report.check(files != [], 'window: fights were recorded (%d files)' % len(files))
    for path in files:
        print('    ' + describe(path), flush=True)
        for name, boot in (('', False), ('-pad-small', False), ('-pad-large', True)):
            from_boot = []
            code, said = check_replay(os.path.join(folder, 'rebushido' + name + exe), path, [], env, from_boot)
            print('    rebushido%s: exit %d%s: %s' % (name, code, ' from power on' if from_boot[0] else '', said),
                  flush=True)
            report.check(code == 0 and 'DESYNC' not in said and from_boot[0] == boot,
                         'window: %s: played by rebushido%s to its end %s, every hash the one recorded'
                         % (os.path.basename(path), name,
                            "from power on (its game's variables lie elsewhere)" if boot else 'from its start'))
    return len(files)


def check_folder(report, name, replays, extra):
    """Every replay of a folder played back by both sizes: by the one that
    recorded it from its start and again from power on, every hash the
    same; by the other from power on, every console hash the same, where
    the file keeps its lead-in, and refused where it does not; and as many
    fights of each size, alike."""
    files = sorted(os.path.join(replays, f) for f in os.listdir(replays) if f.endswith('.rbr'))
    report.check(files != [], '%s: fights were recorded (%d files)' % (name, len(files)))
    by_bits = {64: [], 32: []}
    for path in files:
        print('    ' + describe(path), flush=True)
        lead = has_lead(path)
        base = os.path.basename(path)
        played = []
        for bits, program in sorted(PROGRAMS.items()):
            from_boot = []
            code, said = check_replay(os.path.join(REPO, program), path, extra, from_boot=from_boot)
            if from_boot[0] and without_setup(extra) != extra:
                # ... and without the recording's switches, which its
                # setup holds
                from_boot = []
                code, said = check_replay(os.path.join(REPO, program), path, without_setup(extra),
                                          from_boot=from_boot)
            print('    %d-bit%s: exit %d: %s' % (bits, ' from power on' if from_boot[0] else '', code, said))
            if code == 0 and not from_boot[0]:
                played.append(bits)
                if lead:
                    again, said_again = check_replay(os.path.join(REPO, program), path,
                                                     extra + ['--replay-from-boot'])
                    print('    %d-bit from power on, asked: exit %d: %s' % (bits, again, said_again))
                    report.check(again == 0 and 'DESYNC' not in said_again,
                                 '%s: %s: played from power on by the program that recorded it, every hash the '
                                 'same' % (name, base))
            elif lead:
                report.check(code == 0 and from_boot[0] and 'DESYNC' not in said,
                             "%s: %s: the %d-bit port, another build, plays it from power on to its end, every "
                             "console hash the one recorded" % (name, base, bits))
            else:
                report.check(code == 1 and 'another build' in said,
                             "%s: %s: the %d-bit port refuses it as another build's: it keeps no lead-in (%s)"
                             % (name, base, bits, read_replay(path)[1].get('lead_why')))
        report.check(len(played) == 1 and 'DESYNC' not in said,
                     '%s: %s: played to its end by the program that recorded it, every hash the same'
                     % (name, base))
        if len(played) == 1:
            by_bits[played[0]].append(path)
    report.check(len(by_bits[64]) == len(by_bits[32]),
                 '%s: both sizes recorded as many fights (%d, %d)' % (name, len(by_bits[64]), len(by_bits[32])))
    for first, second in zip(by_bits[64], by_bits[32]):
        a, b = read_replay(first)[0], read_replay(second)[0]
        report.check(a['INPT'] == b['INPT'], '%s: %s and %s: the same inputs'
                     % (name, os.path.basename(first), os.path.basename(second)))
    return len(files)


def check_differ(report, out, replays):
    """port_game's first fight from power on with an input of the run
    changed (--replay-differ: as another game, which went otherwise
    there): the playback stops at the next hash and says so, exit 1, no
    crash. And the file without its lead-in, as one of before it: played
    by its own build, refused by the other."""
    files = sorted(os.path.join(replays, f) for f in os.listdir(replays) if f.endswith('.rbr'))
    if not files or not has_lead(files[0]):
        report.check(False, 'port_game: a fight with its lead-in to change')
        return
    path = files[0]
    start = int(read_replay(path)[1]['start_count'])
    for at, where in ((start // 2, 'before the fight'), (start + 100, 'at frame')):
        code, said = check_replay(os.path.join(REPO, PROGRAMS[64]), path,
                                  ['--replay-from-boot', '--replay-differ', str(at)])
        print('    input %d changed: exit %d: %s' % (at, code, said))
        report.check(code == 1 and 'went otherwise' in said and where in said,
                     'port_game: input %d of the run changed: the playback stops %s, the game gone otherwise, '
                     'exit 1' % (at, where))
    old = strip_lead(path, os.path.join(out, 'port_game', 'without_lead.rbr'))
    results = {}
    for bits, program in sorted(PROGRAMS.items()):
        results[bits] = check_replay(os.path.join(REPO, program), old, [])
        print('    without its lead-in, %d-bit: exit %d: %s' % ((bits,) + results[bits]))
    report.check(sorted(code for code, said in results.values()) == [0, 1] and
                 any('nothing from power on' in said for code, said in results.values()),
                 'port_game: without its lead-in: played by its own build, refused by the other')


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'replays'))
    parser.add_argument('--only', nargs='*', help='the plans: port_game, and those of tools/port_stage_select.py')
    parser.add_argument('--show', nargs='*', help='what these replay files hold, and nothing else')
    parser.add_argument('--window', action='store_true',
                        help="the window's program's replay played by builds whose window differs, and nothing else")
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
    if args.window:
        count = check_window(report, out)
        print('port_replay: window: %d replays, %d checks failed' % (count, report.failed))
        print('%s: port_replay --window' % ('FAILED' if report.failed else 'OK'))
        return 1 if report.failed else 0
    for program in PROGRAMS.values():
        if not report.check(os.path.isfile(os.path.join(REPO, program)), '%s is built (make port)' % program):
            return 1
    wanted = lambda name: not args.only or name in args.only  # noqa: E731
    count = 0
    if wanted('port_game'):
        replays = record_port_game(report, out)
        count += check_folder(report, 'port_game', replays, [])
        check_differ(report, out, replays)
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
                  presses_crew=port_stage_select.fight_presses(port_match.CREW_ROUNDS), **port_match.rb_values())
    plans.append(port_match.CREW_REPLAY_PLAN)
    # and the rule-first Crew and Stock matches whose first fight's winner
    # comes into the second wounded (the pads' presses alone)
    plans += port_match.WOUND_PLANS
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
