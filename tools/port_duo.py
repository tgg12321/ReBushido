#!/usr/bin/env python3
"""Two on two driven on the native port (docs/PORT.md, "Two on two";
ROADMAP 24): a plan of the play tests' kind for each case, on both sizes
of the port (build/port/, build/port32/), whose traces must be the same
line for line and whose checkpoints must hold what the fight made. The
fighters are numbered 1 to 4 as the plans' values have them: 1 and 3 the
first team (the first pad's character twice), 2 and 4 the second.

- retarget: `--fight` Kannuki against Mikado, every fighter a person's
  and only the first played. Fighter 1 targets fighter 2 as the fight
  starts; R2 turns him to fighter 4, whom he walks to and strikes low
  until the blow lands on him (the hit counted against 4, by 1, and none
  on 2). He strikes on: 4 falls, and he is turned to 2 by himself (his
  target down), and strikes 2 down too. The second team out: the first's
  win, and the fight rests on 1 and the last to fall (2) for the game's
  own fall, replay and winner's view, then its menu after a fight.
- pads: the third pad's turn: fighter 3 (P3), who targets 4 as the
  fight starts, turns to 2 with his R2, walks to him and strikes (Cross:
  2's guard beats Circle's blow aside); his blows land on the second
  team's two, who stand side by side: the first on 4, the nearer (a blow
  lands on whichever enemy it meets), the input of the third pad kept by the
  replay.
- menu: ReBushido's main menu (`--menu rebushido`), versus: on the rule
  page Stock (Right), its lives down to 2 (Down, Left), the fighters'
  row (Down) to two on two with the CPU's partners (Right), Cross; each
  side's fighter and weapon picked at the grid, then (the grid again)
  each side's partner, one place down, and his weapon; the stage; the
  fight is two on two (the match's teams the page's) of four characters,
  each partner his own pick, the first player striking, to the score's
  card, the second fight of four, the same four, and the result's card.
- menu_pads: the same with four players (the rule page's row twice
  Right): the partners picked by the third and fourth pads, one place
  down and two, their weapons one along; the first fight of four
  characters, each partner's weapon his pick.
- vscpu: versus the CPU from ReBushido's main menu: the rule page's
  fighters' row to two on two with the CPU's partners, the first player's
  fighter and weapon, his partner one place down; the CPU's opponent and
  his partner drawn from the run's opponents, a character other than his:
  four characters, a fight to one team out.
- four: `--fight` Kannuki against Mikado with `--partners` Tatsumi with
  a nodachi and Utsusemi with a yari, all four the CPU's: four
  characters, four weapons, a fight to one team out, the game's own
  fall, replay and winner's view of the two it rests on.
- levels: `four`'s four, all the port's CPU (`--cpu adept,master`:
  the first team's two the adept, the second's the master; docs/CPU.md),
  each with a memory of his own as the duo passes the four through the
  game's two places: a fight to one team out, its replay played back
  alike; and `levels_log` the same fight with --cpu-log, the port's
  CPU's pads in both places all through (a run with --cpu-log hashes
  otherwise than one without, so it is not a replay's case).
- cpu: the same fighters, all four the CPU's (`--fight-cpu --teams
  cpu`): a fight that plays itself to one team out, the game's menu after
  it, both sizes alike.
- second_wins: the second team's win, written by the plan: fighters 1
  and 2 fall (a `poke` of each slot's is_defeated), the fight goes on
  with 3 and 4, then 3 falls: the first team out, the winner the second.
- draw: all four written down in two blanks: both teams out at once, the
  game's draw (both fallen in its slots).
- stock: Stock 2 (`--match stock:2`), CPU partners, the first player as
  in `retarget` but with no R2, the second team the CPU's: two fights of
  two on two, which as the port plays today the second team takes (its
  first standing each time), the score's card between, the next fight
  begun afresh with all four up, and the result's card.

    python3 tools/port_duo.py [--out FOLDER] [--shots FOLDER] [--only PLAN...]
                              [--rollback]

retarget, pads, cpu and four are recorded as replays (`--replays`) and every
file is played back by both sizes with `--replay-check`
(tools/port_replay.py): by the program that recorded it with every hash
the same. With --rollback, retarget, pads and stock are run by
tools/port_rollback.py instead (make port-rollback): the state saved,
put back and run again every 24 frames across the fight, on both sizes; and four the same way.

In WSL, with the programs `make port` builds and the player's disc (as
tools/port_stage_select.py finds them). Exit 0 if every check passes."""
import argparse
import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))
from port_stage_select import Report, fight_presses, run_plan  # noqa: E402
from port_match import RB_ARGS, RB_FIRST_PICK_CPU, RB_FIRST_PICKS, RB_OPENING, RB_START, RB_VS_MENU  # noqa: E402
from port_match import RB_VSCPU_MENU  # noqa: E402

# the opening's checkpoints and ReBushido's main menu (tools/port_match.py)
MENU_START = RB_START

HEAD = '''discs = ["port"]
cards = "none"
limit = %(limit)d
seconds = 900
'''

COMPARE = 'compare = ["screen", "winner", "port_duo_on", "port_duo_decided", "port_duo_target_1", ' \
          '"port_duo_target_2", "port_duo_target_3", "port_duo_target_4", "port_duo_down_1", "port_duo_down_2", ' \
          '"port_duo_down_3", "port_duo_down_4", "port_duo_hits_1", "port_duo_hits_2", "port_duo_hits_3", ' \
          '"port_duo_hits_4", "port_duo_rest_1", "port_duo_rest_2", "port_duo_bound_1", "port_duo_bound_2", ' \
          '"port_duo_bound_3", "port_duo_bound_4", "port_duo_kind_1", "port_duo_kind_2", "port_duo_kind_3", ' \
          '"port_duo_kind_4", "port_match_card", "port_duo_character_1", "port_duo_character_2", ' \
          '"port_duo_character_3", "port_duo_character_4"]\n'

FIGHT = '''
[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE && port_duo_on == 1"
at_frame = true
within = 3000
expect = ["port_duo_target_1 == 1", "port_duo_target_2 == 0", "port_duo_target_3 == 3", "port_duo_target_4 == 2",
          "port_duo_down_1 == 0", "port_duo_down_2 == 0", "port_duo_down_3 == 0", "port_duo_down_4 == 0",
          "winner == FIGHT_NOBODY"]
'''

AFTER = '''
[[checkpoint]]
name = "after"
when = "screen == SCREEN_AFTER_FIGHT_UPDATE"
within = 6000
expect = [%(after)s]
'''

# 1 retargets with R2, strikes 4 down, then 2.
TOML_RETARGET = 'about = "Two on two: a retarget, a blow on the second enemy, the first team\'s win ' \
    '(tools/port_duo.py)"\n' + HEAD + '''input = ["fight+20f R2", %(presses)s]
''' + COMPARE + FIGHT + '''
[[checkpoint]]
name = "retargeted"
when = "port_duo_target_1 == 3"
at_frame = true
within = 200
expect = ["port_duo_target_2 == 0", "port_duo_target_3 == 3", "port_duo_hits_2 == 0", "port_duo_hits_4 == 0"]

[[checkpoint]]
name = "struck"
when = "port_duo_hits_4 >= 1"
at_frame = true
within = 6000
expect = ["port_duo_hit_by_4 == 0", "port_duo_hits_2 == 0", "port_duo_hit_by_2 == 255", "port_duo_target_1 == 3"]

[[checkpoint]]
name = "first_down"
when = "port_duo_down_4 == 1"
at_frame = true
within = 6000
expect = ["port_duo_down_2 == 0", "port_duo_decided == 0", "winner == FIGHT_NOBODY", "port_duo_hit_by_4 == 0",
          "screen == SCREEN_FIGHT_UPDATE"]

[[checkpoint]]
name = "turned"
when = "port_duo_target_1 == 1"
at_frame = true
within = 100
expect = ["port_duo_down_4 == 1", "port_duo_down_2 == 0"]

[[checkpoint]]
name = "decided"
when = "port_duo_decided == 1"
at_frame = true
within = 9000
expect = ["port_duo_down_2 == 1", "port_duo_down_4 == 1", "port_duo_down_1 == 0", "winner == 0",
          "port_duo_hit_by_2 == 0", "port_duo_rest_1 == 0", "port_duo_rest_2 == 1", "p2_defeated == 1",
          "p1_defeated == 0"]
''' + AFTER % {'after': '"winner == 0", "port_duo_down_1 == 0"'}

# ReBushido's main menu: versus, the rule page (Stock 2, two on two with
# the CPU's partners), the grid's first fighters, the stage.
TOML_MENU = 'about = "Two on two chosen on the rule page: a Stock match of two (tools/port_duo.py)"\n' + HEAD + \
    '''input = [%(rb_opening)s,
    %(rb_vs_menu)s,
    "rule+30 RIGHT", "rule+50 DOWN", "rule+70 LEFT", "rule+90 DOWN", %(teams_presses)s "rule+150 CROSS",
    %(first_picks)s,
    %(partner_picks)s,
    "stages+40 CROSS",
    %(presses)s, %(presses2)s, %(presses_p2)s, %(presses2_p2)s,
]
''' + COMPARE.replace(']\n', ', "port_match_rule_page", "port_match_rule_row", "port_match_page_rule", '
                      '"port_match_page_teams", "port_match_teams"]\n') + MENU_START + '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false

[[checkpoint]]
name = "rule"
when = "port_match_rule_page == 1"
within = 3000
expect = ["port_match_page_rule == 0", "port_match_page_teams == 0"]

[[checkpoint]]
name = "grid"
when = "port_match_rule_page == 0"
within = 1000
expect = ["port_match_page_rule == 1", "port_match_page_teams == %(teams)d"]

[[checkpoint]]
name = "partner"
when = "port_match_crew_picked == 1"
within = 1500
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_crew_size == 2", "port_match_crew_sides == 2"]

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false
''' + FIGHT.replace('expect = [', 'expect = ["port_match_teams == %(teams)d", "port_match_rule == 1", '
                     '"port_match_count == 2", "port_duo_character_3 != port_duo_character_1", '
                     '"port_duo_character_4 != port_duo_character_2", '
                     '"port_duo_character_3 != port_duo_character_2", '
                     '"port_duo_character_4 != port_duo_character_1", ') + '''
[[checkpoint]]
name = "card"
when = "port_match_card != 0"
within = 20000
expect = ["port_match_card == 1", "port_match_teams == %(teams)d"]

[[checkpoint]]
name = "fight2"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY && port_duo_on == 1"
at_frame = true
within = 3000
expect = ["port_duo_down_1 == 0", "port_duo_down_2 == 0", "port_duo_down_3 == 0", "port_duo_down_4 == 0",
          "port_duo_character_3 != port_duo_character_1", "port_duo_character_4 != port_duo_character_2"]

[[checkpoint]]
name = "result"
when = "port_match_card == 2"
within = 12000
expect = ["port_match_over == 1", "port_match_teams == %(teams)d"]
'''

# The partners' place at the grid: one place down (two for the second side
# with four players) and Cross, then the weapon (one along with four
# players), by the first two pads or, with four players, the third and
# fourth.
PARTNER_PICKS = ('"partner+60 DOWN", "partner+70 P2 DOWN", "partner+100 CROSS", "partner+110 P2 CROSS", '
                 '"partner+300 CROSS", "partner+340 P2 CROSS"')
PARTNER_PICKS_PADS = ('"partner+60 P3 DOWN", "partner+70 P4 DOWN", "partner+90 P4 DOWN", "partner+120 P3 CROSS", '
                      '"partner+130 P4 CROSS", "partner+260 P3 RIGHT", "partner+270 P4 RIGHT", '
                      '"partner+320 P3 CROSS", "partner+360 P4 CROSS"')

# Versus the CPU: the first player's team picked, the CPU's drawn.
TOML_VSCPU = 'about = "Two on two versus the CPU, the partners picked and drawn (tools/port_duo.py)"\n' + HEAD + \
    '''input = [%(rb_opening)s,
    %(rb_vscpu_menu)s,
    "rule+30 DOWN", "rule+50 RIGHT", "rule+90 CROSS",
    %(first_pick_cpu)s,
    "partner+60 DOWN", "partner+100 CROSS", "partner+300 CROSS",
    %(presses)s,
]
''' + COMPARE + MENU_START + '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false

[[checkpoint]]
name = "rule"
when = "port_match_rule_page == 1"
within = 3000
expect = ["port_match_page_teams == 0"]

[[checkpoint]]
name = "grid"
when = "port_match_rule_page == 0"
within = 1000
expect = ["port_match_page_teams == 1"]

[[checkpoint]]
name = "partner"
when = "port_match_crew_picked == 1"
within = 1500
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_crew_size == 2", "port_match_crew_sides == 1"]
''' + FIGHT.replace('expect = [', 'expect = ["mode == FIGHT_MODE_VSCPU", "port_match_teams == 1", '
                    '"port_duo_character_3 != port_duo_character_1", '
                    '"port_duo_character_4 != port_duo_character_2", ').replace(
    'within = 3000', 'within = 4000') + '''
[[checkpoint]]
name = "decided"
when = "port_duo_decided == 1"
at_frame = true
within = 20000
expect = ["(port_duo_down_1 == 1 && port_duo_down_3 == 1) || (port_duo_down_2 == 1 && port_duo_down_4 == 1)"]
'''

# Four characters by --fight and --partners, all the CPU's, to a team out.
TOML_FOUR = 'about = "Two on two of four characters and weapons, all the CPU\'s (tools/port_duo.py)"\n' + HEAD + \
    'input = []\n' + COMPARE + \
    FIGHT.replace('expect = [', 'expect = ["port_duo_character_1 == 0", "port_duo_character_2 == 2", '
                  '"port_duo_character_3 == 1", "port_duo_character_4 == 3", "p1_weapon == 0", "p2_weapon == 0", ') + '''
[[checkpoint]]
name = "decided"
when = "port_duo_decided == 1"
at_frame = true
within = 20000
expect = ["(port_duo_down_1 == 1 && port_duo_down_3 == 1) || (port_duo_down_2 == 1 && port_duo_down_4 == 1)"]
''' + AFTER % {'after': '"winner == 0 || winner == 1"'}

# 3 (the third pad) turns from 4 to 2 and strikes him.
TOML_PADS = 'about = "Two on two: the third pad retargets and strikes (tools/port_duo.py)"\n' + HEAD + \
    '''input = ["fight+20f P3 R2", %(presses_p3)s]
''' + COMPARE + FIGHT + '''
[[checkpoint]]
name = "retargeted"
when = "port_duo_target_3 == 1"
at_frame = true
within = 200
expect = ["port_duo_target_1 == 1", "port_duo_target_4 == 2"]

[[checkpoint]]
name = "struck"
when = "port_duo_hits_4 >= 1"
at_frame = true
within = 6000
expect = ["port_duo_hit_by_4 == 2", "port_duo_target_3 == 1", "port_duo_hits_1 == 0", "port_duo_hits_3 == 0"]
'''

TOML_CPU = 'about = "Two on two, four CPU fighters, to one team out (tools/port_duo.py)"\n' + HEAD + 'input = []\n' + \
    COMPARE + FIGHT + '''
[[checkpoint]]
name = "decided"
when = "port_duo_decided == 1"
at_frame = true
within = 20000
expect = ["(port_duo_down_1 == 1 && port_duo_down_3 == 1) || (port_duo_down_2 == 1 && port_duo_down_4 == 1)"]
''' + AFTER % {'after': '"winner == 0 || winner == 1"'}

# The fighters in the slots written down: first the two firsts (the fight
# rests on the seconds then), the seconds after.
DOWN = '''
[[checkpoint]]
name = "%(name)s"
frames = %(frames)d
within = 400
expect = [%(expect)s]
'''

TOML_SECOND_WINS = 'about = "Two on two: the second team\'s win, the falls written by the plan ' \
    '(tools/port_duo.py)"\n' + HEAD + 'input = []\n' + COMPARE + FIGHT + \
    DOWN % {'name': 'later', 'frames': 60, 'expect': '"port_duo_decided == 0"'} + '''
[[checkpoint]]
name = "firsts_down"
when = "port_duo_down_1 == 1 && port_duo_down_2 == 1"
at_frame = true
within = 400
expect = ["port_duo_decided == 0", "winner == FIGHT_NOBODY", "port_duo_rest_1 == 2", "port_duo_rest_2 == 3",
          "port_duo_target_3 == 3", "port_duo_target_4 == 2"]

[[checkpoint]]
name = "decided"
when = "port_duo_decided == 1"
at_frame = true
within = 400
expect = ["port_duo_down_3 == 1", "port_duo_down_4 == 0", "winner == 1", "port_duo_rest_1 == 2",
          "port_duo_rest_2 == 3", "p1_defeated == 1", "p2_defeated == 0"]
''' + AFTER % {'after': '"winner == 1"'}

TOML_DRAW = 'about = "Two on two: all four down at once, a draw (tools/port_duo.py)"\n' + HEAD + 'input = []\n' + \
    COMPARE + FIGHT + DOWN % {'name': 'later', 'frames': 60, 'expect': '"port_duo_decided == 0"'} + '''
[[checkpoint]]
name = "seconds_in"
when = "port_duo_rest_1 == 2 && port_duo_rest_2 == 3"
at_frame = true
within = 400
expect = ["port_duo_down_1 == 1", "port_duo_down_2 == 1", "port_duo_decided == 0", "winner == FIGHT_NOBODY"]

[[checkpoint]]
name = "decided"
when = "port_duo_decided == 1"
at_frame = true
within = 400
expect = ["port_duo_down_1 == 1", "port_duo_down_2 == 1", "port_duo_down_3 == 1", "port_duo_down_4 == 1",
          "p1_defeated == 1", "p2_defeated == 1"]
''' + AFTER % {'after': '"p1_defeated == 1", "p2_defeated == 1"'}

# Stock 2: two fights, the second team's both, the score's card between.
TOML_STOCK = 'about = "Two on two, a Stock match of two lives (tools/port_duo.py)"\n' + HEAD + \
    'input = [%(presses)s, %(presses2)s]\n' + COMPARE.replace(']\n', ', "port_match_lives_p1", '
                                                              '"port_match_lives_p2"]\n') + FIGHT + '''
[[checkpoint]]
name = "card"
when = "port_match_card != 0"
within = 9000
expect = ["port_match_card == 1", "port_match_lives_p1 == 1", "port_match_lives_p2 == 2", "port_match_teams == 1",
          "port_duo_down_1 == 1", "port_duo_down_3 == 1", "winner == 1"]

[[checkpoint]]
name = "fight2"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY && port_duo_on == 1"
at_frame = true
within = 3000
expect = ["port_duo_down_1 == 0", "port_duo_down_2 == 0", "port_duo_down_3 == 0", "port_duo_down_4 == 0",
          "port_duo_decided == 0", "port_match_lives_p1 == 1"]

[[checkpoint]]
name = "result"
when = "port_match_card == 2"
within = 12000
expect = ["port_match_lives_p1 == 0", "port_match_lives_p2 == 2", "port_match_over == 1", "port_match_winner == 1"]
'''

FIGHT_ARGS = ['--mods', 'off', '--fight', 'tatara-shrine', 'kannuki', 'mikado']
# the four of `four`: Kannuki and Mikado with katanas, Tatsumi with a
# nodachi and Utsusemi with a yari
FOUR_ARGS = FIGHT_ARGS + ['--partners', 'tatsumi:nodachi', 'utsusemi:yari']
# the plans recorded as replays, and those rolled back (no `poke`: a
# plan's write into memory is no input, which neither holds)
REPLAY_PLANS = ('retarget', 'pads', 'cpu', 'four', 'levels')
# the third pad's round: a shorter walk than the first's, and Cross (the
# second fighter's guard beats a Circle aside from where he comes in)
P3_ROUND = [(0, 'RIGHT hold 8f'), (25, 'CROSS'), (40, 'CROSS')]
P2_ROUND = [(0, 'P2 LEFT hold 10f'), (15, 'P2 CIRCLE'), (50, 'P2 CIRCLE')]
ROLLBACK_PLANS = ('retarget', 'pads', 'stock', 'four', 'levels')
# levels' log of the CPU's pads (each size's run writes it in turn)
CPU_LOG = os.path.join(REPO, 'build', 'duo', 'levels_log-cpu.txt')


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'duo'))
    parser.add_argument('--shots', default=None)
    parser.add_argument('--only', nargs='*', help='the plans to run (retarget, pads, menu, menu_pads, vscpu, cpu, '
                        'second_wins, draw, stock, four, levels, levels_log)')
    parser.add_argument('--rollback', action='store_true',
                        help='retarget, pads and stock through tools/port_rollback.py instead')
    args = parser.parse_args(argv[1:])
    import playtest
    report = Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    presses = fight_presses(rounds=120)
    # the second player's in a fight of the menu: at the first team, so
    # that no fight of the CPU's partners stands off
    presses_p2 = fight_presses(40, 60, P2_ROUND)
    values = {'presses': presses, 'presses2': presses.replace('"fight+', '"fight2+'),
              'presses_p2': presses_p2, 'presses2_p2': presses_p2.replace('"fight+', '"fight2+'),
              'rb_opening': ', '.join(RB_OPENING), 'rb_vs_menu': RB_VS_MENU, 'first_picks': RB_FIRST_PICKS,
              'partner_picks': PARTNER_PICKS, 'teams_presses': '"rule+110 RIGHT",', 'teams': 1,
              'rb_vscpu_menu': RB_VSCPU_MENU, 'first_pick_cpu': RB_FIRST_PICK_CPU,
              'presses_p3': fight_presses(60, 60, P3_ROUND).replace('f ', 'f P3 ')}
    plans = [
        ('retarget', TOML_RETARGET, FIGHT_ARGS + ['--fight-2p', '--teams', 'pads'], (), 20000),
        ('pads', TOML_PADS, FIGHT_ARGS + ['--fight-2p', '--teams', 'pads'], (), 12000),
        ('menu', TOML_MENU, RB_ARGS + ['--match', 'single'], (), 40000),
        ('menu_pads', TOML_MENU.split('\n[[checkpoint]]\nname = "card"')[0].replace(
            'a Stock match of two', 'four players picking their partners').replace(
            '%(presses)s, %(presses2)s, %(presses_p2)s, %(presses2_p2)s', '%(presses)s, %(presses_p2)s'),
         RB_ARGS + ['--match', 'single'], ('values', {'partner_picks': PARTNER_PICKS_PADS, 'teams': 2,
                                                      'teams_presses': '"rule+110 RIGHT", "rule+130 RIGHT",'}),
         12000),
        ('vscpu', TOML_VSCPU, RB_ARGS + ['--match', 'single'], (), 30000),
        ('cpu', TOML_CPU, FIGHT_ARGS + ['--fight-cpu', '--teams', 'cpu'], (), 30000),
        ('second_wins', TOML_SECOND_WINS, FIGHT_ARGS + ['--fight-2p', '--teams', 'pads'],
         ('poke', [('later', 2, 'p1_defeated', 1), ('later', 2, 'p2_defeated', 1),
                   ('firsts_down', 2, 'p1_defeated', 1)]), 6000),
        ('draw', TOML_DRAW, FIGHT_ARGS + ['--fight-2p', '--teams', 'pads'],
         ('poke', [('later', 2, 'p1_defeated', 1), ('later', 2, 'p2_defeated', 1),
                   ('seconds_in', 2, 'p1_defeated', 1), ('seconds_in', 2, 'p2_defeated', 1)]), 6000),
        ('stock', TOML_STOCK, FIGHT_ARGS + ['--teams', 'cpu', '--match', 'stock:2'], (), 40000),
        ('four', TOML_FOUR, FOUR_ARGS + ['--fight-cpu', '--teams', 'cpu'], (), 30000),
        ('levels', TOML_FOUR, FOUR_ARGS + ['--fight-cpu', '--teams', 'cpu', '--cpu', 'adept,master'], (), 30000),
        ('levels_log', TOML_FOUR, FOUR_ARGS + ['--fight-cpu', '--teams', 'cpu', '--cpu', 'adept,master'], (), 30000),
    ]
    if args.rollback:
        code = 0
        for name, text, extra, _, limit in plans:
            if name not in ROLLBACK_PLANS:
                continue
            toml = os.path.join(out, '%s-rollback.toml' % name)
            with open(toml, 'w', encoding='utf-8', newline='\n') as f:
                # no pictures: a frame run again quietly paints none
                text = re.sub(r'^shot = .*\n', '', text % dict(values, limit=limit), flags=re.M)
                f.write(re.sub(r'^(name = .*)$', r'\1\nshot = false', text, flags=re.M))
            folder = os.path.join(out, 'rollback-' + name)
            code |= subprocess.run([sys.executable, os.path.join(REPO, 'tools', 'port_rollback.py'), '--test',
                                    toml, '--out', folder] + ['--with=' + arg for arg in extra],
                                   cwd=REPO).returncode
        return code
    if args.only:
        plans = [plan for plan in plans if plan[0] in args.only]
    import port_replay
    for name, text, extra, pokes, limit in plans:
        plan_pokes = pokes[1] if pokes and pokes[0] == 'poke' else ()
        plan_values = dict(values, **(pokes[1] if pokes and pokes[0] == 'values' else {}))
        more = []
        replays = os.path.join(out, 'replays-' + name)
        if name in REPLAY_PLANS:
            if os.path.isdir(replays):
                shutil.rmtree(replays)
            os.makedirs(replays)
            more = ['--replays', replays]
        if name == 'levels_log':
            more += ['--cpu-log', CPU_LOG]
        traces = run_plan(playtest, report, out, name, text % dict(plan_values, limit=limit), extra + more,
                          args.shots, plan_pokes)
        if traces is None:
            return 1
        if name == 'levels_log':
            with open(CPU_LOG, encoding='utf-8') as f:
                kinds = set(line.split()[3] for line in f if line.startswith('f '))
            report.check(kinds == {'2'}, "levels_log: every pad the port's CPU's (%s)" % sorted(kinds))
        if name in REPLAY_PLANS:
            port_replay.check_folder(report, name, replays, extra)
    print('port_duo: %d checks failed' % report.failed)
    print('%s: port_duo' % ('FAILED' if report.failed else 'OK'))
    return 1 if report.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
