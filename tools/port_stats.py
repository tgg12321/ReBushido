#!/usr/bin/env python3
"""The player's record of fights on the native port (ROADMAP 30; docs/PORT.md,
"Stats"): the record file read, and headless runs by plans whose record is
held to what the fights were.

    python3 tools/port_stats.py --print FILE
        the record's fights, a line each, and its totals

    python3 tools/port_stats.py [--out FOLDER] [--only CASE...]
        the cases, on both sizes of the port (build/port/, build/port32/),
        each size with a record of its own (--stats), the two records the
        same fight for fight (but the dates):

  kill       versus mode, a Stock match of two lives (tools/port_match.py's
             vs_stock): two fights, each won by a kill, one match won; the
             second fight's start, after the match, is not a fight
             finished and is not counted. With --replays: each fight's
             replay named in its line, and its file there.
  surrender  a versus fight in which the second fighter gives up (Select)
             and the first strikes: one fight won by a surrender (the
             kaishaku's scene played to its end).
  rollback   kill's plan with the rollback self-test (--rollback-test 8
             --rollback-every 24: the game's state put back and every frame
             run again) and run-ahead 2: the same two fights, counted once.
  replay     each of kill's replays played back (--replay-check) with the
             record kill made: nothing counted, the file the same.

In WSL, with the programs `make port` builds and the player's disc (as
tools/port_stage_select.py finds it: REBUSHIDO_RETAIL, or the .cue at the
repository's root). Exit 0 if every check passes."""
import argparse
import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

HEAD_WORD = 'rebushido-stats'
VERSION = 1
CASES = ['kill', 'surrender', 'rollback', 'replay']
PROGRAMS = {64: 'build/port/rebushido', 32: 'build/port32/rebushido'}
SECONDS = 1800


def read_record(path):
    """(version, [fight dicts]) of a record file; the values are strings."""
    with open(path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    head = lines[0].split()
    if len(head) != 2 or head[0] != HEAD_WORD:
        raise ValueError('%s: not a record of fights' % path)
    fights = []
    for line in lines[1:]:
        if not line:
            continue
        words = line.split(' ')
        if words[0] != 'fight':
            raise ValueError('%s: a line that is not a fight: %s' % (path, line))
        fights.append(dict(w.split('=', 1) for w in words[1:]))
    return int(head[1]), fights


def print_record(path):
    version, fights = read_record(path)
    print('%s: version %d, %d fights' % (path, version, len(fights)))
    won = sum(1 for f in fights if f['result'] == 'won')
    lost = sum(1 for f in fights if f['result'] == 'lost')
    for f in fights:
        print('  %s %-6s %-9s %-5s by %-9s match %s.%s (%s) kills %s given %s taken %s %s frames%s'
              % (f['date'], f['mode'], f['online'], f['result'], f['how'], f['match'], f['match_fight'],
                 f['match_result'], f['kills'], f['given'], f['taken'], f['frames'],
                 ' replay ' + f['replay'] if f.get('replay') else ''))
    print('  won %d, lost %d, drawn %d' % (won, lost, len(fights) - won - lost))
    return 0


def without_dates(fights):
    return [{k: v for k, v in f.items() if k not in ('date', 'replay')} for f in fights]


# ---- the plans ----

def plans():
    """name -> (plan text, the program's arguments)."""
    import port_match
    from port_stage_select import HEAD, OPENING, START, fight_presses
    values = dict(port_match.VALUES, opening=', '.join(OPENING), presses=fight_presses(),
                  presses_crew=fight_presses(port_match.CREW_ROUNDS),
                  presses_stock=fight_presses(port_match.STOCK_ROUNDS),
                  group_menu=port_match.group_menu(), group_presses=port_match.group_presses(),
                  **port_match.rb_values())
    kill = port_match.TOML_VS_STOCK % values
    # versus, no rule: the second fighter kneels (Select) at once, the first
    # walks in and strikes, and the kaishaku's scene is played to its end
    presses = ', '.join(['"fight+40f P2 SELECT"'] + [
        '"fight+%df %s"' % (90 + n * 60 + offset, button)
        for n in range(20) for offset, button in [(0, 'RIGHT hold 20f'), (25, 'CIRCLE'), (40, 'CIRCLE')]])
    surrender = ('about = "Versus mode, the second fighter gives up (tools/port_stats.py)"\n' + HEAD +
                 'input = [%s,\n    %s,\n    "stages+40 CROSS",\n    %s,\n]\n'
                 % (values['opening'], values['menu'], presses) + START + port_match.SELECT + '''
[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
shot = false

[[checkpoint]]
name = "kaishaku"
when = "screen == SCREEN_KAISHAKU_UPDATE"
within = 3000
shot = false

[[checkpoint]]
name = "after"
when = "screen == SCREEN_AFTER_MATCH_UPDATE"
within = 3000
shot = false
''')
    return {'kill': (kill, ['--mods', 'off', '--match', 'stock:2']),
            'surrender': (surrender, ['--mods', 'off'])}


def write_plan(playtest, out, name, text, bits):
    """The plan for the program of `bits`, whose names it reads."""
    program = os.path.join(REPO, PROGRAMS[bits])
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    toml = os.path.join(out, name + '.toml')
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), 1)
    plan = os.path.join(out, '%s-plan%d.txt' % (name, bits))
    with open(plan, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return plan


def run(playtest, program, args, folder, name):
    """The program's exit code; its output in FOLDER/NAME.out and .err."""
    cmd = [os.path.join(REPO, program), '--image', playtest.RETAIL_CUE, '--no-cards'] + args
    with open(os.path.join(folder, name + '.out'), 'wb') as o, \
            open(os.path.join(folder, name + '.err'), 'wb') as e:
        return subprocess.run(cmd, cwd=REPO, stdout=o, stderr=e, stdin=subprocess.DEVNULL,
                              timeout=SECONDS).returncode


class Report:
    def __init__(self):
        self.failed = 0

    def check(self, ok, text):
        print('%s: %s' % ('ok' if ok else 'FAIL', text), flush=True)
        if not ok:
            self.failed += 1
        return ok


def run_case(playtest, report, out, case, made, plan_texts):
    """A case on both sizes: {bits: fights} of their records."""
    plan_name = 'kill' if case in ('rollback', 'replay') else case
    text, extra = plan_texts[plan_name]
    records = {}
    for bits, program in sorted(PROGRAMS.items()):
        if not report.check(os.path.exists(os.path.join(REPO, program)), 'the %d-bit port is built' % bits):
            return None
        folder = os.path.join(out, '%s%d' % (case, bits))
        if os.path.isdir(folder):
            shutil.rmtree(folder)
        os.makedirs(folder)
        stats = os.path.join(folder, 'stats.txt')
        plan = write_plan(playtest, out, case, text, bits)
        if case == 'replay':
            # the record kill made, and its replays played back with it
            shutil.copy(made[bits]['stats'], stats)
            before = open(stats, encoding='utf-8').read()
            for replay in made[bits]['replays']:
                code = run(playtest, program, ['--replay-check', replay, '--stats', stats], folder,
                           os.path.basename(replay))
                report.check(code == 0, 'replay: %d-bit: %s plays back, every hash the same (exit %d)'
                             % (bits, os.path.basename(replay), code))
            report.check(open(stats, encoding='utf-8').read() == before,
                         'replay: %d-bit: the record is as it was: a replay counts nothing' % bits)
            records[bits] = read_record(stats)[1]
            continue
        args = ['--plan', plan, '--out', os.path.join(folder, 'run'), '--stats', stats] + extra
        replays = os.path.join(folder, 'replays')
        if case == 'kill':
            args += ['--replays', replays]
        if case == 'rollback':
            args += ['--rollback-test', '8', '--rollback-every', '24', '--run-ahead', '2']
        os.makedirs(os.path.join(folder, 'run'))
        code = run(playtest, program, args, folder, 'run')
        report.check(code == 0, '%s: %d-bit: the plan ran to its end (exit %d; %s)'
                     % (case, bits, code, os.path.join(folder, 'run.err')))
        if not report.check(os.path.exists(stats), '%s: %d-bit: the record was written' % (case, bits)):
            return None
        version, fights = read_record(stats)
        report.check(version == VERSION, '%s: %d-bit: the record is of version %d' % (case, bits, VERSION))
        report.check(not os.path.exists(stats + '.part'), '%s: %d-bit: no part of a record is left' % (case, bits))
        records[bits] = fights
        if case == 'kill':
            files = sorted(os.path.join(replays, f) for f in os.listdir(replays)) if os.path.isdir(replays) else []
            made[bits] = {'stats': stats, 'replays': files}
            names = [f.get('replay', '') for f in fights]
            report.check(len(files) == 2 and sorted(names) == sorted(os.path.basename(f) for f in files),
                         'kill: %d-bit: each fight has its replay named (%s; in the folder %s)'
                         % (bits, names, [os.path.basename(f) for f in files]))
    if len(records) == 2:
        report.check(without_dates(records[64]) == without_dates(records[32]),
                     '%s: both sizes kept the same record, %d fights' % (case, len(records[64])))
    return records


def check_kill(report, case, fights):
    if not report.check(len(fights) == 2, '%s: two fights counted, once each (%d)' % (case, len(fights))):
        return
    for n, f in enumerate(fights):
        report.check(f['mode'] == 'vs' and f['online'] == 'local' and f['cpu'] == 'person' and f['side'] == '0',
                     '%s: fight %d: versus, local, against a person, the first side' % (case, n + 1))
        report.check(f['result'] == 'won' and f['how'] == 'kill' and f['kills'] == '1:0',
                     '%s: fight %d: won by a kill (%s by %s, kills %s)' % (case, n + 1, f['result'], f['how'],
                                                                         f['kills']))
        blow, place = f['given'].split(':')[:2]
        report.check(blow != 'none' and place != 'none', '%s: fight %d: the blow %s, to the %s'
                     % (case, n + 1, blow, place))
        report.check(f['taken'].startswith('none:'), '%s: fight %d: no blow taken' % (case, n + 1))
        report.check(f['rule'] == 'stock' and f['count'] == '2' and f['match_fight'] == str(n + 1),
                     '%s: fight %d: of a Stock match of 2, its fight %s' % (case, n + 1, f['match_fight']))
        report.check(int(f['frames']) > 30, '%s: fight %d: %s frames long' % (case, n + 1, f['frames']))
    report.check(fights[0]['match'] == fights[1]['match'], '%s: the two fights are one match' % case)
    report.check(fights[0]['match_result'] == 'going' and fights[1]['match_result'] == 'won',
                 '%s: the match won by its second fight' % case)


def check_surrender(report, fights):
    if not report.check(len(fights) == 1, 'surrender: one fight counted (%d)' % len(fights)):
        return
    f = fights[0]
    report.check(f['how'] == 'surrender' and f['result'] == 'won' and f['match_result'] == 'won',
                 'surrender: won by a surrender (%s by %s)' % (f['result'], f['how']))
    report.check(f['rule'] == 'single', 'surrender: a single fight')


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--print', metavar='FILE', help='print a record and end')
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'tests', 'port_stats'))
    parser.add_argument('--only', nargs='*', choices=CASES)
    args = parser.parse_args(argv[1:])
    if args.print:
        return print_record(args.print)
    import playtest
    report = Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    cases = args.only or CASES
    if 'replay' in cases and 'kill' not in cases:
        cases = ['kill'] + cases
    texts = plans()
    made = {}
    records = {}
    for case in cases:
        got = run_case(playtest, report, out, case, made, texts)
        if got is None:
            break
        records[case] = got
        fights = got[64]
        if case in ('kill', 'rollback', 'replay'):
            check_kill(report, case, fights)
        elif case == 'surrender':
            check_surrender(report, fights)
    if 'kill' in records and 'rollback' in records:
        report.check(without_dates(records['kill'][64]) == without_dates(records['rollback'][64]),
                     'rollback: the record is the plain run\'s: nothing counted twice')
    print('%s: port_stats: %d checks failed' % ('FAIL' if report.failed else 'OK', report.failed))
    return 1 if report.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
