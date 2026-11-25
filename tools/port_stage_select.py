#!/usr/bin/env python3
"""The stage select's three kinds of stage driven on the native port
(docs/PORT.md, "The stage select's kinds of stage"; ROADMAP 16): a plan
of the play tests' kind presses through versus mode's select screen,
switches a stage to Classic and back, turns its time of day (L1, R1)
and the fight's music (L2, R2), opens the arenas' page and fights on
the sample arena, then rematches on a stage of the disc's. Then the two
gameplay switches (docs/PORT.md, "Every stage in every mode, and every
unlock"): versus the CPU with --stage-select and --unlock-all picks a
fighter the save has not won, goes on to the stage pages, switches a
stage to Classic and fights there; training with --stage-select alone
goes through the same pages to a fight on the stage chosen, with the
save's own unlocks; and versus mode with --unlock-all picks the
Narukagami's last fighter and a fighter the save has not won. Last,
versus the CPU with --fight-music stage through two fights: the stage's
piece over the first, the CPU cut down, and the next opponent's fight
loaded and started (the stream used to loop on and the load wait for
ever; docs/PORT.md, "A fight's music"); and again on the Castle Rooftop,
Tony Umeda against Kannuki and then Utsusemi on the stage kept in memory
(the port stopped there: the rooftop's backdrop was moved by whatever a
register held; docs/PORT.md, "An argument that was not passed"). And
seven arenas, more than the arenas' page's six (three rows of two, as a
stage page's): the d-pad, past the page's sides, and L1 and R1 turn its
pages and the seventh is fought. And the arenas' page in the one-player
modes: versus the CPU and training, each to a fight on the sample arena. Both sizes
of the port (build/port/, build/port32/) must print the same trace of
each plan, line for line, and each checkpoint's values must be what the
presses chose.

    python3 tools/port_stage_select.py [--out FOLDER] [--shots FOLDER]
                                       [--only PLAN...] [--window FOLDER]

In WSL, with the programs `make port` builds and the player's disc (as
tools/arena_validate.py --fight finds them). The sample arena
(tools/arenas/courtyard/) is compiled into the run's folder as a mod of
its own. With --shots the checkpoints' pictures are copied there (the
select screen, the switch, the arenas' page, the stage pages of versus
the CPU and of training), each plan's under its own name.

With --window FOLDER the window's program (build/port-sdl/linux/, make
port-sdl) runs the plans of one arena and of seven again, with no window
shown (SDL's offscreen driver, software OpenGL), and the window's own
pictures at each checkpoint of the stage pages go to FOLDER: the
remastered picture of both, and the original (F2) of the first. The
arenas' page is drawn there alone, over the game's page with its map and
names left out (docs/PORT.md, "The stage select's kinds of stage"): each
picture of the page must differ from the stage page's before it where the
map and the names are. Where there is no window's program it says so.

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
    # turns the first place's time of day back, from its own to night; R2
    # twice takes the fight's music two pieces on, L2 one back, and neither
    # touches the time (L1 and R1)
    "stages+20 SQUARE",
    "classic+4 R2", "classic+10 L1", "classic+16 R2", "classic+20 SQUARE",
    "again+8 L2", "again+20 DOWN", "again+40 SQUARE",
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
           "port_time", "port_music_row"]

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
expect = ["port_cursor_tier == 1", "port_cursor_time == 4", "port_music_row == 2"]

[[checkpoint]]
name = "second"
after = 70
expect = ["port_cursor_tier == 0", "port_music_row == 1"]

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
expect = ["stage == %(host)d", "port_arena == 0", "port_music_row == 1", "port_music_heard == 1"]

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

# The arenas' page in the one-player modes' stage pages: versus the CPU
# (as TOML_VSCPU to the stage pages) and training (as TOML_PRACTICE), each
# with the sample arena as a mod: Select opens the page, Cross fights on
# the courtyard, over its host stage, the game's own page under the
# window's (port_StagePageCovers).
ARENA_PAGE_PRESSES = '"stages+20 SELECT", "stages+60 CROSS"'
ARENA_PAGE_CHECKPOINT = '''[[checkpoint]]
name = "arenas"
after = 40
expect = ["port_on_stages == 1", "port_page == 1", "port_page_cursor == 0"]
'''
TOML_VSCPU_ARENA = TOML_VSCPU.replace(
    'Versus the CPU through the stage pages, every unlock', 'Versus the CPU on an arena from the stage pages').replace(
    '"stages+20 DOWN", "stages+40 RIGHT", "stages+60 SQUARE", "stages+100 CROSS"', ARENA_PAGE_PRESSES).replace(
    '''[[checkpoint]]
name = "classic"
after = 80
expect = ["port_cursor_tier == 0", "port_on_stages == 1"]
''', ARENA_PAGE_CHECKPOINT).replace(
    '"p2_cpu == 1", "port_tier == 0", "port_arena == -1"', '"p2_cpu == 1", "port_tier == 1", "port_arena == 0"')
TOML_PRACTICE_ARENA = TOML_PRACTICE.replace(
    'Training through the stage pages', 'Training on an arena from the stage pages').replace(
    '"stages+20 RIGHT", "stages+60 CROSS"', ARENA_PAGE_PRESSES).replace(
    '''[[checkpoint]]
name = "cursor"
after = 40
expect = ["port_on_stages == 1", "port_cursor_tier == 1"]
''', ARENA_PAGE_CHECKPOINT).replace(
    '"mode == 2", "stage == 24", "config_stage == 24", "p1_character == 0",\n          "port_tier == 1", '
    '"port_arena == -1"',
    '"mode == 2", "stage == 25", "config_stage == 25", "p1_character == 0",\n          "port_tier == 1", '
    '"port_arena == 0"').replace('expect = ["stage == 24", "mode == 2"]', 'expect = ["stage == 25", "mode == 2"]')

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

# Versus the CPU with --fight-music stage, through two fights: the stage's
# own piece plays over the first, where the game plays none, and the CPU
# is cut down; the next opponent's fight must load and start, with its
# own piece (the owner's hang of 2026-11-19: the stream looped on and the
# next fight's load waited for the drive for ever). The first fighter
# walks in and strikes low, twice, again and again until the CPU falls (as
# the port plays today the first opponent falls to it; high strikes lose).
FIGHT_ROUND_FRAMES = 60
FIGHT_ROUNDS = 66
FIGHT_ROUND = [(0, 'RIGHT hold 20f'), (25, 'CIRCLE'), (40, 'CIRCLE')]


def fight_presses(rounds=FIGHT_ROUNDS, every=FIGHT_ROUND_FRAMES, pattern=FIGHT_ROUND):
    """The first fighter's presses through a fight, in its frames."""
    return ', '.join('"fight+%df %s"' % (n * every + offset, button)
                     for n in range(rounds) for offset, button in pattern)


TOML_VSCPU_NEXT = 'about = "Versus the CPU, the stage\'s music, to the next opponent (tools/port_stage_select.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    "menu+30 DOWN", "menu+90 CROSS",
    "select+300 CROSS", "select+520 CROSS",
    "stages+40 CROSS",
    %(presses)s,
]
''' % {'opening': ', '.join(OPENING), 'presses': fight_presses()} + COMPARE.replace('"port_page_used"]', '"port_page_used", "port_music_heard", "winner"]') + START + '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["mode == 1", "port_music_heard == 1", "port_music_track >= 0", "winner == FIGHT_NOBODY"]

[[checkpoint]]
name = "kill"
when = "winner != FIGHT_NOBODY"
within = 6000
expect = ["winner == 0", "p2_defeated != 0", "p1_defeated == 0"]

[[checkpoint]]
name = "next"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
within = 3000
expect = ["mode == 1", "port_music_heard == 1", "port_music_track >= 0",
          "p1_defeated == 0", "p2_defeated == 0"]

[[checkpoint]]
name = "walk"
after = 120
expect = ["screen == SCREEN_FIGHT_UPDATE", "mode == 1"]
'''

# Versus the CPU on the Castle Rooftop, the owner's crash of 2026-11-19:
# Tony Umeda with the long sword cuts Kannuki down, and Utsusemi's fight
# is set up on the stage kept in memory. Loading her model frees
# Kannuki's, the arena moves the stage's block down, and the rooftop's
# backdrop is moved by a distance its call never passes
# (resource_OnStageMoved): on the console the register still holds the
# stage's own, on a PC it held anything and the port stopped in the load
# or drawing the next frame. The fighters, the stage and the order of the
# run are written in (`poke`) rather than picked: from the stage pages,
# for as long as the first fight takes to start, Tony Umeda, his long
# sword, the rooftop and Kannuki with the katana; from the first fight on,
# Utsusemi second in the run (g_vscpu_order). The presses are vscpu_next's.
ROOFTOP = 13
ROOFTOP_POKES = (('stages', 400, 'config_roster_p1', 26), ('stages', 400, 'config_weapon_p1', 1),
                 ('stages', 400, 'config_stage', ROOFTOP), ('stages', 400, 'stage', ROOFTOP),
                 ('stages', 300, 'config_roster_p2', 3), ('stages', 300, 'config_weapon_p2', 0),
                 ('fight', 1500, 'g_vscpu_order+1', 3))
TOML_VSCPU_ROOFTOP = 'about = "Versus the CPU on the rooftop, to the next opponent on the stage kept ' \
    '(tools/port_stage_select.py)"\n' + HEAD + '''input = [%(opening)s,
    "menu+30 DOWN", "menu+90 CROSS",
    "select+300 CROSS", "select+520 CROSS",
    "stages+40 CROSS",
    %(presses)s,
]
''' % {'opening': ', '.join(OPENING), 'presses': fight_presses()} + COMPARE.replace(
    '"port_page_used"]', '"port_page_used", "loaded_stage", "winner"]') + START + '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["mode == 1", "stage == %(rooftop)d", "p1_character == 20", "p1_weapon == 1",
          "p2_character == 0", "p2_weapon == 0", "winner == FIGHT_NOBODY"]

[[checkpoint]]
name = "kill"
when = "winner != FIGHT_NOBODY"
within = 6000
expect = ["winner == 0", "p2_defeated != 0", "p1_defeated == 0"]

[[checkpoint]]
name = "next"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
within = 3000
expect = ["stage == %(rooftop)d", "loaded_stage == %(rooftop)d", "p1_character == 20",
          "p2_character == 3", "p2_weapon == 4", "p1_defeated == 0", "p2_defeated == 0"]

[[checkpoint]]
name = "walk"
after = 120
expect = ["screen == SCREEN_FIGHT_UPDATE", "stage == %(rooftop)d"]
'''

# More arenas than a page's six (MANY_ARENAS of them, the sample under
# other names, each a mod of its own): versus mode's stage pages, Select
# opens the arenas' page (three rows of two), R1 turns to its second page
# (the seventh arena, alone there), Down stays on it, R1 round to the
# first page, Right to the second arena, Down to the fourth, Right past
# the page's side to the next page (its place the second row's first,
# which it has not: its last, the seventh), Left past the side back to
# the first page's first row's right (the second), L1 round to the second
# page again (the seventh), and Cross fights on the seventh arena.
MANY_ARENAS = 7
TOML_MANY = 'about = "The arenas\' page of more than six (tools/port_stage_select.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    "menu+30 DOWN", "menu+60 DOWN", "menu+90 DOWN", "menu+150 CROSS",
    "select+280 CROSS", "select+320 P2 CROSS",
    "select+500 CROSS", "select+540 P2 CROSS",
    "stages+20 SELECT",
    "open+10 R1", "turned+10 DOWN", "down+10 R1", "round+10 RIGHT", "right+10 DOWN",
    "lower+10 RIGHT", "past+10 LEFT", "left+10 L1", "back+10 CROSS",
]
''' % {'opening': ', '.join(OPENING)} + \
    'compare = ["screen", "stage", "config_stage", "port_arena", "port_page", "port_page_cursor", ' \
    '"port_page_top"]\n' + START + '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
expect = ["port_page == 0"]

[[checkpoint]]
name = "open"
after = 30
expect = ["port_page == 1", "port_page_cursor == 0", "port_page_top == 0"]

[[checkpoint]]
name = "turned"
after = 20
expect = ["port_page == 1", "port_page_cursor == 6", "port_page_top == 6"]

[[checkpoint]]
name = "down"
after = 20
expect = ["port_page_cursor == 6", "port_page_top == 6"]

[[checkpoint]]
name = "round"
after = 20
expect = ["port_page_cursor == 0", "port_page_top == 0"]

[[checkpoint]]
name = "right"
after = 20
expect = ["port_page_cursor == 1", "port_page_top == 0"]

[[checkpoint]]
name = "lower"
after = 20
expect = ["port_page_cursor == 3", "port_page_top == 0"]

[[checkpoint]]
name = "past"
after = 20
expect = ["port_page_cursor == 6", "port_page_top == 6"]

[[checkpoint]]
name = "left"
after = 20
expect = ["port_page_cursor == 1", "port_page_top == 0"]

[[checkpoint]]
name = "back"
after = 20
expect = ["port_page == 1", "port_page_cursor == 6", "port_page_top == 6"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["stage == %(host)d", "port_arena == 6", "port_page == 0"]

[[checkpoint]]
name = "walk"
frames = 40
expect = ["stage == %(host)d", "port_arena == 6"]
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


def make_many_mods(folder):
    """MANY_ARENAS arenas, each the sample under a name of its own
    (`Arena 1` ...), each a mod of its own in the order of their folders."""
    mods = os.path.join(folder, 'many-mods')
    data = arena_compile.compile_arena(os.path.join(REPO, ARENA_SOURCE), report=lambda text: None)
    for n in range(1, MANY_ARENAS + 1):
        dest = os.path.join(mods, 'arena-%d' % n, 'arenas')
        os.makedirs(dest, exist_ok=True)
        with open(os.path.join(dest, 'a%d.arena' % n), 'wb') as f:
            f.write(data.replace(b'\nname = Stone courtyard\n', b'\nname = Arena %d\n' % n, 1))
    return mods


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'stage-select'))
    parser.add_argument('--shots', default=None)
    parser.add_argument('--window', default=None, metavar='FOLDER',
                        help="the window's pictures of the stage pages and the arenas' page, into FOLDER")
    parser.add_argument('--window-size', default=WINDOW_SIZE, help='their size (%s)' % WINDOW_SIZE)
    parser.add_argument('--only', nargs='*',
                        help='the plans to run (stage_select, vscpu, practice, vs_unlocked, vscpu_next, vscpu_rooftop, '
                             'many_arenas, vscpu_arena, practice_arena)')
    args = parser.parse_args(argv[1:])
    import playtest
    report = Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    mods = make_mods(out)
    values = {'host': ARENA_HOST, 'all': ALL_UNLOCKS, 'default': DEFAULT_UNLOCKS, 'rooftop': ROOFTOP}
    # each plan: its name, its test, what the run is given besides, and
    # what is written into the game's memory
    plans = [
        ('stage_select', TOML, ['--mods', mods]),
        ('vscpu', TOML_VSCPU, ['--mods', 'off', '--stage-select', '--unlock-all']),
        ('practice', TOML_PRACTICE, ['--mods', 'off', '--stage-select']),
        ('vs_unlocked', TOML_VS_UNLOCKED, ['--mods', 'off', '--unlock-all']),
        ('vscpu_next', TOML_VSCPU_NEXT,
         ['--mods', 'off', '--stage-select', '--unlock-all', '--fight-music', 'stage']),
        ('vscpu_rooftop', TOML_VSCPU_ROOFTOP,
         ['--mods', 'off', '--stage-select', '--unlock-all', '--fight-music', 'stage'], ROOFTOP_POKES),
        ('many_arenas', TOML_MANY, ['--mods', make_many_mods(out)]),
        ('vscpu_arena', TOML_VSCPU_ARENA, ['--mods', mods, '--stage-select', '--unlock-all']),
        ('practice_arena', TOML_PRACTICE_ARENA, ['--mods', mods, '--stage-select']),
    ]
    if args.only:
        plans = [plan for plan in plans if plan[0] in args.only]
    for name, text, extra, *pokes in plans:
        traces = run_plan(playtest, report, out, name, text % values, extra, args.shots, *pokes)
        if traces is None:
            return 1
        if args.window and name in WINDOW_PLANS and 64 in traces:
            window_pictures(playtest, report, out, name, text % values, extra, traces[64], args.window,
                            args.window_size)
    return 1 if report.failed else 0


# The window's pictures (--window): its program, the size of its window,
# each plan's pictures (remastered, and original where said), the stage
# page's checkpoint before the arenas' page and the arenas' page's, and
# how many blanks after a checkpoint its picture is taken (the press
# before it has been drawn).
WINDOW_PROGRAM = 'build/port-sdl/linux/rebushido'
WINDOW_SIZE = '1440x1080'
WINDOW_PLANS = {
    'stage_select': (('remastered', 'original'), 'second', ['arenas']),
    'many_arenas': (('remastered',), 'stages', ['open', 'turned', 'round', 'lower', 'past']),
}
WINDOW_SETTLE = 8
WINDOW_TAP_EVERY = 2
# the mean of a pixel's channels' differences, out of 255, that the
# arenas' page must differ by from the stage page before it in the map's
# frame and on the scroll (the same picture: 0; the page: 20 and more)
WINDOW_DIFFER = 6.0
# the map's frame and the scroll's names, as fractions of the game's 4:3
# area (stage_page.c's MAP_ and NAME_ places, of 640 by 240)
WINDOW_MAP = (144 / 640, 42 / 240, 494 / 640, 112 / 240)
WINDOW_NAMES = (110 / 640, 126 / 240, 530 / 640, 190 / 240)


def window_raw(path):
    """A picture --tap-window kept (sdl_window.c: four words, then the
    window's pixels as ARGB words, a row after a row): (w, h, rgb)."""
    import struct
    with open(path, 'rb') as f:
        data = f.read()
    _, w, h, _ = struct.unpack('<4I', data[:16])
    pixels = data[16:16 + 4 * w * h]
    rgb = bytearray(w * h * 3)
    rgb[0::3] = pixels[2::4]
    rgb[1::3] = pixels[1::4]
    rgb[2::3] = pixels[0::4]
    return w, h, bytes(rgb)


def window_region_difference(a, b, box):
    """The mean difference of two pictures' channels in `box` (fractions
    of the 4:3 area in the middle of the window)."""
    w, h, pa = a
    _, _, pb = b
    area_w = min(w, h * 4 // 3)
    area_h = area_w * 3 // 4
    x0 = (w - area_w) // 2
    y0 = (h - area_h) // 2
    total = 0
    count = 0
    for y in range(y0 + int(box[1] * area_h), y0 + int(box[3] * area_h), 2):
        row = y * w * 3
        for i in range(row + 3 * (x0 + int(box[0] * area_w)), row + 3 * (x0 + int(box[2] * area_w)), 6):
            total += abs(pa[i] - pb[i]) + abs(pa[i + 1] - pb[i + 1]) + abs(pa[i + 2] - pb[i + 2])
            count += 3
    return total / max(count, 1)


def window_pictures(playtest, report, out, name, text, extra, trace, folder, size):
    """The plan `name` again in the window's program, with no window shown,
    and its pictures at the stage pages' checkpoints into `folder`; each of
    the arenas' page held to the stage page's before it."""
    import threading
    import time
    program = os.path.join(REPO, WINDOW_PROGRAM)
    if not os.path.isfile(program):
        print('port_stage_select: --window: skipped: no %s (make port-sdl)' % WINDOW_PROGRAM)
        return
    looks, before, pages = WINDOW_PLANS[name]
    blanks = {}
    for line in trace.splitlines():
        word = line.split()
        if len(word) > 2 and word[0] == 'cp' and word[1] in [before] + pages:
            blanks[int(word[2]) + WINDOW_SETTLE] = word[1]
    if not report.check(len(blanks) == 1 + len(pages), '%s: window: the checkpoints of its pictures reached'
                        % name):
        return
    os.makedirs(folder, exist_ok=True)
    work = os.path.join(out, 'window-' + name)
    os.makedirs(work, exist_ok=True)
    toml = os.path.join(work, 'plan.toml')
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), 1)
    plan = os.path.join(work, 'plan.txt')
    with open(plan, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    for look in looks:
        run = os.path.join(work, look)
        tap = os.path.join(run, 'tap')
        if os.path.isdir(run):
            shutil.rmtree(run)
        os.makedirs(tap)
        config = os.path.join(run, 'rebushido.cfg')
        with open(config, 'w', encoding='utf-8', newline='\n') as f:
            f.write('graphics = %s\nmenu = original\nfullscreen = off\n' % look)
        cmd = [program, '--image', playtest.RETAIL_CUE] + extra + [
            '--plan', plan, '--no-cards', '--fast', '--no-fps', '--window', size, '--config', config,
            '--out', run, '--frames', str(max(blanks) + WINDOW_TAP_EVERY), '--log', os.path.join(run, 'log.txt'),
            '--tap-window', tap, '--tap-every', str(WINDOW_TAP_EVERY)]
        env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy', LIBGL_ALWAYS_SOFTWARE='1')
        wanted = ['window-%06d.raw' % blank for blank in blanks]
        done = threading.Event()

        def sweep():
            # the pictures not wanted, let go as they come
            while True:
                stop = done.wait(0.5)
                for entry in os.listdir(tap):
                    if entry not in wanted:
                        try:
                            os.remove(os.path.join(tap, entry))
                        except OSError:
                            pass
                if stop:
                    return
        sweeper = threading.Thread(target=sweep, daemon=True)
        sweeper.start()
        began = time.time()
        with open(os.path.join(run, 'trace.txt'), 'wb') as o:
            subprocess.run(cmd, cwd=REPO, env=env, stdout=o, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                           timeout=3600)
        done.set()
        sweeper.join()
        pictures = {}
        for blank, checkpoint in sorted(blanks.items()):
            path = os.path.join(tap, 'window-%06d.raw' % blank)
            if not report.check(os.path.isfile(path), '%s: window: %s: the picture at `%s`'
                                % (name, look, checkpoint)):
                continue
            pictures[checkpoint] = window_raw(path)
            w, h, rgb = pictures[checkpoint]
            playtest.write_png(os.path.join(folder, '%s-%s-%s.png' % (name, look, checkpoint)), w, h, rgb)
        print('port_stage_select: %s: window: %s: %d pictures (%.0f s)' % (name, look, len(pictures),
                                                                          time.time() - began), flush=True)
        if before not in pictures:
            continue
        for page in pages:
            if page not in pictures:
                continue
            for what, box in (('the map\'s frame', WINDOW_MAP), ('the scroll\'s names', WINDOW_NAMES)):
                differ = window_region_difference(pictures[before], pictures[page], box)
                report.check(differ >= WINDOW_DIFFER, '%s: window: %s: at `%s`, %s is the arenas\' page\'s, not '
                             'the stage page\'s (%.1f)' % (name, look, page, what, differ))


def poke_lines(playtest, program, lines, pokes):
    """The plan's `poke` lines: (checkpoint, blanks held, name, value)
    each, the name a value the plan reads or a variable of the program's,
    and an offset into it after a `+`; a byte unless the plan reads it
    wider."""
    where = {}
    for line in lines:
        word = line.split()
        if word and word[0] == 'var':
            where[word[1]] = (int(word[2]), int(word[3]))
    symbols = playtest.read_port_symbols(program + '.sym')[0]
    cps = [line.split()[1] for line in lines if line.startswith('cp ')]
    result = []
    for checkpoint, hold, name, value in pokes:
        base, _, offset = name.partition('+')
        address, size = where[base] if base in where else (symbols[base], 1)
        result.append('poke %d 0 %d %d %d %d'
                      % (cps.index(checkpoint) + 1, hold, address + int(offset or 0), size, value))
    return result


def run_plan(playtest, report, out, name, text, extra, shots, pokes=()):
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
        lines += poke_lines(playtest, program, lines, pokes)
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
