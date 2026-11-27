#!/usr/bin/env python3
"""A local tournament played on the native port (ROADMAP 31; docs/PORT.md,
"A local tournament"; include/port_tournament.h): whole tournaments run
headless from --tournament to the end, the bracket's result held to what a
bracket is and the record (rebushido-stats.txt, port_stats.h) held to the
result.

    python3 tools/port_tournament.py [--out FOLDER] [--shots FOLDER]
                                     [--only CASE...]

The cases:

  window    the window's program (build/port-sdl/linux/rebushido, `make
            port-sdl`) with no window, sound or dialog (SDL_VIDEODRIVER and
            SDL_AUDIODRIVER dummy), begun as a player begins one: the main
            menu's Tournament (its fifth row), and on its page (the settings
            menu's presses, --menu-keys) eight entrants, double elimination,
            the first entrant made the CPU Master with Kannuki and the
            katana on its own page, Begin, and the bracket's Play; the rest
            eight CPUs of the four levels, the seeds drawn, one fight a
            match, each later match begun 40 steps after the bracket's page
            comes up (--tournament-auto), to the champion. The pages
            pictured (--menu-shots) into --shots (tmp/tournament/ for the
            owner): the tournament's page and the entrant's, and the bracket
            each time it comes up; the log's matches held to the bracket's
            rules, the record's tournament line to the log, and no fight in
            the record (the CPU against the CPU counts none there).
  cpu8      the same tournament on both sizes of the headless port
            (build/port/, build/port32/): the same matches, places and
            record on both (but the dates).
  byes      five CPUs, single elimination, the seeds in order: three byes,
            so four matches, the first between seeds 4 and 5, the top three
            seeds playing no first round.
  person    Player 1, who presses nothing in a fight, and two CPUs (Master
            and Adept, each set to Gengoro with the yari), single
            elimination, in order: the person's bye, the CPUs' semifinal
            straight to its fight (the second side's Gengoro made another
            fighter), then the final at versus mode's select screen: the
            person picks with the first pad, the CPU's side is picked for it
            (Gengoro and the yari, as set), a stage; the CPU wins. The
            record: that one fight, lost, against the CPU's level, the
            tournament's date in it, and the tournament's line.
  rollback  byes' tournament with the rollback self-test (--rollback-test 8
            --rollback-every 24) and run-ahead 2: the same matches, places
            and record as byes'.

In WSL, with the programs `make port` (and for `window`, `make port-sdl`)
builds and the player's disc (REBUSHIDO_RETAIL, or the .cue at the
repository's root). Exit 0 if every check passes."""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))
from port_match import RB_OPENING, RB_START  # noqa: E402
from port_stage_select import HEAD  # noqa: E402

CASES = ['window', 'cpu8', 'byes', 'person', 'rollback']
PROGRAMS = {64: 'build/port/rebushido', 32: 'build/port32/rebushido'}
WINDOW = 'build/port-sdl/linux/rebushido'
SECONDS = 2400
# the tournaments: --tournament's words
EIGHT = 'double,random,one,seed:2026,cpu:4/Kenshin,cpu:3/Musashi,cpu:2/Kojiro,cpu:1/Bokuden,' \
        'cpu:4/Tsukahara,cpu:3/Inshun,cpu:2/Sekishusai,cpu:1/Ittosai'
BYES = 'single,order,one,seed:5,cpu:4,cpu:3,cpu:2,cpu:1,cpu:4'
PERSON = 'single,order,one,seed:9,p1/Trial,cpu:4:gengoro:yari/Shainto,cpu:2:gengoro:yari/Adept'
# the CPUs' words in the record, by the names above
PERSON_LEVELS = {'Shainto': 'master', 'Adept': 'adept'}
AUTO = 40
# enum RosterId, WeaponId: Gengoro, the yari
ROSTER_GENGORO = 18
WEAPON_YARI = 5

# The opening to the main menu (tools/port_match.py), then the tournament
# to its end at the main menu, and a moment of it.
OPENING = RB_START.split('[[checkpoint]]\nname = "menu"')[0]
OVER = '''
[[checkpoint]]
name = "over"
when = "port_tournament_phase == 4 && screen == SCREEN_MENU_UPDATE"
within = 390000
shot = false

[[checkpoint]]
name = "after"
after = 120
shot = false
'''
# The main menu's Tournament: its fifth row (Singleplayer, Versus,
# Training, Online Play, Tournament, Options); then the settings menu's
# presses on its page: eight entrants, double elimination, the first
# entrant the CPU Master (Left from Player 1's pad) with Kannuki and the
# katana, Begin (the fifth row of entrants on: the four settings, the
# eight entrants), and Play on the bracket.
WINDOW_PRESSES = ['"menu+30 DOWN"', '"menu+50 DOWN"', '"menu+70 DOWN"', '"menu+90 DOWN"', '"menu+130 CROSS"']
WINDOW_KEYS = ','.join(['wait:12', 'shot', 'right', 'right', 'right', 'right', 'down', 'right', 'down', 'down',
                        'down', 'left', 'accept', 'wait:12', 'shot', 'down', 'down', 'right', 'down', 'right',
                        'wait:4', 'shot', 'back', 'wait:12', 'shot'] + ['down'] * 8 +
                       ['accept', 'wait:20', 'accept'])
WINDOW_ENTRANTS = 8

# Player 1 in the final: its select screen, the fighter and the weapon
# under the first pad's cursor, a stage; then nothing pressed.
PERSON_PLAN = '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT && screen == SCREEN_MENU_UPDATE"
within = 200000
shot = false
expect = ["port_tournament_phase == 3", "port_tournament_decided == 1", "port_match_rule_page == 0"]

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 3000
shot = false

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
within = 3000
at_frame = true
shot = false
expect = ["mode == FIGHT_MODE_VS", "p1_cpu == 0", "p2_cpu == 1", "port_tournament_side1 == 0",
          "p2_roster == %(roster)d", "p2_weapon == %(weapon)d"]
''' % {'roster': ROSTER_GENGORO, 'weapon': WEAPON_YARI}
PERSON_PRESSES = ['"select+300 CROSS"', '"select+520 CROSS"', '"stages+60 CROSS"']


def toml_text(presses, middle='', start=OPENING):
    head = HEAD.replace('limit = 9000', 'limit = 400000').replace('seconds = 600', 'seconds = %d' % SECONDS)
    return head + 'input = [' + ', '.join(RB_OPENING + presses) + ']\n' + start + middle + OVER


class Report:
    def __init__(self):
        self.failed = 0

    def check(self, ok, text):
        print('%s: %s' % ('ok' if ok else 'FAIL', text), flush=True)
        if not ok:
            self.failed += 1
        return ok


def write_plan(playtest, out, name, text, program):
    """The plan for `program`, whose names it reads."""
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    toml = os.path.join(out, name + '.toml')
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), 1)
    plan = os.path.join(out, name + '-plan.txt')
    with open(plan, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    order = [line.split()[1] for line in lines if line.startswith('var ')]
    return plan, test, order


def run(playtest, program, name, folder, plan, spec, extra=(), window=False):
    """A run of `program` with the tournament `spec`: its exit code, its
    log's text, its trace's, and its record's path."""
    os.makedirs(folder, exist_ok=True)
    stats = os.path.join(folder, 'rebushido-stats.txt')
    if os.path.exists(stats):
        os.remove(stats)
    log = os.path.join(folder, 'log.txt')
    cmd = [program, '--image', playtest.RETAIL_CUE, '--no-cards', '--mods', 'off', '--menu', 'rebushido',
           '--plan', plan, '--out', folder, '--stats', stats, '--tournament-auto', str(AUTO)] + list(extra)
    if spec:
        cmd += ['--tournament', spec]
    env = dict(os.environ)
    if window:
        config = os.path.join(folder, 'rebushido.cfg')
        with open(config, 'w', encoding='utf-8', newline='\n') as f:
            f.write('graphics = original\nfps = off\n')
        cmd += ['--config', config, '--fast', '--log', log]
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    started = time.time()
    with open(os.path.join(folder, 'trace.txt'), 'wb') as o, \
            open(os.path.join(folder, 'output.txt' if window else 'log.txt'), 'wb') as e:
        code = subprocess.run(cmd, cwd=REPO, stdout=o, stderr=e, stdin=subprocess.DEVNULL, env=env,
                              timeout=SECONDS).returncode
    print('   %s: ran in %d s (exit %d)' % (name, time.time() - started, code), flush=True)
    with open(log, encoding='utf-8', errors='replace') as f:
        log_text = f.read()
    with open(os.path.join(folder, 'trace.txt'), encoding='utf-8', errors='replace') as f:
        trace = f.read()
    return code, log_text, trace, stats


# ---- the log and the record ----

MATCH_LINE = re.compile(r'tournament: match (\d+) of (\d+) \(([^,]+(?:, reset)?), (\d+)\): (.+) beat (.+)$')
PLACE_LINE = re.compile(r'tournament: place (\d+): (.+) \((\d+) lost\)$')
OVER_LINE = re.compile(r'tournament: over after (\d+) matches; the champion: (.+)$')


def tournament_of(log):
    """(matches [(round, winner, loser)], places {name: (place, lost)},
    champion, played) of a run's log."""
    matches, places, champion, played = [], {}, None, None
    for line in log.splitlines():
        line = re.sub(r'^\s*[0-9.]+\s+', '', line.strip())
        m = MATCH_LINE.search(line)
        if m:
            matches.append((m.group(3), m.group(5), m.group(6)))
            continue
        m = PLACE_LINE.search(line)
        if m:
            places[m.group(2)] = (int(m.group(1)), int(m.group(3)))
            continue
        m = OVER_LINE.search(line)
        if m:
            played, champion = int(m.group(1)), m.group(2)
    return matches, places, champion, played


def unname(text):
    return re.sub(r'%([0-9A-F]{2})', lambda m: chr(int(m.group(1), 16)), text)


def record_of(path):
    """(version, [fight dicts], [tournament dicts]) of a record."""
    with open(path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    head = lines[0].split()
    fights, tournaments = [], []
    for line in lines[1:]:
        if not line:
            continue
        words = line.split(' ')
        values = dict(w.split('=', 1) for w in words[1:])
        (fights if words[0] == 'fight' else tournaments).append(values)
    return int(head[1]), fights, tournaments


def check_bracket(report, case, entrants, is_double, matches, places, champion, played):
    """What any bracket's result is: its count of matches, everyone's
    losses, one champion, places counting the field."""
    names = list(places)
    report.check(len(names) == entrants, '%s: every entrant has a place (%d)' % (case, len(names)))
    report.check(played == len(matches), '%s: the log says each of the %s matches' % (case, played))
    if is_double:
        report.check(played in (2 * entrants - 2, 2 * entrants - 1),
                     '%s: double elimination of %d in %s matches (2N - 2, or 2N - 1 with a reset)'
                     % (case, entrants, played))
    else:
        report.check(played == entrants - 1, '%s: single elimination of %d in %s matches' % (case, entrants, played))
    losses = dict((name, 0) for name in names)
    for _, winner, loser in matches:
        report.check(winner in losses and loser in losses, '%s: %s beat %s, both entrants' % (case, winner, loser))
        losses[loser] = losses.get(loser, 0) + 1
    allowed = 2 if is_double else 1
    for name in names:
        lost = losses.get(name, 0)
        place, said = places[name]
        report.check(lost == said, '%s: %s lost %d, as the log says' % (case, name, lost))
        if name == champion:
            report.check(place == 1 and lost < allowed, '%s: the champion %s first, %d lost' % (case, name, lost))
        else:
            report.check(place > 1 and lost == allowed, '%s: %s out with %d lost, place %d' % (case, name, lost, place))
    counted = sorted(p for p, _ in places.values())
    report.check(counted.count(1) == 1 and counted.count(2) == 1,
                 '%s: one first and one second place' % case)
    at = 1
    while at <= entrants and counted.count(at):
        at += counted.count(at)
    report.check(at == entrants + 1, '%s: the places count the field: %s' % (case, counted))


def check_record(report, case, stats, matches, places, champion, fights_wanted):
    """The record: version 2, the fights a person played, the tournament's
    line the log's result."""
    if not report.check(os.path.exists(stats), '%s: the record was written' % case):
        return None
    version, fights, tournaments = record_of(stats)
    report.check(version == 2, '%s: the record is version 2' % case)
    report.check(len(fights) == fights_wanted, '%s: %d fights in the record (%d wanted)'
                 % (case, len(fights), fights_wanted))
    if not report.check(len(tournaments) == 1, '%s: one tournament in the record' % case):
        return None
    t = tournaments[0]
    count = int(t['entrants'])
    names = {}
    for i in range(1, count + 1):
        who, place, lost, seed, name = t['e%d' % i].split(':', 4)
        names[i] = unname(name)
        report.check((int(place), int(lost)) == places.get(names[i]),
                     '%s: the record\'s %s, place %s, %s lost, as the log' % (case, names[i], place, lost))
    report.check(names.get(int(t['champion'])) == champion, '%s: the record\'s champion %s' % (case, champion))
    played = int(t['played'])
    report.check(played == len(matches), '%s: the record\'s %d matches' % (case, played))
    for k in range(1, played + 1):
        part, round_, first, second, winner = t['m%d' % k].split(':')
        log_winner, log_loser = matches[k - 1][1], matches[k - 1][2]
        report.check(names[int(winner)] == log_winner and
                     {names[int(first)], names[int(second)]} == {log_winner, log_loser},
                     '%s: the record\'s match %d: %s beat %s' % (case, k, log_winner, log_loser))
    for fight in fights:
        report.check(fight.get('tournament') == t['begun'], '%s: the fight is the tournament\'s (%s)'
                     % (case, fight.get('tournament')))
    return t, fights


def check_checkpoints(report, case, test, order, trace, playtest):
    reached = {}
    for line in trace.splitlines():
        word = line.split()
        if len(word) > 3 and word[0] == 'cp' and word[1] != '(failure)':
            reached[word[1]] = dict(zip(order, (int(v) for v in word[3:])))
    for cp in test.checkpoints:
        if not report.check(cp.name in reached, '%s: the checkpoint `%s` reached' % (case, cp.name)):
            continue
        values = dict(playtest.header_names())
        values.update(reached[cp.name])
        for claim in cp.expect:
            report.check(playtest.evaluate(claim, values), '%s: at `%s`, %s' % (case, cp.name, claim))


def strip_dates(text):
    return re.sub(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d', 'DATE', text)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'tests', 'port_tournament'))
    parser.add_argument('--shots', default=os.path.join(REPO, 'tmp', 'tournament'),
                        help="where the window's pictures of the bracket go")
    parser.add_argument('--only', nargs='*', choices=CASES)
    args = parser.parse_args(argv[1:])
    import playtest
    report = Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    cases = args.only or CASES
    results = {}

    for case in cases:
        folder = os.path.join(out, case)
        if os.path.isdir(folder):
            shutil.rmtree(folder)
        os.makedirs(folder)
        print('%s:' % case, flush=True)
        if case == 'window':
            program = os.path.join(REPO, WINDOW)
            if not report.check(os.path.exists(program), 'window: the window\'s program is built (%s)' % program):
                continue
            plan, test, order = write_plan(playtest, folder, case, toml_text(WINDOW_PRESSES, start=RB_START),
                                           program)
            shots = os.path.abspath(args.shots)
            os.makedirs(shots, exist_ok=True)
            for old in os.listdir(shots):
                if re.fullmatch(r'(bracket|menu)-\d+\.png', old):
                    os.remove(os.path.join(shots, old))
            code, log, trace, stats = run(playtest, program, case, folder, plan, None,
                                          ['--menu-shots', shots, '--menu-keys', WINDOW_KEYS], window=True)
            report.check(code == 0, 'window: the program ran its plan to the end (exit %d)' % code)
            check_checkpoints(report, case, test, order, trace, playtest)
            report.check('settings menu: asked for by Tournament' in log,
                         "window: the main menu's Tournament opened the tournament's page")
            report.check('tournament page: begun, %d entrants' % WINDOW_ENTRANTS in log,
                         'window: its Begin began a tournament of %d' % WINDOW_ENTRANTS)
            matches, places, champion, played = tournament_of(log)
            check_bracket(report, case, WINDOW_ENTRANTS, True, matches, places, champion, played)
            got = check_record(report, case, stats, matches, places, champion, 0)
            if got:
                first = got[0]['e1'].split(':')
                report.check(first[0] == 'master', 'window: the first entrant the CPU Master, as its page set it '
                             '(%s)' % first[0])
            pictures = sorted(p for p in os.listdir(shots) if re.fullmatch(r'bracket-\d+\.png', p))
            report.check(len(pictures) == (played or 0) + 1,
                         'window: a picture of the bracket each time it came up (%d: after Begin, the %s matches): %s'
                         % (len(pictures), played, shots))
            pages = sorted(p for p in os.listdir(shots) if re.fullmatch(r'menu-\d+\.png', p))
            report.check(len(pages) == 4, "window: four pictures of the tournament's and the entrant's pages (%d)"
                         % len(pages))
            continue
        spec = {'cpu8': EIGHT, 'byes': BYES, 'person': PERSON, 'rollback': BYES}[case]
        presses = PERSON_PRESSES if case == 'person' else []
        middle = PERSON_PLAN if case == 'person' else ''
        extra = ['--rollback-test', '8', '--rollback-every', '24', '--run-ahead', '2'] if case == 'rollback' else []
        sizes = (64, 32) if case in ('cpu8', 'byes') else (64,)
        logs = {}
        for bits in sizes:
            program = os.path.join(REPO, PROGRAMS[bits])
            if not report.check(os.path.exists(program), '%s: the %d-bit port is built' % (case, bits)):
                continue
            plan, test, order = write_plan(playtest, os.path.join(folder), '%s%d' % (case, bits),
                                           toml_text(presses, middle), program)
            code, log, trace, stats = run(playtest, program, '%s %d-bit' % (case, bits),
                                          os.path.join(folder, str(bits)), plan, spec, extra)
            report.check(code == 0, '%s: the %d-bit port ran its plan to the end (exit %d)' % (case, bits, code))
            check_checkpoints(report, '%s %d-bit' % (case, bits), test, order, trace, playtest)
            matches, places, champion, played = tournament_of(log)
            entrants = len(spec.split(',')) - 4
            check_bracket(report, '%s %d-bit' % (case, bits), entrants, spec.startswith('double'), matches,
                          places, champion, played)
            got = check_record(report, '%s %d-bit' % (case, bits), stats, matches, places, champion,
                               1 if case == 'person' else 0)
            logs[bits] = (matches, places, champion, strip_dates(open(stats).read()) if got else None)
            if case == 'byes' and bits == 64:
                report.check(matches and matches[0][1:] in (('CPU 4', 'CPU 5'), ('CPU 5', 'CPU 4')),
                             'byes: the first match is the fourth seed\'s and the fifth\'s: %s' % (matches[:1],))
                for top in ('CPU 1', 'CPU 2', 'CPU 3'):
                    first = [m for m in matches if top in m[1:]][:1]
                    report.check(first and first[0][0] == 'Semifinal',
                                 'byes: %s played no first round (its first: %s)' % (top, first))
            if case == 'person' and got:
                t, fights = got
                level = PERSON_LEVELS.get(champion)
                report.check(fights and fights[0]['result'] == 'lost' and fights[0]['cpu'] == level and
                             fights[0]['mode'] == 'vs',
                             'person: the fight in the record: lost against the CPU %s (%s) in versus (%s)'
                             % (level, champion,
                                {k: fights[0].get(k) for k in ('result', 'cpu', 'mode')} if fights else None))
                report.check(places.get('Trial', (0,))[0] == 2 and champion in PERSON_LEVELS,
                             'person: Trial second, a CPU (%s) the champion' % champion)
                report.check(matches and matches[0][1:] in (('Shainto', 'Adept'), ('Adept', 'Shainto')),
                             'person: the CPUs\' semifinal first, Trial\'s bye')
        results[case] = logs
        if len(logs) == 2:
            report.check(logs[64] == logs[32], '%s: both sizes played the same tournament and kept the same record'
                         % case)
    if 'byes' in results and 'rollback' in results and 64 in results['byes'] and 64 in results['rollback']:
        report.check(results['byes'][64] == results['rollback'][64],
                     'rollback: the same tournament and record as without the rollbacks and run-ahead')
    print('%s: port_tournament: %d checks failed' % ('FAIL' if report.failed else 'OK', report.failed))
    return 1 if report.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
