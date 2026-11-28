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
  resume    four CPUs, double elimination, kept in a file (--tournament-
            file): played through once; then again, the program ended 300
            frames into the fight of the fourth match (--tournament-quit-in
            3), the file holding the three decided, and a third run begun
            from the file alone (--tournament-resume): the fourth match
            played again from its start, and every match, its sides, the
            champion and the record (but the dates) those of the run not
            stopped; the file taken away when it is over.
  windowresume  resume's tournament stopped in the headless port, then
            resumed in the window's program as a player resumes one: the
            main menu's Tournament, its page's Resume row (above the
            entrants, the page pictured into --shots' resume/), the
            bracket's Play: the same champion as resume's run not stopped.
  crew      four CPUs, single elimination, Crew 3 (crew:3): each match of
            two CPUs straight to its fights, each crew three different
            characters, drawn place by place, the first the entrant's own
            where it is set; to the champion.
  crewdraw  two CPUs, Crew 2: the first two fights made draws (the plan
            writes the first fighter's fall once the second has fallen, a
            `poke`), so that both crews are on their last fighter and then
            run out together: nobody out, the two fight again (sudden
            death), and the match is decided by a fight won.
  crewperson  Player 1 and the CPU (Gengoro and the yari), Crew 2, in
            versus mode's select screen: the player's two places picked at
            the grid with his pad, the CPU's crew picked for it place by
            place (Gengoro and the yari first, a different character
            second), a stage, and the fights.

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

CASES = ['window', 'cpu8', 'byes', 'person', 'rollback', 'resume', 'windowresume', 'crew', 'crewdraw', 'crewperson']
PROGRAMS = {64: 'build/port/rebushido', 32: 'build/port32/rebushido'}
WINDOW = 'build/port-sdl/linux/rebushido'
SECONDS = 2400
# the tournaments: --tournament's words
EIGHT = 'double,random,one,seed:2026,cpu:4/Kenshin,cpu:3/Musashi,cpu:2/Kojiro,cpu:1/Bokuden,' \
        'cpu:4/Tsukahara,cpu:3/Inshun,cpu:2/Sekishusai,cpu:1/Ittosai'
BYES = 'single,order,one,seed:5,cpu:4,cpu:3,cpu:2,cpu:1,cpu:4'
PERSON = 'single,order,one,seed:9,p1/Trial,cpu:4:gengoro:yari/Shainto,cpu:2:gengoro:yari/Adept'
RESUME = 'double,random,one,seed:31,cpu:4/Kenshin,cpu:3/Musashi,cpu:2/Kojiro,cpu:1/Bokuden'
RESUME_QUIT_IN = 3
CREW = 'single,order,crew:3,seed:12,cpu:4:kannuki:katana/Kenshin,cpu:3/Musashi,cpu:2/Kojiro,cpu:1/Bokuden'
CREW_DRAW = 'single,order,crew:2,seed:4,cpu:4/Kenshin,cpu:3/Musashi'
CREW_PERSON = 'single,order,crew:2,seed:9,p1/Trial,cpu:4:gengoro:yari/Shainto'
SPECS = {'cpu8': EIGHT, 'byes': BYES, 'person': PERSON, 'rollback': BYES, 'crew': CREW, 'crewdraw': CREW_DRAW,
         'crewperson': CREW_PERSON}
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

# Crew 2 of two CPUs: the first fight and the second each a draw (the
# first fighter written fallen once the second has: a `poke`), the
# second's leaving both crews out at once, which in a tournament takes
# nobody out: the third fight decides.
CREW_DRAW_PLAN = '''
[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE && port_tournament_phase == 3"
within = 200000
shot = false
expect = ["port_match_rule == 3", "port_match_count == 2", "port_match_lives_p1 == 2", "port_match_lives_p2 == 2"]

[[checkpoint]]
name = "down"
when = "p1_frames_alive >= 90"
within = 9000
shot = false

[[checkpoint]]
name = "card"
when = "port_match_card == 1"
within = 3000
shot = false
expect = ["port_match_last == 2", "port_match_lives_p1 == 1", "port_match_lives_p2 == 1", "port_match_over == 0"]

[[checkpoint]]
name = "fight2"
when = "screen == SCREEN_FIGHT_UPDATE && port_match_card == 0 && p1_defeated == 0 && p2_defeated == 0"
within = 3000
shot = false

[[checkpoint]]
name = "down2"
when = "p1_frames_alive >= 90"
within = 9000
shot = false

[[checkpoint]]
name = "card2"
when = "port_match_card == 1"
within = 3000
shot = false
expect = ["port_match_last == 2", "port_match_fights == 2", "port_match_lives_p1 == 1", "port_match_lives_p2 == 1",
          "port_match_over == 0"]

[[checkpoint]]
name = "fight3"
when = "screen == SCREEN_FIGHT_UPDATE && port_match_card == 0 && p1_defeated == 0 && p2_defeated == 0"
within = 3000
shot = false
'''
CREW_DRAW_POKES = [('down', 4, 'p1_defeated', 1), ('down', 4, 'p2_defeated', 1),
                   ('down2', 4, 'p1_defeated', 1), ('down2', 4, 'p2_defeated', 1)]

# Crew 2, the player's match: the first place at the grid and the weapon
# row as person's; the second place one down the grid (the first's
# character is his crew's), its weapon; a stage.
CREW_PERSON_PLAN = PERSON_PLAN.split('[[checkpoint]]\nname = "stages"')[0].replace(
    '"port_tournament_decided == 1", ', '"port_tournament_decided == 0", ') + '''[[checkpoint]]
name = "crew2"
when = "port_match_crew_picked == 1"
within = 3000
shot = false
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_page_rule == 3", "port_match_crew_size == 2",
          "port_crew_p2_1 == %(gengoro)d"]

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 3000
shot = false
expect = ["port_match_crew_picked == 2", "port_crew_p2_1 == %(gengoro)d", "port_crew_p2_2 != %(gengoro)d",
          "port_crew_p1_1 != port_crew_p1_2"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
within = 3000
at_frame = true
shot = false
expect = ["mode == FIGHT_MODE_VS", "p1_cpu == 0", "p2_cpu == 1", "port_match_rule == 3", "port_match_count == 2",
          "p2_roster == %(roster)d", "p2_weapon == %(weapon)d"]
''' % {'roster': ROSTER_GENGORO, 'weapon': WEAPON_YARI, 'gengoro': 12}
CREW_PERSON_PRESSES = ['"select+300 CROSS"', '"select+520 CROSS"', '"crew2+60 DOWN"', '"crew2+100 CROSS"',
                       '"crew2+320 CROSS"', '"stages+60 CROSS"']
# the sides of a match as the log says them at its fight
SIDES_LINE = re.compile(r"tournament: match (\d+)'s fight: (.+) \((.*)\) against (.+) \((.*)\)$")


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


def run(playtest, program, name, folder, plan, spec, extra=(), window=False, keep_stats=False):
    """A run of `program` with the tournament `spec`: its exit code, its
    log's text, its trace's, and its record's path."""
    os.makedirs(folder, exist_ok=True)
    stats = os.path.join(folder, 'rebushido-stats.txt')
    if os.path.exists(stats) and not keep_stats:
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


def sides_of(log):
    """[(match, first, [picks], second, [picks])] of a run's log."""
    out = []
    for line in log.splitlines():
        line = re.sub(r'^\s*[0-9.]+\s+', '', line.strip())
        m = SIDES_LINE.search(line)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3).split(), m.group(4), m.group(5).split()))
    return out


def check_crews(report, case, sides, size):
    """Each crew of a match: `size` places, no character twice."""
    report.check(len(sides) > 0, '%s: the log says each match\'s crews (%d)' % (case, len(sides)))
    for number, first, picks1, second, picks2 in sides:
        for name, picks in ((first, picks1), (second, picks2)):
            fighters = [p.split('/')[0] for p in picks]
            report.check(len(picks) == size and len(set(fighters)) == size,
                         '%s: match %d, %s\'s crew of %d different characters: %s'
                         % (case, number, name, size, ' '.join(picks)))


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
    if fights_wanted is not None:
        report.check(len(fights) == fights_wanted, '%s: %d fights in the record (%d wanted)'
                     % (case, len(fights), fights_wanted))
    else:
        report.check(len(fights) >= 1, '%s: the person\'s fights in the record (%d)' % (case, len(fights)))
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


def run_resume(playtest, report, folder):
    """The resume case: a run not stopped, a run stopped in a match, and a
    run resumed from its file."""
    program = os.path.join(REPO, PROGRAMS[64])
    if not report.check(os.path.exists(program), 'resume: the 64-bit port is built'):
        return
    entrants = len(RESUME.split(',')) - 4
    runs = {}
    for part in ('whole', 'stopped', 'resumed'):
        sub = os.path.join(folder, part)
        os.makedirs(sub, exist_ok=True)
        kept = os.path.join(folder, 'tournament-%s.txt' % ('whole' if part == 'whole' else 'kept'))
        text = toml_text([], '')
        extra = ['--tournament-file', kept]
        spec = RESUME
        if part == 'stopped':
            extra += ['--tournament-quit-in', str(RESUME_QUIT_IN)]
        elif part == 'resumed':
            if not os.path.exists(kept):
                return
            extra += ['--tournament-resume']
            spec = None
        plan, test, order = write_plan(playtest, folder, 'resume-' + part, text, program)
        code, log, trace, stats = run(playtest, program, 'resume ' + part, sub, plan, spec, extra)
        runs[part] = (code, log, trace, stats, kept)
        if part == 'stopped':
            report.check('the program ends in match %d\'s fight' % (RESUME_QUIT_IN + 1) in log,
                         'resume: the stopped run ended in the fight of match %d' % (RESUME_QUIT_IN + 1))
            report.check(os.path.exists(kept), 'resume: the tournament is kept in its file (%s)' % kept)
            if os.path.exists(kept):
                with open(kept, encoding='utf-8') as f:
                    kept_text = f.read()
                shutil.copy(kept, os.path.join(sub, 'kept-as-stopped.txt'))
                results = [line for line in kept_text.splitlines() if line.startswith('result=')]
                report.check(kept_text.startswith('rebushido-tournament 1\n') and len(results) == RESUME_QUIT_IN,
                             'resume: the file has the %d matches decided (%d), not the one stopped'
                             % (RESUME_QUIT_IN, len(results)))
            report.check(len(tournament_of(log)[0]) == RESUME_QUIT_IN,
                         'resume: the stopped run decided %d matches' % RESUME_QUIT_IN)
            continue
        report.check(code == 0, 'resume %s: the port ran its plan to the end (exit %d)' % (part, code))
        check_checkpoints(report, 'resume ' + part, test, order, trace, playtest)
        matches, places, champion, played = tournament_of(log)
        check_bracket(report, 'resume ' + part, entrants, True, matches, places, champion, played)
        check_record(report, 'resume ' + part, stats, matches, places, champion, 0)
        report.check(not os.path.exists(kept), 'resume %s: the file taken away when it was over' % part)
        if part == 'resumed':
            report.check('tournament: resumed, %d entrants' % entrants in log,
                         'resume: the third run resumed the tournament kept')
    if len(runs) != 3:
        return
    whole, resumed = runs['whole'][1], runs['resumed'][1]
    report.check(tournament_of(whole)[:3] == tournament_of(resumed)[:3],
                 'resume: the same matches, places and champion as the run not stopped (%s)'
                 % tournament_of(resumed)[2])
    stopped_sides = sides_of(runs['stopped'][1])
    report.check(sides_of(whole)[RESUME_QUIT_IN:] == sides_of(resumed) and
                 len(sides_of(whole)) == len(tournament_of(whole)[0]),
                 'resume: each match after the resume with the sides of the run not stopped')
    report.check(stopped_sides and stopped_sides[-1][0] == RESUME_QUIT_IN + 1 and
                 stopped_sides[-1] in sides_of(resumed),
                 'resume: the match stopped (%d) played again from its start, as it began'
                 % (RESUME_QUIT_IN + 1))
    if os.path.exists(runs['whole'][3]) and os.path.exists(runs['resumed'][3]):
        report.check(strip_dates(open(runs['whole'][3]).read()) == strip_dates(open(runs['resumed'][3]).read()),
                     'resume: the record\'s tournament the same as the run not stopped\'s (but the dates)')


# The window's presses to resume: the Tournament page's four settings,
# then Resume; on the bracket, Play.
RESUME_KEYS = ','.join(['wait:12', 'shot', 'down', 'down', 'down', 'down', 'wait:4', 'shot', 'accept', 'wait:20',
                        'shot', 'accept'])


def run_window_resume(playtest, report, folder, shots):
    """windowresume: stopped headless, resumed in the window's program."""
    program = os.path.join(REPO, PROGRAMS[64])
    window = os.path.join(REPO, WINDOW)
    if not (report.check(os.path.exists(program), 'windowresume: the 64-bit port is built') and
            report.check(os.path.exists(window), 'windowresume: the window\'s program is built')):
        return
    kept = os.path.join(folder, 'tournament-kept.txt')
    plan, _, _ = write_plan(playtest, folder, 'stopped', toml_text([], ''), program)
    run(playtest, program, 'windowresume stopped', os.path.join(folder, 'stopped'), plan, RESUME,
        ['--tournament-file', kept, '--tournament-quit-in', str(RESUME_QUIT_IN)])
    if not report.check(os.path.exists(kept), 'windowresume: the tournament kept by the stopped run'):
        return
    os.makedirs(shots, exist_ok=True)
    for old in os.listdir(shots):
        if re.fullmatch(r'(bracket|menu)-\d+\.png', old):
            os.remove(os.path.join(shots, old))
    plan, test, order = write_plan(playtest, folder, 'window', toml_text(WINDOW_PRESSES, start=RB_START), window)
    code, log, trace, stats = run(playtest, window, 'windowresume window', os.path.join(folder, 'window'), plan,
                                  None, ['--tournament-file', kept, '--menu-shots', shots, '--menu-keys',
                                         RESUME_KEYS], window=True)
    report.check(code == 0, 'windowresume: the window\'s program ran its plan to the end (exit %d)' % code)
    check_checkpoints(report, 'windowresume', test, order, trace, playtest)
    report.check('tournament page: resumed, 4 entrants, %d played' % RESUME_QUIT_IN in log,
                 'windowresume: the page\'s Resume resumed the tournament kept')
    matches, places, champion, played = tournament_of(log)
    check_bracket(report, 'windowresume', 4, True, matches, places, champion, played)
    check_record(report, 'windowresume', stats, matches, places, champion, 0)
    report.check(not os.path.exists(kept), 'windowresume: the file taken away when it was over')
    whole = os.path.join(os.path.dirname(folder), 'resume', 'whole', 'log.txt')
    if os.path.exists(whole):
        with open(whole, encoding='utf-8', errors='replace') as f:
            report.check(tournament_of(f.read())[:3] == (matches, places, champion),
                         'windowresume: the matches, places and champion of resume\'s run not stopped (%s)'
                         % champion)
    pages = sorted(p for p in os.listdir(shots) if re.fullmatch(r'menu-\d+\.png', p))
    report.check(len(pages) >= 2, 'windowresume: the Tournament page pictured with its Resume row (%d): %s'
                 % (len(pages), shots))


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
        if case == 'resume':
            run_resume(playtest, report, folder)
            continue
        if case == 'windowresume':
            run_window_resume(playtest, report, folder, os.path.join(os.path.abspath(args.shots), 'resume'))
            continue
        spec = SPECS[case]
        presses = {'person': PERSON_PRESSES, 'crewperson': CREW_PERSON_PRESSES}.get(case, [])
        middle = {'person': PERSON_PLAN, 'crewperson': CREW_PERSON_PLAN, 'crewdraw': CREW_DRAW_PLAN}.get(case, '')
        extra = ['--rollback-test', '8', '--rollback-every', '24', '--run-ahead', '2'] if case == 'rollback' else []
        sizes = (64, 32) if case in ('cpu8', 'byes', 'crew') else (64,)
        logs = {}
        for bits in sizes:
            program = os.path.join(REPO, PROGRAMS[bits])
            if not report.check(os.path.exists(program), '%s: the %d-bit port is built' % (case, bits)):
                continue
            plan, test, order = write_plan(playtest, os.path.join(folder), '%s%d' % (case, bits),
                                           toml_text(presses, middle), program)
            if case == 'crewdraw':
                from port_stage_select import poke_lines
                with open(plan, encoding='utf-8') as f:
                    lines = f.read().split('\n')
                lines = [line for line in lines if line] + poke_lines(playtest, program, lines, CREW_DRAW_POKES)
                with open(plan, 'w', encoding='utf-8', newline='\n') as f:
                    f.write('\n'.join(lines) + '\n')
            code, log, trace, stats = run(playtest, program, '%s %d-bit' % (case, bits),
                                          os.path.join(folder, str(bits)), plan, spec, extra)
            report.check(code == 0, '%s: the %d-bit port ran its plan to the end (exit %d)' % (case, bits, code))
            check_checkpoints(report, '%s %d-bit' % (case, bits), test, order, trace, playtest)
            matches, places, champion, played = tournament_of(log)
            entrants = len(spec.split(',')) - 4
            check_bracket(report, '%s %d-bit' % (case, bits), entrants, spec.startswith('double'), matches,
                          places, champion, played)
            got = check_record(report, '%s %d-bit' % (case, bits), stats, matches, places, champion,
                               {'person': 1}.get(case, 0) if case != 'crewperson' else None)
            if case in ('crew', 'crewdraw', 'crewperson'):
                sides = sides_of(log)
                check_crews(report, '%s %d-bit' % (case, bits), sides, int(spec.split('crew:')[1].split(',')[0]))
                report.check(len(sides) == len(matches), '%s: a crew match for each match decided (%d of %d)'
                             % (case, len(sides), len(matches)))
            if case == 'crew' and bits == 64:
                firsts = [s for s in sides_of(log) if 'Kenshin' in (s[1], s[3])]
                report.check(firsts and all((s[2] if s[1] == 'Kenshin' else s[4])[0] == 'kannuki/katana'
                                            for s in firsts),
                             'crew: Kenshin\'s crew led by his own Kannuki and katana in each of his matches')
            if case == 'crewperson' and got:
                cpu = [s for s in sides_of(log) if s[3] == 'Shainto']
                report.check(cpu and cpu[0][4][0] == 'gengoro/yari' and len(set(p.split('/')[0] for p in cpu[0][4])) == 2,
                             'crewperson: the CPU\'s crew picked for it at the grid, Gengoro and the yari first: %s'
                             % (cpu[0][4] if cpu else None))
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
