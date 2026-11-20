#!/usr/bin/env python3
"""The match rules of one against one driven on the native port
(docs/PORT.md, "Match rules"; ROADMAP 21): a plan of the play tests'
kind for each rule, on both sizes of the port (build/port/,
build/port32/), whose traces must be the same line for line and whose
checkpoints must hold the score the fights made.

- vs_stock: versus mode with --match stock:2. The first fighter walks in
  and strikes low until the second, whom nobody plays, falls; the score's
  card, the next fight on the same stage with the same fighters, the
  second life taken, the result's card, then the stage select with the
  fighters kept, and Cross there starts a new match with them (the
  first fighter's presses end before that stage page, where their
  Circle would turn the rule).
- vs_first_to: versus mode with --match stock:1, turned on the stage page
  to First to (Circle) and 2 (Start); two fights won, then the stage
  select.
- vscpu_stock: versus the CPU with --match stock:2 and the stage pages:
  a fight won and two lost, the match lost, the game's own menu after a
  defeat, and its Rematch a new match against the same opponent.
- vscpu_won: versus the CPU with --match stock:1: the fight won is the
  match won, and the game's own next opponent follows, his match begun
  afresh.
- vs_crew: versus mode with --match crew:3: the crews' later places
  picked on the grid after the stage page; a fight won (the loser's
  next comes in, the winner stays), a draw (one out of each; the plan
  writes the first fighter's fall, a `poke`), a fight won that empties
  the second crew; the stage select with the crews kept, and Cross there
  a new match of them.
- vs_crew_drawn: Crew 2 of two draws: both crews out at once, a drawn
  match.
- vscpu_crew: versus the CPU with --match crew:2: the player's crew, the
  CPU's led by the game's opponent, the match lost, the game's menu, and
  its Rematch the crews' first fighters loaded again.

In both versus plans the pictures of the game's screen at each fight's
end (`card`, `result`) are held to have none of the game's own "WINS 00"
counter, which the match's strip replaces (fight.c, fight_UpdateMode;
port_MatchHidesWins). The play tests' single fights keep it.

    python3 tools/port_match.py [--out FOLDER] [--shots FOLDER]
                                [--only PLAN...] [--rollback]

With --rollback, vs_stock's match and a Crew 2 match (vs_crew_won, no
draw) are run by tools/port_rollback.py instead (make port-rollback):
the state saved, put back and run again every 24 frames across the
whole match, on both sizes.

In WSL, with the programs `make port` builds and the player's disc (as
tools/port_stage_select.py finds them). Exit 0 if every check passes."""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))
from port_stage_select import (HEAD, OPENING, START, Report, fight_presses,  # noqa: E402
                               run_plan)

COMPARE = 'compare = ["screen", "stage", "mode", "winner", "port_match_rule", "port_match_lives_p1", ' \
          '"port_match_lives_p2", "port_match_wins_p1", "port_match_wins_p2", "port_match_card"]\n'

# Versus mode: the top menu's fourth row, each side's first fighter and
# weapon, the stage under the second page's first cursor.
VS_MENU = '''"menu+30 DOWN", "menu+60 DOWN", "menu+90 DOWN", "menu+150 CROSS",
    "select+300 CROSS", "select+340 P2 CROSS", "select+520 CROSS", "select+560 P2 CROSS"'''

SELECT = '''
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
'''

# A fight of the match: its start, and the card that follows it.
FIGHT = '''
[[checkpoint]]
name = "%(name)s"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
at_frame = true
within = 3000
expect = [%(expect)s]

[[checkpoint]]
name = "%(card)s"
when = "port_match_card != 0"
within = 6000
expect = [%(card_expect)s]
'''


def fight(name, expect, card, card_expect):
    return FIGHT % {'name': name, 'expect': ', '.join('"%s"' % e for e in expect),
                    'card': card, 'card_expect': ', '.join('"%s"' % e for e in card_expect)}


SAME = ['stage == %(stage)d', 'p1_character == %(p1)d', 'p2_character == %(p2)d', 'mode == 4']

TOML_VS_STOCK = 'about = "Versus mode, a Stock match of two lives (tools/port_match.py)"\n' + HEAD + \
    '''input = [%(opening)s,
    %(menu)s,
    "stages+40 CROSS",
    %(presses_stock)s,
    "back+60 CROSS",
]
''' + COMPARE + START + SELECT + \
    fight('fight', ['port_match_rule == 1', 'port_match_count == 2', 'port_match_lives_p1 == 2',
                    'port_match_lives_p2 == 2', 'mode == 4'],
          'card', ['port_match_card == 1', 'port_match_lives_p1 == 2', 'port_match_lives_p2 == 1',
                   'port_match_fights == 1', 'port_match_last == 0', 'port_match_over == 0']) + \
    fight('fight2', SAME + ['port_match_lives_p2 == 1', 'p1_defeated == 0', 'p2_defeated == 0'],
          'result', ['port_match_card == 2', 'port_match_lives_p1 == 2', 'port_match_lives_p2 == 0',
                     'port_match_fights == 2', 'port_match_over == 1', 'port_match_winner == 0']) + '''
[[checkpoint]]
name = "back"
when = "port_on_stages == 1"
within = 3000
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_over == 1", "port_match_lives_p2 == 0"]

[[checkpoint]]
name = "again"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["mode == 4", "p1_character == %(p1)d", "p2_character == %(p2)d",
          "port_match_lives_p1 == 2", "port_match_lives_p2 == 2", "port_match_fights == 0"]
'''

TOML_VS_FIRST_TO = 'about = "Versus mode, First to 2 chosen on the stage page (tools/port_match.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    %(menu)s,
    "stages+30 CIRCLE", "stages+50 START", "stages+80 CROSS",
    %(presses)s,
]
''' + COMPARE + START + SELECT + '''
[[checkpoint]]
name = "rule"
after = 70
expect = ["port_match_page_rule == 2", "port_match_page_count == 2"]
''' + \
    fight('fight', ['port_match_rule == 2', 'port_match_count == 2', 'port_match_wins_p1 == 0',
                    'port_match_wins_p2 == 0', 'mode == 4'],
          'card', ['port_match_card == 1', 'port_match_wins_p1 == 1', 'port_match_wins_p2 == 0',
                   'port_match_over == 0']) + \
    fight('fight2', SAME + ['port_match_wins_p1 == 1'],
          'result', ['port_match_card == 2', 'port_match_wins_p1 == 2', 'port_match_wins_p2 == 0',
                     'port_match_over == 1', 'port_match_winner == 0']) + '''
[[checkpoint]]
name = "back"
when = "port_on_stages == 1"
within = 3000
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_wins_p1 == 2"]
'''

# Versus the CPU (the top menu's second row) through the stage pages, as
# tools/port_stage_select.py's vscpu_next. As the port plays today the
# first opponent (character 14) falls to the first fight's presses and wins
# the next two: the match lost 1 to 2, the game's own menu after it, and
# its Rematch a new match against him.
VSCPU_MENU = '''"menu+30 DOWN", "menu+90 CROSS",
    "select+300 CROSS", "select+520 CROSS",
    "stages+40 CROSS"'''
TOML_VSCPU_STOCK = 'about = "Versus the CPU, a Stock match of two lives lost (tools/port_match.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    %(vscpu)s,
    %(presses)s,
    "lost+30 CROSS",
]
''' + COMPARE + START + SELECT + \
    fight('fight', ['port_match_rule == 1', 'port_match_lives_p2 == 2', 'mode == 1'],
          'card', ['port_match_card == 1', 'port_match_lives_p1 == 2', 'port_match_lives_p2 == 1']) + \
    fight('fight2', ['mode == 1', 'p2_character == %(cpu)d', 'port_match_lives_p2 == 1'],
          'card2', ['port_match_card == 1', 'port_match_lives_p1 == 1', 'port_match_lives_p2 == 1',
                    'port_match_last == 1']) + \
    fight('fight3', ['mode == 1', 'p2_character == %(cpu)d'],
          'result', ['port_match_card == 2', 'port_match_lives_p1 == 0', 'port_match_over == 1',
                     'port_match_winner == 1']) + '''
[[checkpoint]]
name = "lost"
when = "screen == SCREEN_AFTER_FIGHT_UPDATE && port_match_card == 0"
within = 3000
expect = ["port_match_lives_p1 == 2", "port_match_lives_p2 == 2", "port_match_fights == 0"]

[[checkpoint]]
name = "rematch"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
within = 3000
expect = ["mode == 1", "p2_character == %(cpu)d", "port_match_rule == 1", "port_match_lives_p1 == 2",
          "port_match_lives_p2 == 2"]
'''

# Versus the CPU with Stock 1: the first fight wins the match, and the
# game goes on to the next opponent, whose match starts afresh.
TOML_VSCPU_WON = 'about = "Versus the CPU, a Stock match of one won, the next opponent (tools/port_match.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    %(vscpu)s,
    %(presses)s,
]
''' + COMPARE + START + SELECT + \
    fight('fight', ['port_match_rule == 1', 'port_match_lives_p2 == 1', 'mode == 1'],
          'result', ['port_match_card == 2', 'port_match_lives_p2 == 0', 'port_match_over == 1',
                     'port_match_winner == 0']) + '''
[[checkpoint]]
name = "next"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
within = 3000
expect = ["mode == 1", "p2_character != %(cpu)d", "port_match_rule == 1", "port_match_lives_p1 == 1",
          "port_match_lives_p2 == 1", "port_match_fights == 0", "port_match_card == 0"]
'''

# Crew (docs/PORT.md, "Match rules", "Crew"): the first place is the
# fighters and weapons picked before the stage page, the later ones the
# grid and the weapons again once a place, each cursor where it was: both
# players one place down each time (Kannuki, Mikado, Tatsumi against
# Gengoro, Kaun, Jo).
# a Crew match's plans run longer: fighters are loaded each fight
CREW_HEAD = HEAD.replace('limit = 9000', 'limit = 14000')
CREW_COMPARE = COMPARE.replace('"port_match_card"]', '"port_match_card", "port_match_crew_picked", '
                               '"p1_character", "p2_character", "p1_weapon", "p2_weapon"]')
CREW_PICKS = '''"crew2+60 DOWN", "crew2+70 P2 DOWN", "crew2+100 CROSS", "crew2+110 P2 CROSS",
    "crew2+300 CROSS", "crew2+340 P2 CROSS",
    "crew3+60 DOWN", "crew3+70 P2 DOWN", "crew3+100 CROSS", "crew3+110 P2 CROSS",
    "crew3+300 CROSS", "crew3+340 P2 CROSS"'''
CREW_PLACES = '''
[[checkpoint]]
name = "crew2"
when = "port_match_crew_picked == 1"
within = 1500
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_page_rule == 3", "port_match_crew_size == 3",
          "port_match_crew_sides == 2", "port_crew_p1_1 == %(p1)d", "port_crew_p2_1 == %(p2)d"]

[[checkpoint]]
name = "crew3"
when = "port_match_crew_picked == 2"
within = 1500
expect = ["menu_page == MENU_PAGE_SELECT", "port_crew_p1_2 == %(p1_2)d", "port_crew_p2_2 == %(p2_2)d"]
'''

# A fight a draw: both fall. Nobody plays the second fighter, so the
# first player's strikes would always win it; the plan writes that the
# first fell too, once the second has, which the game counts as a draw
# two seconds later (a `poke`: a developer's way, never in a test held to
# the emulator).
DOWN = '''
[[checkpoint]]
name = "%s"
when = "p2_defeated != 0"
within = 3000
'''

# Versus mode, Crew 3: the first fight won by the first player (the
# second's first fighter out, the winner staying), the second a draw (one
# out of each), the third won (the second crew out), the result's card,
# the stage select, and Cross there a new match of the same crews.
TOML_VS_CREW = 'about = "Versus mode, a Crew match of three (tools/port_match.py)"\n' + CREW_HEAD + \
    '''input = [%(opening)s,
    %(menu)s,
    "stages+40 CROSS",
    %(picks)s,
    %(presses_crew)s,
    "back+60 CROSS",
]
''' + CREW_COMPARE + START + SELECT + CREW_PLACES + \
    fight('fight', ['port_match_rule == 3', 'port_match_count == 3', 'port_match_lives_p1 == 3',
                    'port_match_lives_p2 == 3', 'port_match_crew_picked == 3', 'mode == 4',
                    'p1_character == %(p1)d', 'p2_character == %(p2)d'],
          'card', ['port_match_card == 1', 'port_match_lives_p1 == 3', 'port_match_lives_p2 == 2',
                   'port_match_fights == 1', 'port_match_last == 0', 'port_match_over == 0']) + \
    fight('fight2', ['p1_character == %(p1)d', 'p2_character == %(p2_2)d', 'p1_defeated == 0',
                     'p2_defeated == 0', 'stage == %(stage)d', 'port_match_lives_p2 == 2'],
          'card2', ['port_match_card == 1', 'port_match_last == 2', 'port_match_lives_p1 == 2',
                    'port_match_lives_p2 == 1', 'port_match_over == 0']).replace(
        '\n[[checkpoint]]\nname = "card2"', DOWN % 'down2' + '\n[[checkpoint]]\nname = "card2"') + \
    fight('fight3', ['p1_character == %(p1_2)d', 'p2_character == %(p2_3)d', 'stage == %(stage)d'],
          'result', ['port_match_card == 2', 'port_match_lives_p1 == 2', 'port_match_lives_p2 == 0',
                     'port_match_fights == 3', 'port_match_over == 1', 'port_match_winner == 0']) + '''
[[checkpoint]]
name = "back"
when = "port_on_stages == 1"
within = 3000
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_over == 1", "port_match_crew_picked == 3"]

[[checkpoint]]
name = "again"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["mode == 4", "p1_character == %(p1)d", "p2_character == %(p2)d", "port_match_rule == 3",
          "port_match_lives_p1 == 3", "port_match_lives_p2 == 3", "port_match_fights == 0",
          "port_crew_p1_2 == %(p1_2)d", "port_crew_p2_3 == %(p2_3)d"]
'''

# Versus mode, Crew 2, two draws: both crews out at once, a drawn match.
TOML_VS_CREW_DRAWN = 'about = "Versus mode, a Crew match of two drawn (tools/port_match.py)"\n' + CREW_HEAD + \
    '''input = [%(opening)s,
    %(menu)s,
    "stages+40 CROSS",
    "crew2+60 DOWN", "crew2+70 P2 DOWN", "crew2+100 CROSS", "crew2+110 P2 CROSS",
    "crew2+300 CROSS", "crew2+340 P2 CROSS",
    %(presses)s,
]
''' + CREW_COMPARE + START + SELECT + CREW_PLACES.split('\n[[checkpoint]]\nname = "crew3"')[0].replace(
        '"port_match_crew_size == 3"', '"port_match_crew_size == 2"') + \
    fight('fight', ['port_match_rule == 3', 'port_match_count == 2', 'port_match_lives_p1 == 2',
                    'port_match_lives_p2 == 2', 'port_match_crew_picked == 2'],
          'card', ['port_match_card == 1', 'port_match_last == 2', 'port_match_lives_p1 == 1',
                   'port_match_lives_p2 == 1', 'port_match_over == 0']).replace(
        '\n[[checkpoint]]\nname = "card"', DOWN % 'down' + '\n[[checkpoint]]\nname = "card"') + \
    fight('fight2', ['p1_character == %(p1_2)d', 'p2_character == %(p2_2)d'],
          'result', ['port_match_card == 2', 'port_match_last == 2', 'port_match_lives_p1 == 0',
                     'port_match_lives_p2 == 0', 'port_match_over == 1', 'port_match_winner == 2']).replace(
        '\n[[checkpoint]]\nname = "result"', DOWN % 'down2' + '\n[[checkpoint]]\nname = "result"') + '''
[[checkpoint]]
name = "back"
when = "port_on_stages == 1"
within = 3000
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_over == 1", "port_match_winner == 2"]
'''

# Versus mode, Crew 2, both fights the first player's: the second crew
# out. tools/port_replay.py's, whose fights are played back from their
# replays (no draw: a plan's draw is written into memory, which no replay
# holds).
TOML_VS_CREW_WON = 'about = "Versus mode, a Crew match of two won (tools/port_match.py)"\n' + CREW_HEAD + \
    '''input = [%(opening)s,
    %(menu)s,
    "stages+40 CROSS",
    "crew2+60 DOWN", "crew2+70 P2 DOWN", "crew2+100 CROSS", "crew2+110 P2 CROSS",
    "crew2+300 CROSS", "crew2+340 P2 CROSS",
    %(presses)s,
]
''' + CREW_COMPARE + START + SELECT + CREW_PLACES.split('\n[[checkpoint]]\nname = "crew3"')[0].replace(
        '"port_match_crew_size == 3"', '"port_match_crew_size == 2"') + \
    fight('fight', ['port_match_rule == 3', 'port_match_lives_p1 == 2', 'port_match_lives_p2 == 2'],
          'card', ['port_match_card == 1', 'port_match_last == 0', 'port_match_lives_p2 == 1']) + \
    fight('fight2', ['p1_character == %(p1)d', 'p2_character == %(p2_2)d'],
          'result', ['port_match_card == 2', 'port_match_lives_p1 == 2', 'port_match_lives_p2 == 0',
                     'port_match_winner == 0'])

# Versus the CPU, Crew 2, through the stage pages: the player's second
# place picked after the stage, the CPU's crew led by the game's first
# opponent (character 9 after these presses) and the rest drawn (7). As
# the port plays today the CPU's first takes both of the player's
# fighters, one after the other: the match lost, the game's own menu
# after a defeat, and its Rematch a new match of the crews' first
# fighters, loaded again.
VSCPU_CREW_MENU = '''"menu+30 DOWN", "menu+90 CROSS",
    "select+300 CROSS", "select+520 CROSS",
    "stages+40 CROSS",
    "crew2+60 DOWN", "crew2+100 CROSS", "crew2+300 CROSS"'''
TOML_VSCPU_CREW = 'about = "Versus the CPU, a Crew match of two (tools/port_match.py)"\n' + CREW_HEAD + \
    '''input = [%(opening)s,
    %(vscpu_crew)s,
    %(presses)s,
    "lost+30 CROSS",
]
''' + CREW_COMPARE + START + SELECT + '''
[[checkpoint]]
name = "crew2"
when = "port_match_crew_picked == 1"
within = 1500
expect = ["port_match_page_rule == 3", "port_match_crew_size == 2", "port_match_crew_sides == 1",
          "port_crew_p1_1 == %(p1)d"]
''' + \
    fight('fight', ['port_match_rule == 3', 'port_match_count == 2', 'mode == 1', 'p1_character == %(p1)d',
                    'p2_character == %(cpu_crew)d', 'port_crew_p2_1 == %(cpu_crew)d',
                    'port_crew_p2_2 == %(cpu_crew_2)d', 'port_crew_p1_2 == %(p1_2)d'],
          'card', ['port_match_card == 1', 'port_match_last == 1', 'port_match_lives_p1 == 1',
                   'port_match_lives_p2 == 2']) + \
    fight('fight2', ['mode == 1', 'p1_character == %(p1_2)d', 'p2_character == %(cpu_crew)d'],
          'result', ['port_match_card == 2', 'port_match_lives_p1 == 0', 'port_match_lives_p2 == 2',
                     'port_match_over == 1', 'port_match_winner == 1']).replace('within = 6000', 'within = 9000') + '''

[[checkpoint]]
name = "lost"
when = "screen == SCREEN_AFTER_FIGHT_UPDATE && port_match_card == 0"
within = 3000
expect = ["port_match_lives_p1 == 2", "port_match_lives_p2 == 2", "port_match_fights == 0"]

[[checkpoint]]
name = "rematch"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
within = 3000
expect = ["mode == 1", "p1_character == %(p1)d", "p2_character == %(cpu_crew)d", "port_match_rule == 3",
          "port_match_lives_p1 == 2", "port_match_lives_p2 == 2"]
'''

# versus mode's first fighters (the grid's first place and the second
# cursor's) on the second page's first stage (Building Rooftop); versus
# the CPU's first opponent of the run as an empty save and the stage pages
# have it; the crews' later places as CREW_PICKS moves the cursors
VALUES = {'menu': VS_MENU, 'vscpu': VSCPU_MENU, 'vscpu_crew': VSCPU_CREW_MENU, 'stage': 29, 'p1': 0, 'p2': 12,
          'cpu': 14, 'cpu_crew': 9, 'cpu_crew_2': 7, 'picks': CREW_PICKS, 'p1_2': 2, 'p2_2': 13, 'p1_3': 1, 'p2_3': 14}
# A Crew match's plans (also tools/port_replay.py's): name, plan, the
# program's arguments, the plan's pokes (the draws).
CREW_POKE = 4
# vs_crew's first player's presses end before the stage page after the
# match, where their Circle would turn the rule
CREW_ROUNDS = 37
CREW_PLANS = [
    ('vs_crew', TOML_VS_CREW, ['--mods', 'off', '--match', 'crew:3'], [('down2', CREW_POKE, 'p1_defeated', 1)]),
    ('vs_crew_drawn', TOML_VS_CREW_DRAWN, ['--mods', 'off', '--match', 'crew:2'],
     [('down', CREW_POKE, 'p1_defeated', 1), ('down2', CREW_POKE, 'p1_defeated', 1)]),
    ('vscpu_crew', TOML_VSCPU_CREW, ['--mods', 'off', '--stage-select', '--unlock-all', '--match', 'crew:2']),
]
# tools/port_replay.py's Crew match
CREW_REPLAY_PLAN = ('vs_crew_won', TOML_VS_CREW_WON, ['--mods', 'off', '--match', 'crew:2'])
# vs_stock's first player's presses end before the stage page after the
# match, as vs_crew's
STOCK_ROUNDS = 34


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'match'))
    parser.add_argument('--shots', default=None)
    parser.add_argument('--only', nargs='*', help='the plans to run (vs_stock, vs_first_to, vscpu_stock, vscpu_won, vs_crew, '
                        'vs_crew_drawn, vscpu_crew)')
    parser.add_argument('--rollback', action='store_true',
                        help="vs_stock's match through tools/port_rollback.py instead: rolled back every "
                        "24 frames, on both sizes")
    args = parser.parse_args(argv[1:])
    import playtest
    report = Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    values = dict(VALUES, opening=', '.join(OPENING), presses=fight_presses(),
                  presses_crew=fight_presses(CREW_ROUNDS), presses_stock=fight_presses(STOCK_ROUNDS))
    plans = [
        ('vs_stock', TOML_VS_STOCK, ['--mods', 'off', '--match', 'stock:2']),
        ('vs_first_to', TOML_VS_FIRST_TO, ['--mods', 'off', '--match', 'stock:1']),
        ('vscpu_stock', TOML_VSCPU_STOCK, ['--mods', 'off', '--stage-select', '--unlock-all', '--match', 'stock:2']),
        ('vscpu_won', TOML_VSCPU_WON, ['--mods', 'off', '--stage-select', '--unlock-all', '--match', 'stock:1']),
    ] + CREW_PLANS
    if args.rollback:
        # the Stock match, and a Crew match, its crews picked and its
        # fighters loaded afresh between fights
        code = 0
        for name, text, extra in (plans[0], CREW_REPLAY_PLAN):
            toml = os.path.join(out, '%s-rollback.toml' % name)
            with open(toml, 'w', encoding='utf-8', newline='\n') as f:
                # no pictures: a frame run again quietly paints none, and a
                # checkpoint's picture in it would not be the first run's
                text = re.sub(r'^shot = .*\n', '', text % values, flags=re.M)
                f.write(re.sub(r'^(name = .*)$', r'\1\nshot = false', text, flags=re.M))
            more = ['--with=' + arg for arg in extra]
            folder = os.path.join(out, 'rollback' if name == 'vs_stock' else 'rollback-' + name)
            code |= subprocess.run([sys.executable, os.path.join(REPO, 'tools', 'port_rollback.py'), '--test', toml,
                                    '--out', folder] + more, cwd=REPO).returncode
        return code
    if args.only:
        plans = [plan for plan in plans if plan[0] in args.only]
    for plan in plans:
        name, text, extra = plan[:3]
        pokes = plan[3] if len(plan) > 3 else ()
        traces = run_plan(playtest, report, out, name, text % values, extra, None, pokes)
        if traces is None:
            return 1
        run = os.path.join(out, name + '-out64')
        shots = [line.split()[1:] for line in traces[64].splitlines() if line.startswith('shot ')]
        if name.startswith('vs_'):
            # the strip shows the score: the game's own wins counter is not
            # drawn over a fight of the match
            for shot, width, height, bpp in shots:
                if shot in ('card', 'result'):
                    count = wins_rule_pixels(os.path.join(run, shot + '.raw'), int(width), int(height), int(bpp))
                    report.check(count == 0, '%s: at `%s` the game\'s "WINS 00" is not drawn (%s pixels of its '
                                 'red rules)' % (name, shot, count))
        if args.shots:
            os.makedirs(args.shots, exist_ok=True)
            for shot, width, height, bpp in shots:
                if playtest.convert_shot(run, shot, int(width), int(height), int(bpp)) is not None:
                    shutil.copy(os.path.join(run, shot + '.png'), os.path.join(args.shots, '%s-%s.png' % (name, shot)))
    return 1 if report.failed else 0


def wins_rule_pixels(raw, width, height, bpp):
    """The pixels of the wins counter's red rules (hud.c, hud_DrawWinCounts,
    RULE_R/G/B: 255, 16, 16) in the top quarter of a checkpoint's picture of
    the game's screen; None if there is no picture. The stage, the blood and
    the strip have none of that colour (the picture of a Stock fight before
    the counter was left out had 252, the rules alone)."""
    if not os.path.isfile(raw) or bpp != 16:
        return None
    with open(raw, 'rb') as f:
        data = f.read()
    rule = 0xFF >> 3 | (0x10 >> 3) << 5 | (0x10 >> 3) << 10
    pixels = struct.unpack('<%dH' % (width * (height // 4)), data[:width * (height // 4) * 2])
    return sum(1 for v in pixels if v & 0x7FFF == rule)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
