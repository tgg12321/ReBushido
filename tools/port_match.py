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
- rb_vscpu_level, rb_story_level: ReBushido's top menu, the CPU's level
  chosen before the grid (port_match.h): versus the CPU on the rule
  page's row (Adept), held for its fight, which the port's CPU plays;
  the story on a page of that row alone (Master).
- group_stock, group_first_to, group_crew: group battle (the game's team
  battle) with --match stock:2, first_to:2 and crew:3: the first match
  won, the card of the scores between matches, the second match begun and
  three hundred frames of it, and none of the rules in play at any of
  them (they are versus' and versus the CPU's: port_MatchModeHasRules).
  The window's build crashed as that first match ended (main.c,
  disp_DrawLoadingScreen).

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
                   'port_match_over == 0', 'port_match_kept_p1 == 0', 'p1_leg_wounds == 1']) + \
    fight('fight2', SAME + ['port_match_wins_p1 == 1', 'p1_leg_wounds == 0', 'port_match_kept_p1 == 0'],
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

# A Crew match's end: the top menu, the crews let go (versus, online and
# versus the CPU alike, won or lost).
TOP = '''
[[checkpoint]]
name = "top"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_TOP && port_match_to_top == 1"
within = 3000
expect = ["port_match_crew_size == 0", "port_match_crew_picked == 0"]
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
                     'port_match_fights == 3', 'port_match_over == 1', 'port_match_winner == 0']) + TOP

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
        '\n[[checkpoint]]\nname = "result"', DOWN % 'down2' + '\n[[checkpoint]]\nname = "result"') + TOP

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
                    'port_crew_p2_2 == %(cpu_crew_2)d', 'port_crew_p1_2 == %(p1_2)d',
                    'port_crew_p2_1 != port_crew_p2_2'],
          'card', ['port_match_card == 1', 'port_match_last == 1', 'port_match_lives_p1 == 1',
                   'port_match_lives_p2 == 2']) + \
    fight('fight2', ['mode == 1', 'p1_character == %(p1_2)d', 'p2_character == %(cpu_crew)d'],
          'result', ['port_match_card == 2', 'port_match_lives_p1 == 0', 'port_match_lives_p2 == 2',
                     'port_match_over == 1', 'port_match_winner == 1']).replace('within = 6000', 'within = 9000') + \
    TOP

# Group battle (the game's team battle, FIGHT_MODE_TEAM): the top menu's
# fifth row; the first side picks the first place of the grid and its
# weapon, the second the same, three fighters each, one place down each
# time; one point a match. The first player walks in and strikes until the
# second fighter, whom nobody plays, falls: the match is the first side's,
# the card of the scores between matches (the loading screen
# LOADING_SCREEN_TEAM_SCORES, drawn by the loader that fight_NextTeamMatch
# waits on from inside a frame of the fight), and the second match begun.
# The port crashed there in the window's build (main.c,
# disp_DrawLoadingScreen). Run under each match rule given on the command
# line: none applies to the group battle, whose own rules are the game's.
def group_menu():
    presses = ['"menu+30 DOWN"', '"menu+60 DOWN"', '"menu+90 DOWN"', '"menu+120 DOWN"', '"menu+180 CROSS"',
               '"menu+400 CROSS"', '"menu+520 CROSS"', '"menu+540 P2 CROSS"']
    for t in range(620, 1311, 60):
        presses += ['"menu+%d CROSS"' % t, '"menu+%d P2 CROSS"' % (t + 8), '"menu+%d DOWN"' % (t + 30),
                    '"menu+%d P2 DOWN"' % (t + 38)]
    return ', '.join(presses)


def group_presses(last=2396):
    """The first fighter's presses: a walk in, then a strike every 12
    frames, Circle, Cross and Square in turn, and a step right after each
    twenty."""
    presses = ['"fight+60f RIGHT hold 40f"']
    buttons = ['CIRCLE', 'CROSS', 'SQUARE']
    at = 110
    struck = 0
    walked = 0
    while at <= last:
        if struck and struck % 20 == 0 and walked != struck:
            presses.append('"fight+%df RIGHT hold 10f"' % at)
            at += 14
            walked = struck
            continue
        presses.append('"fight+%df %s"' % (at, buttons[struck % 3]))
        struck += 1
        at += 12
    return ', '.join(presses)


GROUP_COMPARE = COMPARE.replace('"port_match_card"]', '"port_match_card", "team_match", "team_points_p1", '
                                '"team_points_p2", "loading_screen"]')
GROUP_RULE_OFF = ['port_match_rule == 0', 'port_match_card == 0', 'port_match_fights == 0']
TOML_GROUP = 'about = "Group battle under the rule %(rule)s, which does not apply to it (tools/port_match.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    %(group_menu)s,
    %(group_presses)s,
]
''' + GROUP_COMPARE + START.replace('screen_frames >= 120', 'screen_frames >= 60') + '''
[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE && mode == FIGHT_MODE_TEAM"
at_frame = true
within = 3000
expect = [%(fight)s]

[[checkpoint]]
name = "ippon"
when = "team_points_p1 == 1"
within = 6000
shot = false
expect = ["team_match == 0", "team_points_p2 == 0", "p2_defeated != 0", %(off)s]

[[checkpoint]]
name = "card"
when = "loading_screen == 10 && team_match == 1"
within = 3000
shot = false
expect = ["mode == FIGHT_MODE_TEAM", %(off)s]

[[checkpoint]]
name = "fight2"
when = "screen == SCREEN_FIGHT_UPDATE && team_match == 1"
at_frame = true
within = 3000
expect = ["mode == FIGHT_MODE_TEAM", "team_points_p1 == 0", "team_points_p2 == 0", "p1_defeated == 0",
          "p2_defeated == 0", %(off)s]

[[checkpoint]]
name = "fight2_on"
after = 300
shot = false
expect = ["mode == FIGHT_MODE_TEAM", "team_match == 1", %(off)s]
'''
# the rules, as --match gives them, under which the group battle is run
GROUP_RULES = ['stock:2', 'first_to:2', 'crew:3']


def group_plans():
    """The group battle's plans, one a rule: name, plan, the program's
    arguments."""
    plans = []
    for rule in GROUP_RULES:
        off = ', '.join('"%s"' % claim for claim in GROUP_RULE_OFF)
        fight = ', '.join('"%s"' % claim for claim in ['team_match == 0', 'team_points_p1 == 0',
                                                        'team_points_p2 == 0'] + GROUP_RULE_OFF)
        text = TOML_GROUP.replace('%(rule)s', rule).replace('%(off)s', off).replace('%(fight)s', fight)
        plans.append(('group_' + rule.split(':')[0], text, ['--mods', 'off', '--match', rule]))
    return plans


# ---- the rule first (ReBushido's top menu, --menu rebushido) ----
#
# With ReBushido's top menu the rule is chosen on a page of its own before
# the first select screen's grid (docs/PORT.md, "Match rules", "The rule
# first"): Left and Right turn the row's value, Up and Down the row, Cross
# takes it. A crew's places are then picked one after another at the grid,
# each taken as its weapons cross, before the stage page; a character the
# crew has is refused (Cross plays the locked sound and picks nothing) and
# drawn dimmed, or locked when every crew picking has it. The winner of a
# Stock or Crew fight keeps his wounds into the next; at a Crew match's end
# the game goes back to the top menu.
#
# The opening is the play tests' less the options page, which ReBushido's
# top menu does not show; the top menu's rows are Singleplayer, Versus,
# Training, Online Play and Options, the Singleplayer page's Story, Vs. CPU,
# Slash and Group Battle.
RB_START = '\n[[checkpoint]]\nname = "movie"' + START.split('\n[[checkpoint]]\nname = "movie"', 1)[1]
RB_OPENING = [press for press in OPENING if not press.startswith('"page+')]
RB_VS_MENU = '"menu+30 DOWN", "menu+90 CROSS"'
RB_VSCPU_MENU = '"menu+30 CROSS", "menu+90 DOWN", "menu+150 CROSS"'
RB_ARGS = ['--mods', 'off', '--menu', 'rebushido']
RB_COMPARE = CREW_COMPARE.replace('"p1_weapon", "p2_weapon"]', '"p1_weapon", "p2_weapon", "port_match_rule_page", '
                                  '"port_match_rule_row", "port_match_page_rule", "port_match_kept_p1", '
                                  '"port_match_kept_p2", "p1_leg_wounds", "p2_leg_wounds", "port_match_to_top"]')


def rule_presses(rights, lefts):
    """The rule page's presses: from Single fight, Right to Stock, First to
    or Crew, then Down to its number, which Left takes down, and Cross."""
    presses = ['"rule+%d RIGHT"' % (30 + 12 * i) for i in range(rights)]
    at = 30 + 12 * rights + 10
    presses.append('"rule+%d DOWN"' % at)
    for i in range(lefts):
        at += 12
        presses.append('"rule+%d LEFT"' % at)
    presses.append('"rule+%d CROSS"' % (at + 30))
    return ', '.join(presses)


RB_RULE = '''
[[checkpoint]]
name = "rule"
when = "port_match_rule_page == 1"
within = 3000
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_rule_first == 1", "port_match_crew_picked == 0",
          "port_match_page_rule == 0"]

[[checkpoint]]
name = "grid"
when = "port_match_rule_page == 0"
within = 1000
shot = false
expect = [GRID_EXPECT]
'''
# the grid's first place: each side the fighter and the weapon under its
# cursor, as versus mode's play tests take them
RB_FIRST_PICKS = '"grid+60 CROSS", "grid+70 P2 CROSS", "grid+260 CROSS", "grid+300 P2 CROSS"'
RB_FIRST_PICK_CPU = '"grid+60 CROSS", "grid+260 CROSS"'
# a later place: first Cross on the fighter the crew has (refused), then
# one place down and Cross, and the weapon under the cursor
RB_PLACE = '''"crewN+60 CROSS", "crewN+66 P2 CROSS", "crewN+100 DOWN", "crewN+110 P2 DOWN",
    "crewN+140 CROSS", "crewN+150 P2 CROSS", "crewN+340 CROSS", "crewN+380 P2 CROSS"'''
RB_PLACE_CPU = '''"crewN+60 CROSS", "crewN+100 DOWN", "crewN+140 CROSS", "crewN+340 CROSS"'''


def rb_place(n, cpu=False):
    return (RB_PLACE_CPU if cpu else RB_PLACE).replace('crewN', 'crew%d' % n)


def rb_crew_place(n, size):
    """The checkpoints of a crew's later place `n`: the grid up again, and
    after the refused Cross nothing picked."""
    return '''
[[checkpoint]]
name = "crew%d"
when = "port_match_crew_picked == %d"
within = 1500
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_crew_size == %d"]

[[checkpoint]]
name = "dup%d"
after = 90
expect = ["port_match_crew_picked == %d", "port_match_rule_page == 0"]
''' % (n, n - 1, size, n, n - 1)


# the wound: once a fight is under way the plan gives the first fighter a
# leg wound (a `poke`, as the draws are written), and the second one too,
# who then falls to the first's strikes
WOUND = '''
[[checkpoint]]
name = "%s"
after = 150
shot = false
'''
RB_POKE = 4


def wound_pokes(checkpoint, both=True):
    """The pokes of the wounds at `checkpoint`: the first fighter's leg,
    and the second's too if `both`."""
    pokes = [(checkpoint, RB_POKE, 'p1_leg_wounds', 1)]
    if both:
        pokes.append((checkpoint, RB_POKE, 'p2_leg_wounds', 1))
    return pokes


def wounded(text, name):
    """`text` with the wound's checkpoint before the checkpoint `name`."""
    head = '\n[[checkpoint]]\nname = "%s"' % name
    return text.replace(head, WOUND % (name + '_wound') + head, 1)


RB_SELECT = SELECT.split('\n[[checkpoint]]\nname = "stages"')[0]

# Versus mode, Crew 2 chosen on the rule page (Right three times, Down,
# Left three times: five to two); the first place picked at the grid, the
# second refused the first's fighter on each side and then one place down
# (Kannuki and Mikado against Gengoro and Kaun); the stage. The first fight
# the first player's, his leg wounded on the way (and the second's, who is
# out): Kannuki stays with the wound against Kaun, who comes in unhurt;
# the second fight the first player's too, the match won, its card, and
# the top menu.
TOML_RB_VS_CREW = 'about = "Versus mode, the rule first: Crew 2, a wounded winner, the top menu ' \
    '(tools/port_match.py)"\n' + CREW_HEAD + '''input = [%(rb_opening)s,
    %(rb_vs_menu)s,
    %(rule_crew)s,
    %(first_picks)s,
    %(place2)s,
    "stages+40 CROSS",
    %(presses)s,
]
''' + RB_COMPARE + RB_START + RB_SELECT + \
    RB_RULE.replace('GRID_EXPECT', '"port_match_page_rule == 3", "port_match_page_crew == 2", '
                    '"port_match_crew_size == 2", "port_match_crew_sides == 2", "port_match_crew_picked == 0"') + \
    rb_crew_place(2, 2) + '''
[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
expect = ["port_match_crew_picked == 2", "port_crew_p1_1 == %(p1)d", "port_crew_p1_2 == %(p1_2)d",
          "port_crew_p2_1 == %(p2)d", "port_crew_p2_2 == %(p2_2)d", "port_match_page_rule == 3"]
''' + \
    wounded(fight('fight', ['port_match_rule == 3', 'port_match_count == 2', 'p1_character == %(p1)d',
                            'p2_character == %(p2)d', 'p1_leg_wounds == 0', 'port_match_kept_p1 == 0'],
                  'card', ['port_match_card == 1', 'port_match_last == 0', 'port_match_lives_p2 == 1',
                           'port_match_kept_p1 == 1', 'port_match_legs_p1 == 1', 'port_match_kept_p2 == 0',
                           'port_match_legs_p2 == 0']), 'card') + \
    fight('fight2', ['p1_character == %(p1)d', 'p2_character == %(p2_2)d', 'p1_leg_wounds == 1',
                     'p2_leg_wounds == 0', 'p1_defeated == 0', 'p2_defeated == 0', 'p1_off_arm_ok == 1'],
          'result', ['port_match_card == 2', 'port_match_over == 1', 'port_match_winner == 0',
                     'port_match_lives_p2 == 0', 'port_match_kept_p1 == 0']).replace('within = 6000',
                                                                                     'within = 9000') + \
    TOP

# Versus the CPU, Crew 3 on the rule page (Right three times, Down, Left
# twice: five to three), no stage page: the player's crew picked at the
# grid (the second and third places refused the fighters the crew has
# first), the CPU's three all different characters, led by the game's
# opponent. As the port plays today the CPU takes the player's fighters
# one after another: the match lost, its card, and the top menu, not the
# game's menu after a defeat.
TOML_RB_VSCPU_CREW = 'about = "Versus the CPU, the rule first: Crew 3, the CPU\'s crew all different, the top ' \
    'menu (tools/port_match.py)"\n' + CREW_HEAD.replace('limit = 14000', 'limit = 20000') + \
    '''input = [%(rb_opening)s,
    %(rb_vscpu_menu)s,
    %(rule_crew3)s,
    %(first_pick_cpu)s,
    %(place2_cpu)s,
    %(place3_cpu)s,
    %(presses)s,
]
''' + RB_COMPARE + RB_START + RB_SELECT + \
    RB_RULE.replace('GRID_EXPECT', '"port_match_page_rule == 3", "port_match_page_crew == 3", '
                    '"port_match_crew_size == 3", "port_match_crew_sides == 1"') + \
    rb_crew_place(2, 3) + rb_crew_place(3, 3) + '''
[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
at_frame = true
within = 3000
expect = ["mode == 1", "port_match_rule == 3", "port_match_count == 3", "port_crew_p1_1 == %(p1)d",
          "port_crew_p1_2 == %(p1_2)d", "port_crew_p1_3 == %(p1_3)d", "port_crew_p2_1 != port_crew_p2_2",
          "port_crew_p2_1 != port_crew_p2_3", "port_crew_p2_2 != port_crew_p2_3", "p1_character == %(p1)d"]

[[checkpoint]]
name = "result"
when = "port_match_card == 2"
within = 14000
expect = ["port_match_over == 1", "port_match_winner == 1", "port_match_lives_p1 == 0"]
''' + TOP

# Versus mode, Stock 2 on the rule page (Right, Down, Left: three to two):
# the first fight the first player's, both legs wounded on the way: the
# winner comes into the second fight with his, the loser of a life unhurt;
# the second fight the first player's too, the result, and the stage page
# with the fighters kept, as Stock goes on.
TOML_RB_VS_STOCK = 'about = "Versus mode, the rule first: Stock 2, a wounded winner (tools/port_match.py)"\n' + \
    HEAD + '''input = [%(rb_opening)s,
    %(rb_vs_menu)s,
    %(rule_stock)s,
    %(first_picks)s,
    "stages+40 CROSS",
    %(presses_stock)s,
]
''' + RB_COMPARE + RB_START + RB_SELECT + \
    RB_RULE.replace('GRID_EXPECT', '"port_match_page_rule == 1", "port_match_page_count == 2"') + '''
[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false
''' + \
    wounded(fight('fight', ['port_match_rule == 1', 'port_match_count == 2', 'p1_leg_wounds == 0'],
                  'card', ['port_match_card == 1', 'port_match_lives_p2 == 1', 'port_match_kept_p1 == 1',
                           'port_match_legs_p1 == 1', 'port_match_kept_p2 == 0']), 'card') + \
    fight('fight2', SAME + ['p1_leg_wounds == 1', 'p2_leg_wounds == 0', 'p1_defeated == 0', 'p2_defeated == 0'],
          'result', ['port_match_card == 2', 'port_match_over == 1', 'port_match_winner == 0']) + '''
[[checkpoint]]
name = "back"
when = "port_on_stages == 1"
within = 3000
expect = ["menu_page == MENU_PAGE_SELECT", "port_match_over == 1", "port_match_to_top == 0"]
'''


def wounding_presses():
    """Both pads' presses in a fight whose winner ends it wounded, with no
    poke (a rollback and a replay hold only what the pads did): the second
    player walks in and strikes with Cross for 260 frames, the first
    waiting, then the first walks in and strikes with Cross until one
    falls (tools/port_net.py's wounding_fight); in the fight after, the
    first pad's walk and low strikes, the second pressing nothing."""
    presses = []

    def strikes(start, end, every, button, pad, walk):
        at, n = start, 0
        while at < end:
            if n % 3 == 0:
                presses.append((at, pad, walk))
                at += 22
            presses.append((at, pad, button))
            n += 1
            at += every

    strikes(0, 260, 35, 'CROSS', 1, 'LEFT hold 20f')
    strikes(300, 6000, 30, 'CROSS', 0, 'RIGHT hold 20f')
    first = ['"fight+%df %s%s"' % (at, 'P2 ' if pad else '', button) for at, pad, button in sorted(presses)]
    second = ['"fight2+%df %s"' % (n * 60 + offset, button) for n in range(66)
              for offset, button in ((0, 'RIGHT hold 20f'), (25, 'CIRCLE'), (40, 'CIRCLE'))]
    return ', '.join(first + second)


# what the first fight's winner takes into the second: some wound, and at
# the second's start his counts the match's, the other's none
WOUNDED = ('(port_match_legs_p%(s)d + port_match_arm_p%(s)d + port_match_body_p%(s)d > 0 || '
           'port_match_off_arm_p%(s)d == 0)')
WOUND_CARD = ['port_match_card == 1', 'port_match_kept_p1 + port_match_kept_p2 == 1',
              'port_match_kept_p1 == 0 || ' + WOUNDED % {'s': 1}, 'port_match_kept_p2 == 0 || ' + WOUNDED % {'s': 2}]
WOUND_SECOND = [
    'port_match_kept_p%(s)d == 0 || (p%(s)d_leg_wounds == port_match_legs_p%(s)d && p%(s)d_arm_wounds == '
    'port_match_arm_p%(s)d && p%(s)d_body_wounds == port_match_body_p%(s)d && p%(s)d_off_arm_ok == '
    'port_match_off_arm_p%(s)d)' % {'s': s} for s in (1, 2)] + [
    'port_match_kept_p%(s)d == 1 || (p%(s)d_leg_wounds + p%(s)d_arm_wounds + p%(s)d_body_wounds == 0 && '
    'p%(s)d_off_arm_ok == 1)' % {'s': s} for s in (1, 2)]

# Versus mode, the rule first, Crew 2 and Stock 2, both pads' presses
# (wounding_presses), no poke: the first fight's winner wounded into the
# second, every pick and press the pads'. tools/port_replay.py plays their
# fights back from their replays and port-rollback rolls them back.
TOML_RB_VS_CREW_WOUNDS = 'about = "Versus mode, the rule first: Crew 2, the winner wounded by the pads ' \
    '(tools/port_match.py)"\n' + CREW_HEAD + '''input = [%(rb_opening)s,
    %(rb_vs_menu)s,
    %(rule_crew)s,
    %(first_picks)s,
    %(place2)s,
    "stages+40 CROSS",
    %(wounding)s,
]
''' + RB_COMPARE + RB_START + RB_SELECT + RB_RULE.replace('GRID_EXPECT', '"port_match_page_rule == 3"') + \
    rb_crew_place(2, 2).split('\n[[checkpoint]]\nname = "dup2"')[0] + '''
[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false
''' + fight('fight', ['port_match_rule == 3', 'port_match_kept_p1 == 0'], 'card', WOUND_CARD).replace(
        'within = 6000', 'within = 14000') + \
    fight('fight2', WOUND_SECOND, 'result', ['port_match_card == 2', 'port_match_over == 1']).replace(
        'within = 6000', 'within = 9000') + TOP
TOML_RB_VS_STOCK_WOUNDS = 'about = "Versus mode, the rule first: Stock 2, the winner wounded by the pads ' \
    '(tools/port_match.py)"\n' + CREW_HEAD + '''input = [%(rb_opening)s,
    %(rb_vs_menu)s,
    %(rule_stock)s,
    %(first_picks)s,
    "stages+40 CROSS",
    %(wounding)s,
]
''' + RB_COMPARE + RB_START + RB_SELECT + RB_RULE.replace('GRID_EXPECT', '"port_match_page_rule == 1"') + '''
[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false
''' + fight('fight', ['port_match_rule == 1', 'port_match_kept_p1 == 0'], 'card', WOUND_CARD).replace(
        'within = 6000', 'within = 14000') + \
    fight('fight2', WOUND_SECOND, 'result', ['port_match_card == 2', 'port_match_over == 1']).replace(
        'within = 6000', 'within = 9000')
# (also tools/port_replay.py's and port-rollback's)
WOUND_PLANS = [
    ('rb_vs_crew_wounds', TOML_RB_VS_CREW_WOUNDS, RB_ARGS + ['--match', 'single']),
    ('rb_vs_stock_wounds', TOML_RB_VS_STOCK_WOUNDS, RB_ARGS + ['--match', 'single']),
]

# Versus the CPU, the CPU's level chosen on the rule page (the single
# fight's rows are the rule, the fighters and the CPU: Down twice, Right
# twice from the setting's Original to Adept): the choice held for the
# fight, which the port's CPU plays (its --cpu-log says so).
RB_LEVEL_COMPARE = RB_COMPARE.replace('"port_match_to_top"]', '"port_match_to_top", "port_match_page_cpu", '
                                      '"port_match_cpu_taken", "port_match_cpu_row"]')
TOML_RB_VSCPU_LEVEL = 'about = "Versus the CPU, the CPU\'s level on the rule page (tools/port_match.py)"\n' + \
    HEAD + '''input = [%(rb_opening)s,
    %(rb_vscpu_menu)s,
    "rule+30 DOWN", "rule+50 DOWN", "rule+70 RIGHT", "rule+90 RIGHT", "rule+130 CROSS",
    %(first_pick_cpu)s,
]
''' + RB_LEVEL_COMPARE + RB_START + RB_SELECT + \
    RB_RULE.replace('"port_match_page_rule == 0"]', '"port_match_page_rule == 0", "port_match_cpu_row == 1", '
                    '"port_match_page_cpu == 0", "port_match_cpu_taken == 0"]').replace(
        'GRID_EXPECT', '"port_match_page_rule == 0", "port_match_page_cpu == 2", "port_match_cpu_taken == 1"') + '''
[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
at_frame = true
within = 3000
expect = ["mode == 1", "port_match_page_cpu == 2", "port_match_cpu_taken == 1"]

[[checkpoint]]
name = "fought"
after = 300
shot = false
'''

# The story: the page of the CPU's row alone, before the grid (Right four
# times: Master), held for the screen's fights. The story's narration
# first (the menu's page 4), which Cross ends.
RB_STORY_MENU = '"menu+30 CROSS", "menu+150 CROSS", "narration+90 CROSS"'
RB_NARRATION = '''
[[checkpoint]]
name = "narration"
when = "menu_page == 4"
within = 1500
shot = false
'''
TOML_RB_STORY_LEVEL = 'about = "The story, the CPU\'s level on a page of its own (tools/port_match.py)"\n' + \
    HEAD + '''input = [%(rb_opening)s,
    %(rb_story_menu)s,
    "rule+30 RIGHT", "rule+50 RIGHT", "rule+70 RIGHT", "rule+90 RIGHT", "rule+130 CROSS",
]
''' + RB_LEVEL_COMPARE + RB_START + RB_NARRATION + RB_SELECT + \
    RB_RULE.replace('"port_match_rule_first == 1", ', '"port_match_rule_first == 0", "port_match_rule_row == 3", ')\
    .replace('"port_match_page_rule == 0"]', '"port_match_cpu_row == 1", "port_match_page_cpu == 0"]')\
    .replace('GRID_EXPECT', '"port_match_page_cpu == 4", "port_match_cpu_taken == 1", "port_match_rule_first == 0"')


def rb_values():
    """The rule-first plans' values."""
    return dict(rb_opening=', '.join(RB_OPENING), rb_vs_menu=RB_VS_MENU, rb_vscpu_menu=RB_VSCPU_MENU,
                rule_crew=rule_presses(3, 3), rule_crew3=rule_presses(3, 2), rule_stock=rule_presses(1, 1),
                rb_story_menu=RB_STORY_MENU, first_picks=RB_FIRST_PICKS, first_pick_cpu=RB_FIRST_PICK_CPU, place2=rb_place(2),
                place2_cpu=rb_place(2, True), place3_cpu=rb_place(3, True), wounding=wounding_presses())


# rb_vscpu_level's log of the CPU's pads (the second run's: each size's
# run writes it in turn)
CPU_LOG = os.path.join(REPO, 'build', 'match', 'rb_vscpu_level-cpu.txt')
RB_PLANS = [
    ('rb_vs_crew', TOML_RB_VS_CREW, RB_ARGS + ['--match', 'single'], wound_pokes('card_wound')),
    ('rb_vscpu_crew', TOML_RB_VSCPU_CREW, RB_ARGS + ['--unlock-all', '--match', 'single']),
    ('rb_vs_stock', TOML_RB_VS_STOCK, RB_ARGS + ['--match', 'single'], wound_pokes('card_wound')),
    ('rb_vscpu_level', TOML_RB_VSCPU_LEVEL, RB_ARGS + ['--unlock-all', '--match', 'single', '--cpu-log', CPU_LOG]),
    ('rb_story_level', TOML_RB_STORY_LEVEL, RB_ARGS + ['--match', 'single']),
] + WOUND_PLANS

# versus mode's first fighters (the grid's first place and the second
# cursor's) on the second page's first stage (Building Rooftop); versus
# the CPU's first opponent of the run as an empty save and the stage pages
# have it; the crews' later places as CREW_PICKS moves the cursors
VALUES = {'menu': VS_MENU, 'vscpu': VSCPU_MENU, 'vscpu_crew': VSCPU_CREW_MENU, 'stage': 29, 'p1': 0, 'p2': 12,
          'cpu': 14, 'cpu_crew': 9, 'cpu_crew_2': 6, 'picks': CREW_PICKS, 'p1_2': 2, 'p2_2': 13, 'p1_3': 1, 'p2_3': 14}
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
                        'vs_crew_drawn, vscpu_crew, rb_vs_crew, rb_vscpu_crew, rb_vs_stock, rb_vscpu_level, '
                        'rb_story_level, group_stock, '
                        'group_first_to, group_crew)')
    parser.add_argument('--rollback', action='store_true',
                        help="vs_stock's match through tools/port_rollback.py instead: rolled back every "
                        "24 frames, on both sizes (and a Crew 2 match, and the rule-first Crew and Stock "
                        "matches whose winner is wounded)")
    args = parser.parse_args(argv[1:])
    import playtest
    report = Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    values = dict(VALUES, opening=', '.join(OPENING), presses=fight_presses(),
                  presses_crew=fight_presses(CREW_ROUNDS), presses_stock=fight_presses(STOCK_ROUNDS),
                  group_menu=group_menu(), group_presses=group_presses(), **rb_values())
    plans = [
        ('vs_stock', TOML_VS_STOCK, ['--mods', 'off', '--match', 'stock:2']),
        ('vs_first_to', wounded(TOML_VS_FIRST_TO, 'card'), ['--mods', 'off', '--match', 'stock:1'],
         wound_pokes('card_wound', both=False)),
        ('vscpu_stock', TOML_VSCPU_STOCK, ['--mods', 'off', '--stage-select', '--unlock-all', '--match', 'stock:2']),
        ('vscpu_won', TOML_VSCPU_WON, ['--mods', 'off', '--stage-select', '--unlock-all', '--match', 'stock:1']),
    ] + CREW_PLANS + RB_PLANS + group_plans()
    if args.rollback:
        # the Stock match, and a Crew match, its crews picked and its
        # fighters loaded afresh between fights
        code = 0
        for name, text, extra in [plans[0], CREW_REPLAY_PLAN] + WOUND_PLANS:
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
        if name == 'rb_vscpu_level':
            # the second fighter is the port's CPU (port_cpu.h): kind 2
            with open(CPU_LOG, encoding='utf-8') as f:
                kinds = set(line.split()[3] for line in f if line.startswith('f ') and line.split()[2] == '1')
            report.check(kinds == {'2'}, '%s: the CPU\'s fighter is played by the port\'s CPU (%s)'
                         % (name, sorted(kinds)))
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
