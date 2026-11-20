#!/usr/bin/env python3
"""The stage select's three kinds of stage driven on the native port
(docs/PORT.md, "The stage select's kinds of stage"; ROADMAP 16): a plan
of the play tests' kind presses through versus mode's select screen,
switches a stage to Classic and back, opens the arenas' page and fights on
the sample arena, then rematches on a stage of the disc's. Then the two
gameplay switches (docs/PORT.md, "Every stage in every mode, and every
unlock"): versus the CPU with --stage-select and --unlock-all picks a
fighter the save has not won, goes on to the stage pages, switches a
stage to Classic and fights there; training with --stage-select alone
goes through the same pages to a fight on the stage chosen, with the
save's own unlocks; and versus mode with --unlock-all picks the
Narukagami's last fighter and a fighter the save has not won. Both sizes
of the port (build/port/, build/port32/) must print the same trace of
each plan, line for line, and each checkpoint's values must be what the
presses chose.

    python3 tools/port_stage_select.py [--out FOLDER] [--shots FOLDER]
                                       [--only PLAN...]

In WSL, with the programs `make port` builds and the player's disc (as
tools/arena_validate.py --fight finds them). The sample arena
(tools/arenas/courtyard/) is compiled into the run's folder as a mod of
its own. With --shots the checkpoints' pictures are copied there (the
select screen, the switch, the arenas' page, the stage pages of versus
the CPU and of training), each plan's under its own name.

Exit 0 if every check passes, 1 if one fails."""
import argparse
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import arena_compile  # noqa: E402

PROGRAMS = {64: 'build/port/rebushido', 32: 'build/port32/rebushido'}
ARENA_SOURCE = 'tools/arenas/courtyard'
ARENA_HOST = 25
# the stage under the stage pages' first cursor (versus mode opens on the
# second page, its first place): SelectFile.stage_codes
FIRST_STAGE_VALUE = 'stage_cursor'

TOML = '''about = "The stage select's kinds of stage (tools/port_stage_select.py)"
discs = ["port"]
cards = "none"
limit = 9000
seconds = 600
input = [
    "page+30 START",
    "movie+300 START", "movie+600 CROSS", "movie+900 START",
    "menu+30 DOWN", "menu+60 DOWN", "menu+90 DOWN", "menu+150 CROSS",
    "select+280 CROSS", "select+320 P2 CROSS",
    "select+500 CROSS", "select+540 P2 CROSS",
    # the stage pages: the switch on the first place twice (Classic, then
    # back), once on the second place (Classic), and the arenas' page; L1
    # turns the first place's time of day back, from its own to night
    "stages+20 SQUARE",
    "classic+10 L1", "classic+20 SQUARE",
    "again+20 DOWN", "again+40 SQUARE",
    "second+20 SELECT",
    "arenas+20 CROSS",
    # the fight: walk, then the pause menu's way out to the sub menu
    # (blanks: the paused game counts no frames)
    "walk+5 START", "walk+40 DOWN", "walk+70 CROSS",
    # the select screen again: the arena's host itself, the disc's
    # Abandoned Station, fought as the disc has it, so the stage in memory
    # must be loaded again without the arena
    "select2+280 CROSS", "select2+320 P2 CROSS",
    "select2+500 CROSS", "select2+540 P2 CROSS",
    # ... at dawn: R1 once on it
    "stages2+20 DOWN", "stages2+40 RIGHT", "stages2+60 R1", "stages2+80 CROSS",
]
compare = ["screen", "stage", "config_stage", "port_arena", "port_tier", "port_page",
           "port_time"]

[[checkpoint]]
name = "page"
when = "rb_page == RB_PAGE_UP"
within = 2000
shot = false

[[checkpoint]]
name = "movie"
when = "screen == SCREEN_MOVIE"
within = 1000
shot = false

[[checkpoint]]
name = "menu"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_TOP && screen_frames >= 120"
within = 3000
shot = false

[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
expect = ["port_cursor_tier == 1", "port_page == 0"]

[[checkpoint]]
name = "classic"
after = 30
expect = ["port_cursor_tier == 0", "port_cursor_time == 0"]

[[checkpoint]]
name = "again"
after = 30
expect = ["port_cursor_tier == 1", "port_cursor_time == 4"]

[[checkpoint]]
name = "second"
after = 70
expect = ["port_cursor_tier == 0"]

[[checkpoint]]
name = "arenas"
after = 30
expect = ["port_page == 1"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["stage == %(host)d", "port_arena == 0", "port_tier == 1", "port_time == 0"]

[[checkpoint]]
name = "walk"
frames = 40
expect = ["stage == %(host)d", "port_arena == 0"]

[[checkpoint]]
name = "select2"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_SELECT"
within = 3000
shot = false

[[checkpoint]]
name = "stages2"
when = "port_on_stages == 1"
within = 1500
expect = ["port_page == 0"]

[[checkpoint]]
name = "fight2"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["stage == %(host)d", "port_arena == -1", "port_tier == 1", "port_time == 1",
          "p1_x != -2000"]

[[checkpoint]]
name = "walk2"
frames = 40
expect = ["stage == %(host)d", "port_arena == -1", "port_tier == 1"]
'''

# What every plan below shares: the run, and the checkpoints of the options
# page, the opening and the top menu.
HEAD = '''discs = ["port"]
cards = "none"
limit = 9000
seconds = 600
'''
START = '''
[[checkpoint]]
name = "page"
when = "rb_page == RB_PAGE_UP"
within = 2000
shot = false

[[checkpoint]]
name = "movie"
when = "screen == SCREEN_MOVIE"
within = 1000
shot = false

[[checkpoint]]
name = "menu"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_TOP && screen_frames >= 120"
within = 3000
shot = false
'''
OPENING = ['"page+30 START"', '"movie+300 START"', '"movie+600 CROSS"', '"movie+900 START"']
COMPARE = 'compare = ["screen", "stage", "mode", "config_stage", "port_arena", "port_tier", ' \
          '"port_page", "port_page_used"]\n'

# Versus the CPU (the top menu's second row) with --stage-select and
# --unlock-all: Matsumushi, whom an empty save has not won, a weapon, the
# stage pages, where the second page's fourth place (Abandoned Station) is
# switched to Classic and chosen.
TOML_VSCPU = 'about = "Versus the CPU through the stage pages, every unlock (tools/port_stage_select.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    "menu+30 DOWN", "menu+90 CROSS",
    "select+260 RIGHT", "select+300 CROSS", "select+520 CROSS",
    "stages+20 DOWN", "stages+40 RIGHT", "stages+60 SQUARE", "stages+100 CROSS",
]
''' % {'opening': ', '.join(OPENING)} + COMPARE + START + '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
expect = ["port_unlock_all == 1", "port_stage_pages == 1", "port_unlocks == %(all)d"]

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
expect = ["port_page_used == 1", "port_cursor_tier == 1", "port_page == 0"]

[[checkpoint]]
name = "classic"
after = 80
expect = ["port_cursor_tier == 0", "port_on_stages == 1"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["mode == 1", "stage == 25", "config_stage == 25", "p1_character == 9",
          "p2_cpu == 1", "port_tier == 0", "port_arena == -1"]

[[checkpoint]]
name = "walk"
frames = 40
expect = ["stage == 25", "p1_character == 9"]
'''

# Training (the top menu's third row; its page's first row, against the
# ninja) with --stage-select alone: Kannuki, his weapon and the ninja's,
# the stage pages, the second page's second place (Purple Cavern). The
# save's unlocks are an empty save's own. Training against the ninja opens
# on the pause menu, where the game counts no frames: the last checkpoint
# is blanks on.
TOML_PRACTICE = 'about = "Training through the stage pages (tools/port_stage_select.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    "menu+30 DOWN", "menu+60 DOWN", "menu+120 CROSS",
    "practice+60 CROSS",
    "select+280 CROSS", "select+500 CROSS", "select+600 CROSS",
    "stages+20 RIGHT", "stages+60 CROSS",
]
''' % {'opening': ', '.join(OPENING)} + COMPARE + START + '''
[[checkpoint]]
name = "practice"
when = "menu_page == MENU_PAGE_PRACTICE"
within = 1500
shot = false

[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
expect = ["port_unlock_all == 0", "port_stage_pages == 1", "port_unlocks == %(default)d"]

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
expect = ["port_page_used == 1", "port_page == 0"]

[[checkpoint]]
name = "cursor"
after = 40
expect = ["port_on_stages == 1", "port_cursor_tier == 1"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["mode == 2", "stage == 24", "config_stage == 24", "p1_character == 0",
          "port_tier == 1", "port_arena == -1"]

[[checkpoint]]
name = "paused"
after = 40
expect = ["stage == 24", "mode == 2"]
'''

# Versus mode with --unlock-all: the first pad left from Kannuki to the
# Narukagami's crest, their last fighter, which only a chanbara run without
# a continue wins; the second pad right to Utamaru, whom an empty save has
# not won; the second page's first place (Building Rooftop).
TOML_VS_UNLOCKED = 'about = "Versus mode with every unlock (tools/port_stage_select.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    "menu+30 DOWN", "menu+60 DOWN", "menu+90 DOWN", "menu+150 CROSS",
    "select+260 LEFT", "select+270 P2 RIGHT", "select+300 CROSS", "select+340 P2 CROSS",
    "select+520 CROSS", "select+560 P2 CROSS",
    "stages+40 CROSS",
]
''' % {'opening': ', '.join(OPENING)} + COMPARE + START + '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false
expect = ["port_unlock_all == 1", "port_unlocks == %(all)d"]

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false
expect = ["port_page_used == 0"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["mode == 4", "stage == 29", "p1_character == 24", "p2_character == 19"]
'''

# every unlock the save can hold (PORT_PLAY_ALL_UNLOCKS), and an empty
# save's (SAVE_DEFAULT_UNLOCKS)
ALL_UNLOCKS = 0x053FF3FF
DEFAULT_UNLOCKS = 0x7007


class Report:
    def __init__(self):
        self.failed = 0

    def check(self, ok, text):
        print('%s: %s' % ('ok' if ok else 'FAIL', text))
        if not ok:
            self.failed += 1
        return ok


def make_mods(folder):
    """The sample arena as a mod of the run's own."""
    mods = os.path.join(folder, 'mods')
    dest = os.path.join(mods, 'arena-courtyard', 'arenas')
    os.makedirs(dest, exist_ok=True)
    with open(os.path.join(mods, 'arena-courtyard', 'mod.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('name = arena courtyard\nauthor = ReBushido\n')
    with open(os.path.join(dest, 'courtyard.arena'), 'wb') as f:
        f.write(arena_compile.compile_arena(os.path.join(REPO, ARENA_SOURCE), report=lambda text: None))
    return mods


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'stage-select'))
    parser.add_argument('--shots', default=None)
    parser.add_argument('--only', nargs='*', help='the plans to run (stage_select, vscpu, practice, vs_unlocked)')
    args = parser.parse_args(argv[1:])
    import playtest
    report = Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    mods = make_mods(out)
    values = {'host': ARENA_HOST, 'all': ALL_UNLOCKS, 'default': DEFAULT_UNLOCKS}
    # each plan: its name, its test, and what the run is given besides
    plans = [
        ('stage_select', TOML, ['--mods', mods]),
        ('vscpu', TOML_VSCPU, ['--mods', 'off', '--stage-select', '--unlock-all']),
        ('practice', TOML_PRACTICE, ['--mods', 'off', '--stage-select']),
        ('vs_unlocked', TOML_VS_UNLOCKED, ['--mods', 'off', '--unlock-all']),
    ]
    if args.only:
        plans = [plan for plan in plans if plan[0] in args.only]
    for name, text, extra in plans:
        if run_plan(playtest, report, out, name, text % values, extra, args.shots) is None:
            return 1
    return 1 if report.failed else 0


def run_plan(playtest, report, out, name, text, extra, shots):
    """One plan on both sizes of the port: each checkpoint reached and what
    it expects, and the two traces the same. None if a program is not
    built."""
    toml = os.path.join(out, name + '.toml')
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    traces = {}
    for bits, program in sorted(PROGRAMS.items()):
        program = os.path.join(REPO, program)
        if not report.check(os.path.exists(program), 'the %d-bit port is built (%s)' % (bits, program)):
            return None
        playtest.PORT_PROGRAM = program
        playtest.PORT_SYMBOLS = program + '.sym'
        test = playtest.Test(toml)
        disc = playtest.Disc('port')
        test.set_discs([disc])
        names, lines = test.plan(disc, playtest.header_names(), 1)
        prefix = '' if name == 'stage_select' else name + '-'
        plan = os.path.join(out, '%splan%d.txt' % (prefix, bits))
        with open(plan, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        run = os.path.join(out, '%sout%d' % (prefix, bits))
        if os.path.isdir(run):
            shutil.rmtree(run)
        os.makedirs(run)
        cmd = [program, '--image', playtest.RETAIL_CUE] + extra + ['--plan', plan, '--no-cards', '--out', run]
        trace = os.path.join(out, '%strace%d.txt' % (prefix, bits))
        log = os.path.join(out, '%slog%d.txt' % (prefix, bits))
        with open(trace, 'wb') as o, open(log, 'wb') as e:
            code = subprocess.run(cmd, cwd=REPO, stdout=o, stderr=e, stdin=subprocess.DEVNULL,
                                  timeout=1800).returncode
        with open(trace, encoding='utf-8', errors='replace') as f:
            traces[bits] = f.read()
        report.check(code == 0, '%s: the %d-bit port ran the plan to its end (exit %d; %s)'
                     % (name, bits, code, log))
        # each checkpoint reached, and what it expects of its values
        order = [l.split()[1] for l in lines if l.startswith('var ')]
        reached = {}
        for line in traces[bits].splitlines():
            word = line.split()
            if len(word) > 3 and word[0] == 'cp' and word[1] != '(failure)':
                reached[word[1]] = dict(zip(order, (int(v) for v in word[3:])))
        for cp in test.checkpoints:
            if not report.check(cp.name in reached, '%s: %d-bit: the checkpoint `%s` reached' % (name, bits, cp.name)):
                continue
            values = dict(playtest.header_names())
            values.update(reached[cp.name])
            for claim in cp.expect:
                report.check(playtest.evaluate(claim, values),
                             '%s: %d-bit: at `%s`, %s' % (name, bits, cp.name, claim))
    if len(traces) == 2:
        report.check(traces[64] == traces[32],
                     '%s: both sizes printed the same, %d lines' % (name, traces[64].count('\n')))
    if shots:
        os.makedirs(shots, exist_ok=True)
        run = os.path.join(out, ('' if name == 'stage_select' else name + '-') + 'out64')
        for line in traces[64].splitlines():
            word = line.split()
            if len(word) == 5 and word[0] == 'shot':
                if playtest.convert_shot(run, word[1], int(word[2]), int(word[3]), int(word[4])) is not None:
                    shutil.copy(os.path.join(run, word[1] + '.png'),
                                os.path.join(shots, '%s-%s.png' % (name, word[1])))
    return traces

if __name__ == '__main__':
    sys.exit(main(sys.argv))
