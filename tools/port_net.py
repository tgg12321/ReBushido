#!/usr/bin/env python3
"""Two programs of the native port playing each other over UDP on this PC
(docs/NETPLAY.md, sections 11 and 12), headless:

    python3 tools/port_net.py link|rollback|session|window [--bits 32|64]
                              [--out FOLDER] [--latency MS] [--loss PERCENT]
                              [--delay N] [--port-base PORT] [--stress N]
                              [--burn K] [--cpus LIST] [--pause MS]
    python3 tools/port_net.py all [--port-base PORT]    the four at once,
                              and the lobby's ways (tools/port_net_lobby.py
                              headless, its ports LOBBY_BELOW under the base)

`link`: the game's own link-cable versus mode, in lockstep with a frame of
delay. Each program is one console (--link-side 0 and 1) with its own plan:
both go from power on to the top menu's link row as port_game's plan goes
to versus; then the first console's pad picks the first player's fighter,
weapon and stage and fights as port_game's first pad, and the second
console's pad picks and fights as port_game's second. It passes if both
reach the end of the fight and the two traces are the same, line for
line.

`rollback`: rollback netplay (--netplay), the whole session from power on
as one machine with two pads, each program one pad. Both follow
port_game's plan, each pressing only its own pad's part of it, through
the menus, a versus fight to the kill, the replay and back to the menu,
over a network made worse on purpose: 100 ms of latency each way and 5
datagrams in a hundred lost if nothing says otherwise. It passes if both
end well, both see the kill, neither finds a desync, and every state hash
both print (one each 30 frames, once both pads' inputs before it are
known) is the same on both, to the last frame they share near the end.

`inprocess`: sessions begun in the running program, with no start again
(docs/NETPLAY.md, section 15; --session-at), held to a session of a
program started for it: the first frame's hash and every one after the
same with the same seed, two sessions one after the other in one process,
and a guest turned away that goes on with its own game.

`session` and `window` are a session's, set up by --host and --join
(section 13). `session` also holds the two players to the same arena
(docs/MODS.md, "Custom arenas"): the sample arena compiled at test time,
the host with it in play (--arena) and the guest without, which the host
must turn away. And two more headless sessions of each size, begun in the
running program with ReBushido's top menu, play a Crew match of two and a
Stock match of two with the rule chosen by the host before the first
select (PORT.md, "Match rules", "The rule first"): the crews of different
fighters, the first fight's winner wounded into the second, the Crew
match's end the top menu, where the session ends on both.

Under load (docs/NETPLAY.md, section 12, "Under load"): `--stress N` runs
the checks N times over and counts the runs that failed and those that
found a desync, keeping each run's folder; `--burn K` starts K programs
that only burn a processor meanwhile, `--cpus LIST` puts the two programs
and the burners on those processors alone (taskset), and `--pause MS`
stops one program or the other for MS milliseconds every second or so, so
that the other runs ahead and the stopped one wakes to a burst of
datagrams.

Both sockets are bound to 127.0.0.1, on two ports from 47000 up
(--port-base moves them, so that two runs at once do not meet; each run
takes PORTS_USED from its base, and `all` gives link, rollback, session
and window bases that far apart): nothing
listens on any other address. The programs are the ones `make port` built
(build them again first), and the retail disc's image is needed
(REBUSHIDO_RETAIL, or the .cue at the checkout's root); no emulator.
Without the image it says so and passes, as the play tests do."""
import argparse
import os
import random
import shutil
import signal
import subprocess
import sys
import threading
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

import playtest  # noqa: E402  (tools/playtest.py)

PROGRAMS = {64: 'build/port/rebushido', 32: 'build/port32/rebushido'}
WINDOW_PROGRAM = 'build/port-sdl/linux/rebushido'
SECONDS = 600
BASE_PORT = 47000
# The ports one run may take from its base: two for each size, ten apart
# for a session, and four more for the window's.
PORTS_USED = 24
LOCALHOST = '127.0.0.1'
def window_env(env):
    """`env` for a window's program, held to CLAUDE.md's rule: no visible
    window and no sound, ever (SDL's dummy video, or offscreen, and dummy
    audio). A run without them is refused, not started."""
    if env.get('SDL_VIDEODRIVER') not in ('dummy', 'offscreen') or env.get('SDL_AUDIODRIVER') != 'dummy':
        raise SystemExit('port_net: refused to start a window program without SDL_VIDEODRIVER=dummy (or offscreen) '
                         'and SDL_AUDIODRIVER=dummy')
    return env


def window_cmd(cmd):
    """A window's program's command line with --hidden, which keeps its
    window from ever being shown and its sound on SDL's dummy device
    whatever the environment says (sdl_window.c)."""
    return cmd if '--hidden' in cmd else cmd[:1] + ['--hidden'] + cmd[1:]


def heard_only_dummy(text):
    """None if every sound driver a window's log names is the dummy one,
    else the one it named."""
    import re
    for driver in re.findall(r'sound: driver (\S+)', text):
        if driver.rstrip(',') != 'dummy':
            return driver.rstrip(',')
    return None


# SDL's controllers turned off for the window's programs: a controller
# plugged into the PC would otherwise be read by the hidden windows too,
# and its drift pressed into their scripted fights (SDL's hints, as
# environment variables: every backend of Windows' and Linux's).
NO_CONTROLLERS = {'SDL_JOYSTICK_HIDAPI': '0', 'SDL_JOYSTICK_RAWINPUT': '0', 'SDL_XINPUT_ENABLED': '0',
                  'SDL_JOYSTICK_WGI': '0', 'SDL_DIRECTINPUT_ENABLED': '0', 'SDL_JOYSTICK_LINUX_CLASSIC': '0',
                  'SDL_JOYSTICK_DEVICE': ''}
# --cpus: what each program is started under (taskset), or nothing
PINNED = []

# From power on to the top menu, as port_game's plan (tests/play/
# port_game.toml); then five rows down (versus is three: the rows are the
# modes, and slash mode's is shut) to the link cable's, and take it.
TO_THE_LINK = [
    "page+30 START",
    "movie+300 START", "movie+600 CROSS", "movie+900 START",
    "menu+30 DOWN", "menu+60 DOWN", "menu+90 DOWN", "menu+120 DOWN", "menu+150 DOWN",
    "menu+210 CROSS",
]

# port_game's presses in the select screen and the fight, the first pad's
# and the second's (P2): each console presses its own pad, which is the
# first, and the link makes side 0's the first player's and side 1's the
# second's on both.
# The stage's first cell, the castle's rooftop in versus, is another stage
# over the link (select.c, SELECT_STAGE_LINK_TO), where the two start far
# apart; the first console takes the cell beside it.
SELECT = ["select+280 CROSS", "select+320 P2 CROSS", "select+580 CROSS", "select+620 P2 CROSS",
          "select+1150 RIGHT", "select+1180 CROSS"]
# The link's fight is seen in the first person from each side
# (fight_start.c): both walk forward a little, then strike on port_game's
# beats.
FIGHT = ["fight+50f UP hold 20f", "fight+50f P2 UP hold 10f"]
FIGHT += ["fight+%df %s" % (72 + 30 * i, "CIRCLE" if i % 2 == 0 else "CROSS") for i in range(28)]
FIGHT += ["fight+%df P2 %s" % (65 + 35 * i, "SQUARE" if i % 2 else "TRIANGLE") for i in range(24)]

CHECKPOINTS = '''
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
within = 3000
shot = false
expect = ["mode == FIGHT_MODE_NETWORK"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 4000
shot = false
expect = ["mode == FIGHT_MODE_NETWORK", "p1_cpu == 0", "p2_cpu == 0"]

[[checkpoint]]
name = "kill"
when = "winner != FIGHT_NOBODY"
at_frame = true
within = 3000
shot = false
expect = ["p1_defeated + p2_defeated == 1"]

[[checkpoint]]
name = "end"
frames = 60
shot = false
'''


# A session (--host, --join): the session itself takes both programs from
# power on to the versus select screen. There each player takes the
# fighter and the weapon under the cursor, and the host the stage; then
# port_game's fight, each pad's part from its own program. After it the
# host takes the first row of the menu after the fight, Rematch, and the
# same fight is fought again. Then the guest ends (its plan is done): the
# host is told, and the window's plays on alone for ten seconds more,
# the headless one ends with it.
SESSION_SELECT = ["select+280 CROSS", "select+320 P2 CROSS", "select+580 CROSS", "select+620 P2 CROSS",
                  "select+1180 CROSS"]


def port_game_fight():
    """port_game's presses in the fight, as written there."""
    text = read(os.path.join(REPO, 'tests', 'play', 'port_game.toml'))
    presses = []
    for word in text.split('"'):
        if word.startswith('fight+'):
            presses.append(word)
    return presses


SESSION_CHECKPOINTS = '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 4000
shot = false
expect = ["mode == FIGHT_MODE_VS"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 4000
shot = false
expect = ["mode == FIGHT_MODE_VS", "p1_cpu == 0", "p2_cpu == 0"]

[[checkpoint]]
name = "kill"
when = "winner != FIGHT_NOBODY"
at_frame = true
within = 4000
shot = false
expect = ["p1_defeated + p2_defeated == 1"]

[[checkpoint]]
name = "after_fight"
when = "screen == SCREEN_AFTER_FIGHT_UPDATE"
within = 4000
shot = false

[[checkpoint]]
name = "rematch"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
at_frame = true
within = 3000
shot = false
expect = ["mode == FIGHT_MODE_VS", "wins_p1 + wins_p2 == 1"]

[[checkpoint]]
name = "kill2"
when = "winner != FIGHT_NOBODY"
at_frame = true
within = 4000
shot = false
expect = ["p1_defeated + p2_defeated == 1"]
'''
# The guest ends a second after the second kill; the host runs on.
SESSION_END = {0: '\n[[checkpoint]]\nname = "alone"\nafter = 900\nshot = false\n',
               1: '\n[[checkpoint]]\nname = "end"\nframes = 30\nshot = false\n'}


# The headless session plays a Stock match of two lives (docs/PORT.md,
# "Match rules"): the host's --match, which the guest is not given and
# takes from the welcome. After the first kill the card of the score
# stands in for the menu, and the second fight starts by itself; the host's
# Cross after it only takes the card down sooner.
SESSION_MATCH = ['--match', 'stock:2']
SESSION_MATCH_EXPECT = {
    'after_fight': 'expect = ["port_match_rule == 1", "port_match_card == 1", "port_match_fights == 1"]\n',
    'rematch': 'expect = ["mode == FIGHT_MODE_VS", "wins_p1 + wins_p2 == 1", "port_match_rule == 1", '
               '"port_match_count == 2", "port_match_lives_p1 + port_match_lives_p2 == 3"]\n',
}
# what each kind of session's match is: the host's arguments, what its
# checkpoints expect, what both sides' logs say of the rule
SESSION_MATCHES = {
    'stock': (SESSION_MATCH, SESSION_MATCH_EXPECT, "session: the host's match rule is stock, 2"),
}


def session_checkpoints(match):
    """SESSION_CHECKPOINTS, with what the match (a key of SESSION_MATCHES)
    expects."""
    if not match:
        return SESSION_CHECKPOINTS
    text = SESSION_CHECKPOINTS
    for name, expect in SESSION_MATCHES[match][1].items():
        head = 'name = "%s"\n' % name
        start = text.index(head)
        end = text.index('\n\n', start) if '\n\n' in text[start:] else len(text)
        block = '\n'.join(line for line in text[start:end].split('\n') if not line.startswith('expect')) + '\n'
        text = text[:start] + block + expect + text[end:]
    return text


def session_match_problems(label, who, err, out, plan, match='stock'):
    """The match of a headless session (a key of SESSION_MATCHES): each
    side holds the host's rule (the guest was given none), and where a
    checkpoint is printed its score is the one expected (Stock: one life
    lost of four at the second fight)."""
    problems = []
    _, expects, said = SESSION_MATCHES[match]
    if said not in err:
        problems.append('session: %s: the %s did not hold the host\'s match rule (%s)' % (label, who, said))
    names = [line.split()[1] for line in read(plan).split('\n') if line.startswith('var ')]
    for line in out.split('\n'):
        words = line.split()
        if len(words) > 3 and words[0] == 'cp' and words[1] in expects:
            values = dict(playtest.header_names())
            values.update(zip(names, (int(v) for v in words[3:])))
            claims = expects[words[1]].split('"')[1::2]
            for claim in claims:
                if not playtest.evaluate(claim, values):
                    problems.append('session: %s: the %s at `%s`: not %s' % (label, who, words[1], claim))
    return problems


def write_session_plan(program, side, folder, match=None):
    """The plan of the host (side 0) or the guest (1) for `program`: its
    path. `match`: the session plays that of SESSION_MATCHES."""
    fight = port_game_fight()
    rematch = [p.replace('fight+', 'rematch+', 1) for p in fight]
    presses = own_pad(SESSION_SELECT + fight + rematch, side)
    if side == 0:
        presses.append("after_fight+%d CROSS" % (30 if match else 120))
    toml = os.path.join(folder, 'session_side%d.toml' % side)
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write('about = "A session of netplay, side %d."\n' % side)
        # (the cards are the host's own, as the command line names them)
        f.write('discs = ["port"]\nlimit = 16000\nseconds = %d\n' % SECONDS)
        f.write('input = [\n%s\n]\n' % ',\n'.join('    "%s"' % p for p in presses))
        f.write(session_checkpoints(match) + SESSION_END[side])
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), 1)
    path = os.path.join(folder, 'plan_side%d.txt' % side)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return path


# A session that goes to the versus select screen and back: the guest's
# Triangle there, with no fighter picked, takes both back to the top menu,
# which ends a session begun in the running program on both
# (docs/NETPLAY.md, section 15). Then each waits.
LEAVE_PRESSES = ["select+300 P2 TRIANGLE"]
LEAVE_CHECKPOINTS = '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 6000
shot = false

[[checkpoint]]
name = "stay"
after = 30000
shot = false
'''


def write_leave_plan(program, side, folder):
    """The plan of side `side` for a session that goes to the select screen
    and back to the top menu: its path."""
    toml = os.path.join(folder, 'leave_side%d.toml' % side)
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write('about = "A session to the select screen and back, side %d."\n' % side)
        f.write('discs = ["port"]\nlimit = 60000\nseconds = %d\n' % SECONDS)
        f.write('input = [\n%s\n]\n' % ',\n'.join('    "%s"' % p for p in own_pad(LEAVE_PRESSES, side)))
        f.write(LEAVE_CHECKPOINTS)
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), 1)
    path = os.path.join(folder, 'leave_plan_side%d.txt' % side)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return path


# A room of several's match (docs/NETPLAY.md, section 16): one plan for
# every member, both pads' presses in it, and each player presses only its
# place's (--net-plan-pads). The session's start puts the plan back with
# the state of the game's start, so that each match plays it from its
# beginning; the session ends where the fight is decided, at the menu
# after it. Nothing is given up in the member's own play between matches,
# however long, and the plan never ends: the room ends the program
# (--room-matches).
ROOM_CHECKPOINTS = '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 100000000
shot = false

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 100000000
shot = false

[[checkpoint]]
name = "kill"
when = "winner != FIGHT_NOBODY"
at_frame = true
within = 100000000
shot = false

[[checkpoint]]
name = "after_fight"
when = "screen == SCREEN_AFTER_FIGHT_UPDATE"
within = 100000000
shot = false

[[checkpoint]]
name = "stay"
after = 100000000
shot = false
'''


# A room's match of Crew 2 (--match crew:2), whose rule is the room's: no
# rule page, the crews picked at the grid from the first place (the rule
# first, docs/PORT.md, "Match rules"), each player's second place one
# place down after a Cross the select refuses, then the stage. The first
# fight is wounding_fight's, whose winner keeps his wounds into the
# second, finishing_fight's, the loser's next coming in unhurt; the match decided at its
# result's card, which ends the room's session, back to the room's page.
ROOM_CREW_PRESSES = ["select+280 CROSS", "select+320 P2 CROSS", "select+580 CROSS", "select+620 P2 CROSS",
                     "crew2+60 CROSS", "crew2+66 P2 CROSS", "crew2+100 DOWN", "crew2+110 P2 DOWN",
                     "crew2+140 CROSS", "crew2+150 P2 CROSS", "crew2+340 CROSS", "crew2+380 P2 CROSS",
                     "stages+40 CROSS"]
ROOM_CREW_CHECKPOINTS = ROOM_CHECKPOINTS.replace('''
[[checkpoint]]
name = "fight"''', '''
[[checkpoint]]
name = "crew2"
when = "port_match_crew_picked == 1"
within = 100000000
shot = false
expect = ["port_match_rule_first == 1", "port_match_rule_page == 0", "port_match_crew_size == 2"]

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 100000000
shot = false
expect = ["port_match_crew_picked == 2", "port_crew_p1_1 != port_crew_p1_2", "port_crew_p2_1 != port_crew_p2_2"]

[[checkpoint]]
name = "fight"''').replace('''
[[checkpoint]]
name = "after_fight"''', '''
[[checkpoint]]
name = "card"
when = "port_match_card == 1"
within = 100000000
shot = false
expect = ["port_match_fights == 1", "port_match_kept_p1 + port_match_kept_p2 == 1",
          "port_match_kept_p1 == 0 || WOUNDED_P1", "port_match_kept_p2 == 0 || WOUNDED_P2"]

[[checkpoint]]
name = "fight2"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
at_frame = true
within = 100000000
shot = false
expect = ["port_match_kept_p1 == 0 || (p1_leg_wounds == port_match_legs_p1 && p1_arm_wounds == port_match_arm_p1 && p1_body_wounds == port_match_body_p1 && p1_off_arm_ok == port_match_off_arm_p1)",
          "port_match_kept_p2 == 0 || (p2_leg_wounds == port_match_legs_p2 && p2_arm_wounds == port_match_arm_p2 && p2_body_wounds == port_match_body_p2 && p2_off_arm_ok == port_match_off_arm_p2)"]

[[checkpoint]]
name = "kill2"
when = "winner != FIGHT_NOBODY"
at_frame = true
within = 100000000
shot = false

[[checkpoint]]
name = "after_fight"''')


def write_room_plan(program, folder, crew=False):
    """The plan of every member of a room for `program`: its path. `crew`:
    a Crew 2 match's (ROOM_CREW_CHECKPOINTS)."""
    toml = os.path.join(folder, 'room.toml')
    fight = port_game_fight()
    presses = SESSION_SELECT + fight
    if crew:
        presses = ROOM_CREW_PRESSES + wounding_fight() + finishing_fight()
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write('about = "A room of several\'s matches, both pads."\n')
        f.write('discs = ["port"]\nlimit = 200000000\nseconds = %d\n' % SECONDS)
        f.write('input = [\n%s\n]\n' % ',\n'.join('    "%s"' % p for p in presses))
        f.write(ROOM_CREW_CHECKPOINTS.replace('WOUNDED_P1', WOUNDED % {'s': 1}).replace(
            'WOUNDED_P2', WOUNDED % {'s': 2}) if crew else ROOM_CHECKPOINTS)
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), 1)
    path = os.path.join(folder, 'room_plan.txt')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return path


def own_pad(presses, side):
    """The presses of side `side`'s console: its player's, on its own pad."""
    out = []
    for press in presses:
        words = press.split()
        is_second = len(words) > 1 and words[1] == 'P2'
        if is_second != (side == 1):
            continue
        out.append(' '.join(words[:1] + words[2:]) if is_second else press)
    return out


def write_plan(program, side, folder):
    """The plan of side `side`'s console for `program`, into `folder`: its
    path."""
    presses = TO_THE_LINK + own_pad(SELECT, side) + own_pad(FIGHT, side)
    toml = os.path.join(folder, 'link_side%d.toml' % side)
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write('about = "The link cable over UDP, side %d."\n' % side)
        f.write('discs = ["port"]\ncards = "none"\nlimit = 12000\nseconds = %d\n' % SECONDS)
        f.write('input = [\n%s\n]\n' % ',\n'.join('    "%s"' % p for p in presses))
        f.write(CHECKPOINTS)
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), 1)
    path = os.path.join(folder, 'plan_side%d.txt' % side)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return path


def read(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def first_difference(first, second):
    a, b = first.split('\n'), second.split('\n')
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return 'line %d: "%s" against "%s"' % (i + 1, x, y)
    return 'line %d: one ends there (%d lines against %d)' % (min(len(a), len(b)) + 1, len(a), len(b))


def run_pair(program, plans, folder, more, ports):
    """Both programs at once: [(exit code, seconds)]."""
    procs = []
    start = time.time()
    for side in (0, 1):
        cmd = [program, '--image', playtest.RETAIL_CUE, '--plan', plans[side], '--no-cards', '--trace',
               '--out', os.path.join(folder, 'side%d' % side),
               '--net-bind', '%s:%d' % (LOCALHOST, ports[side]),
               '--net-peer', '%s:%d' % (LOCALHOST, ports[1 - side])] + more[side]
        os.makedirs(os.path.join(folder, 'side%d' % side), exist_ok=True)
        out = open(os.path.join(folder, 'side%d.out' % side), 'wb')
        err = open(os.path.join(folder, 'side%d.err' % side), 'wb')
        procs.append((subprocess.Popen(PINNED + cmd, cwd=REPO, stdout=out, stderr=err, stdin=subprocess.DEVNULL),
                      out, err))
    pauser = Pauser([proc for proc, _, _ in procs])
    results = []
    for proc, out, err in procs:
        try:
            code = proc.wait(timeout=max(1, SECONDS - (time.time() - start)))
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            code = None
        out.close()
        err.close()
        results.append((code, time.time() - start))
    pauser.stop()
    return results


class Pauser:
    """--pause MS: one of the two programs, each in turn, stopped for MS
    milliseconds (SIGSTOP, SIGCONT) at times of a fixed sequence of random
    numbers, as a PC that is busy with something else stops a program; the
    other runs on to the frames it may guess and its datagrams wait, so
    that the stopped one wakes to a burst of them."""
    pause_ms = 0

    def __init__(self, procs):
        self.procs = procs
        self.done = threading.Event()
        self.thread = None
        if Pauser.pause_ms and hasattr(signal, 'SIGSTOP'):
            self.thread = threading.Thread(target=self.run, daemon=True)
            self.thread.start()

    def run(self):
        rng = random.Random(1)
        side = 0
        while not self.done.wait(rng.uniform(0.2, 1.5)):
            proc = self.procs[side]
            side = 1 - side
            if proc.poll() is not None:
                continue
            try:
                os.kill(proc.pid, signal.SIGSTOP)
                self.done.wait(Pauser.pause_ms / 1000.0)
                os.kill(proc.pid, signal.SIGCONT)
            except ProcessLookupError:
                pass

    def stop(self):
        self.done.set()
        if self.thread is not None:
            self.thread.join()


def hashes_of(text):
    """{frame: hash} of the `netplay: frame F hash H` lines."""
    found = {}
    for line in text.split('\n'):
        words = line.split()
        if len(words) == 5 and words[0] == 'netplay:' and words[1] == 'frame' and words[3] == 'hash':
            found[int(words[2])] = words[4]
    return found


def check_rollback(label, program, args, ports):
    folder = os.path.join(args.out, 'rollback', label)
    os.makedirs(folder, exist_ok=True)
    plan = os.path.join(folder, 'plan.txt')
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(os.path.join(REPO, 'tests', 'play', 'port_game.toml'))
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), test.discs.index('port') + 1)
    with open(plan, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    trouble = ['--net-delay', str(args.delay), '--net-latency', str(args.latency), '--net-loss', str(args.loss)]
    results = run_pair(program, [plan, plan], folder, [['--netplay', '1'] + trouble, ['--netplay', '2'] + trouble],
                       ports)
    problems = []
    hashes = []
    for side, (code, seconds) in enumerate(results):
        print('port_net: rollback: %s: pad %d: exit %s, %.0f s' % (label, side + 1, code, seconds), flush=True)
        err = read(os.path.join(folder, 'side%d.err' % side))
        for line in err.split('\n'):
            if line.startswith(('net:', 'netplay:')) and ' hash ' not in line:
                print('    ' + line)
        if 'DESYNC' in err:
            problems.append('rollback: %s: pad %d found a desync' % (label, side + 1))
        if code != 0:
            problems.append('rollback: %s: pad %d ended with %s (%s/side%d.err)' % (label, side + 1, code, folder,
                                                                                  side))
        if 'cp kill ' not in read(os.path.join(folder, 'side%d.out' % side)):
            problems.append('rollback: %s: pad %d never saw the kill' % (label, side + 1))
        hashes.append(hashes_of(err))
    common = sorted(set(hashes[0]) & set(hashes[1]))
    differ = [frame for frame in common if hashes[0][frame] != hashes[1][frame]]
    if differ:
        problems.append('rollback: %s: the state hashes differ from frame %d' % (label, differ[0]))
    ends = [max(found) if found else 0 for found in hashes]
    if not common or common[-1] + 60 < min(ends):
        problems.append('rollback: %s: too few state hashes in common (%d)' % (label, len(common)))
    if not problems:
        print('port_net: rollback: %s: %d state hashes, the same on both, the last at frame %d: %s'
              % (label, len(common), common[-1], hashes[0][common[-1]]), flush=True)
    return problems


def check_session(label, program, args, ports, window, match='stock'):
    """A session between two programs, set up by --host and --join, through
    the versus select screen, a fight, a rematch and the guest's leaving.
    `window`: the programs are the window's, headless (dummy drivers), in
    real time; or else the headless ones, as fast as they go."""
    folder = os.path.join(args.out, 'session', label)
    os.makedirs(folder, exist_ok=True)
    plans = [write_session_plan(program, side, folder, None if window else match) for side in (0, 1)]
    trouble = ['--net-latency', str(args.latency), '--net-loss', str(args.loss)]
    env = dict(os.environ)
    procs = []
    start = time.time()
    for side in (0, 1):
        cmd = [program, '--image', playtest.RETAIL_CUE, '--plan', plans[side], '--trace',
               '--out', os.path.join(folder, 'side%d' % side)] + trouble
        # each machine keeps the session's fights as replays (docs/PORT.md,
        # "Replays"), held to each other's below
        replays = os.path.join(folder, 'side%d-replays' % side)
        if os.path.isdir(replays):
            shutil.rmtree(replays)
        cmd += ['--replays', replays]
        if side == 0:
            # the host's cards, which it sends the guest: new ones here,
            # which no program writes (both play on copies in memory)
            cmd += ['--host', '%s:%d' % (LOCALHOST, ports[0]), '--net-delay', str(args.delay),
                    '--card1', os.path.join(folder, 'card1.mcd'), '--card2', os.path.join(folder, 'card2.mcd')]
            if not window:
                cmd += SESSION_MATCHES[match][0]
        else:
            cmd += ['--join', '%s:%d' % (LOCALHOST, ports[0]), '--net-bind', '%s:%d' % (LOCALHOST, ports[1])]
        if window:
            config = os.path.join(folder, 'rebushido.cfg')
            with open(config, 'w', encoding='utf-8', newline='\n') as f:
                f.write('fullscreen = off\n')
            # the loads hurried through, as a player's window has them
            # (docs/PORT.md, "Quick loading"): the two meet at each all
            # the same
            cmd += ['--config', config, '--windowed', '--log', os.path.join(folder, 'side%d.log' % side),
                    '--quick-loading', 'on']
            env['SDL_VIDEODRIVER'] = 'dummy'
            env['SDL_AUDIODRIVER'] = 'dummy'
            env.update(NO_CONTROLLERS)
        os.makedirs(os.path.join(folder, 'side%d' % side), exist_ok=True)
        out = open(os.path.join(folder, 'side%d.out' % side), 'wb')
        err = open(os.path.join(folder, 'side%d.err' % side), 'wb')
        procs.append((subprocess.Popen(PINNED + (window_cmd(cmd) if window else cmd), cwd=REPO, stdout=out,
                                       stderr=err, stdin=subprocess.DEVNULL,
                                       env=window_env(env) if window else env), out, err))
        if side == 0:
            time.sleep(0.5)
    pauser = Pauser([proc for proc, _, _ in procs])
    results = []
    for proc, out, err in procs:
        try:
            code = proc.wait(timeout=max(1, SECONDS - (time.time() - start)))
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            code = None
        out.close()
        err.close()
        results.append((code, time.time() - start))
    pauser.stop()
    problems = []
    hashes = []
    for side, (code, seconds) in enumerate(results):
        who = 'host' if side == 0 else 'guest'
        print('port_net: session: %s: %s: exit %s, %.0f s' % (label, who, code, seconds), flush=True)
        err = read(os.path.join(folder, 'side%d.err' % side))
        for line in err.split('\n'):
            if line.startswith(('net:', 'netplay:', 'session: begun', 'rebushido: sound: netplay')) and \
                    ' hash ' not in line:
                print('    ' + line)
        if 'DESYNC' in err:
            problems.append('session: %s: the %s found a desync' % (label, who))
        if code != 0:
            problems.append('session: %s: the %s ended with %s (%s/side%d.err)' % (label, who, code, folder, side))
        # A checkpoint is printed where a frame run first reaches it; one
        # that only a frame run again after a rollback reaches is not
        # (docs/NETPLAY.md, section 10). The plan goes from one to the
        # next, so a later one printed says the earlier were reached; and
        # a plan that ends well (exit 0) reached its last.
        out = read(os.path.join(folder, 'side%d.out' % side))
        order = ['select', 'fight', 'kill', 'after_fight', 'rematch', 'kill2', 'end', 'alone']
        printed = [n for n, name in enumerate(order) if 'cp %s ' % name in out]
        if not printed or max(printed) < order.index('kill'):
            problems.append('session: %s: the %s printed no checkpoint from the first kill on' % (label, who))
        else:
            print('    checkpoints printed: %s; the last reached: %s' % (
                ' '.join(order[n] for n in printed), order[max(printed)]))
        if not window:
            problems += session_match_problems(label, who, err, out, plans[side], match)
        hashes.append(hashes_of(err))
    host_err = read(os.path.join(folder, 'side0.err'))
    if 'the other program ended after frame' not in host_err:
        problems.append('session: %s: the host was not told that the guest had gone' % label)
    for side in (0, 1):
        driver = window and heard_only_dummy(read(os.path.join(folder, 'side%d.log' % side)))
        if driver:
            problems.append('session: %s: side %d played sound through %s, not the dummy driver' % (label, side, driver))
    if window and 'plays on alone' not in host_err:
        problems.append('session: %s: the host did not play on alone' % label)
    for card in ('card1.mcd', 'card2.mcd'):
        if os.path.exists(os.path.join(folder, card)):
            problems.append('session: %s: %s was written, and nothing of a session is' % (label, card))
    common = sorted(set(hashes[0]) & set(hashes[1]))
    differ = [frame for frame in common if hashes[0][frame] != hashes[1][frame]]
    if differ:
        problems.append('session: %s: the state hashes differ from frame %d' % (label, differ[0]))
    if len(common) < 40:
        problems.append('session: %s: too few state hashes in common (%d)' % (label, len(common)))
    if not problems:
        print('port_net: session: %s: %d state hashes, the same on both, the last at frame %d: %s'
              % (label, len(common), common[-1], hashes[0][common[-1]]), flush=True)
    problems += check_session_replays(label, program, folder, window)
    return problems


def check_session_replays(label, program, folder, window):
    """The session's fights as both machines kept them, a fight's two files
    found by the frame it began at: the same fights on each (but for the
    window's host, which plays on alone after the guest has gone and keeps
    that fight too), each fight's two files with the same inputs, views and
    hashes, and each played back by the headless program of the same size
    with every hash the one recorded (tools/port_replay.py)."""
    import port_replay
    problems = []
    files = []
    for side in (0, 1):
        replays = os.path.join(folder, 'side%d-replays' % side)
        names = sorted(f for f in os.listdir(replays) if f.endswith('.rbr')) if os.path.isdir(replays) else []
        files.append({port_replay.read_replay(os.path.join(replays, name))[1].get('start_count'):
                      os.path.join(replays, name) for name in names})
    common = sorted(set(files[0]) & set(files[1]), key=int)
    alone = set(files[0]) - set(files[1])
    if not common or set(files[1]) - set(files[0]) or (alone and not window):
        return ['session: %s: the two machines kept %d and %d replays, %d of the same fights'
                % (label, len(files[0]), len(files[1]), len(common))]
    for host, guest in ((files[0][start], files[1][start]) for start in common):
        a, b = port_replay.read_replay(host), port_replay.read_replay(guest)
        for chunk in ('INPT', 'HASH', 'VIEW'):
            if a[0].get(chunk) != b[0].get(chunk):
                problems.append('session: %s: %s and %s: their %s differ' % (label, host, guest, chunk))
        if a[1].get('start_hash') != b[1].get('start_hash'):
            problems.append('session: %s: %s and %s start otherwise' % (label, host, guest))
        if window:
            # the window's replays are held to each other's alone: the
            # headless program is another build
            continue
        for path in (host, guest):
            code, said = port_replay.check_replay(program, path, [])
            if code != 0:
                problems.append('session: %s: %s: %s' % (label, path, said))
    if not problems:
        print('port_net: session: %s: %d fights kept as replays on both machines, the same on both%s%s'
              % (label, len(common), '' if window else ', each played back with every hash the same',
                 ', and %d the host played on alone' % len(alone) if alone else ''),
              flush=True)
    return problems


# A session's Crew match with the rule first (docs/PORT.md, "Match rules",
# "The rule first"), begun in the running program (--session-at) with
# ReBushido's top menu (--menu rebushido): the host's pad turns the rule
# page to Crew 2 (the guest's Right there is not the session's: the host
# chooses), each player picks his crew at the grid, the second place first
# refused the first's fighter and then one place down, and the host the
# stage. The first fight wounding_fight's, whose winner keeps his wounds
# into the second, finishing_fight's, and the match decided, its card, the
# game goes back to the top menu, where the session ends on both machines
# at the same frame and each goes back to its own game.
SESSION_RB_CREW_PRESSES = [
    "rule+20 P2 RIGHT", "rule+30 RIGHT", "rule+42 RIGHT", "rule+54 RIGHT", "rule+76 DOWN", "rule+88 LEFT",
    "rule+100 LEFT", "rule+112 LEFT", "rule+150 CROSS",
    "grid+60 CROSS", "grid+70 P2 CROSS", "grid+260 CROSS", "grid+300 P2 CROSS",
    "crew2+60 CROSS", "crew2+66 P2 CROSS", "crew2+100 DOWN", "crew2+110 P2 DOWN", "crew2+140 CROSS",
    "crew2+150 P2 CROSS", "crew2+340 CROSS", "crew2+380 P2 CROSS",
    "stages+40 CROSS"]
# And the same with Stock 2 (the owner, 2026-11-20: "For stock battles,
# make injuries persist as well"): Right, Down, Left on the rule page,
# each player's fighter and weapon, the stage; the first fight's winner
# keeps his wounds into the second, the loser of a life comes back unhurt;
# the match decided, the stage page as Stock has it, and the guest ends.
SESSION_RB_STOCK_PRESSES = [
    "rule+20 P2 RIGHT", "rule+30 RIGHT", "rule+52 DOWN", "rule+64 LEFT", "rule+94 CROSS",
    "grid+60 CROSS", "grid+70 P2 CROSS", "grid+260 CROSS", "grid+300 P2 CROSS",
    "stages+40 CROSS"]
# what the winner of the first fight takes into the second: some wound
WOUNDED = ('(port_match_legs_p%(s)d + port_match_arm_p%(s)d + port_match_body_p%(s)d > 0 || '
           'port_match_off_arm_p%(s)d == 0)')
SESSION_RB_CREW_CHECKPOINTS = '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 6000
shot = false

[[checkpoint]]
name = "rule"
when = "port_match_rule_page == 1"
within = 600
shot = false
expect = ["port_match_rule_first == 1", "port_match_page_rule == 0"]

[[checkpoint]]
name = "grid"
when = "port_match_rule_page == 0"
within = 1000
shot = false
expect = [GRID_EXPECT]

CREW_PLACES
[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 16000
shot = false
expect = ["mode == FIGHT_MODE_VS", "port_match_rule == RULE", "port_match_lives_p1 == 2", "port_match_lives_p2 == 2"]

[[checkpoint]]
name = "kill"
when = "winner != FIGHT_NOBODY"
at_frame = true
within = 16000
shot = false

[[checkpoint]]
name = "card"
when = "port_match_card == 1"
within = 16000
shot = false
expect = ["port_match_fights == 1", "port_match_kept_p1 + port_match_kept_p2 == 1",
          "port_match_kept_p1 == 0 || %(wounded_p1)s", "port_match_kept_p2 == 0 || %(wounded_p2)s"]

[[checkpoint]]
name = "fight2"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
at_frame = true
within = 16000
shot = false
expect = ["port_match_kept_p1 == 0 || (p1_leg_wounds == port_match_legs_p1 && p1_arm_wounds == port_match_arm_p1 && p1_body_wounds == port_match_body_p1 && p1_off_arm_ok == port_match_off_arm_p1)",
          "port_match_kept_p2 == 0 || (p2_leg_wounds == port_match_legs_p2 && p2_arm_wounds == port_match_arm_p2 && p2_body_wounds == port_match_body_p2 && p2_off_arm_ok == port_match_off_arm_p2)",
          "port_match_kept_p1 == 1 || (p1_leg_wounds + p1_arm_wounds + p1_body_wounds == 0 && p1_off_arm_ok == 1)",
          "port_match_kept_p2 == 1 || (p2_leg_wounds + p2_arm_wounds + p2_body_wounds == 0 && p2_off_arm_ok == 1)"]

[[checkpoint]]
name = "kill2"
when = "winner != FIGHT_NOBODY"
at_frame = true
within = 16000
shot = false

[[checkpoint]]
name = "result"
when = "port_match_card == 2"
within = 16000
shot = false
expect = ["port_match_over == 1"]
THE_END''' % {'wounded_p1': WOUNDED % {'s': 1}, 'wounded_p2': WOUNDED % {'s': 2}}
SESSION_RB_KINDS = {
    'crew': (SESSION_RB_CREW_PRESSES, {
        'RULE': '3',
        'GRID_EXPECT': '"port_match_page_rule == 3", "port_match_crew_size == 2", "port_match_crew_sides == 2"',
        'CREW_PLACES': '''[[checkpoint]]
name = "crew2"
when = "port_match_crew_picked == 1"
within = 1500
shot = false

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false
expect = ["port_match_crew_picked == 2", "port_crew_p1_1 != port_crew_p1_2", "port_crew_p2_1 != port_crew_p2_2"]
''',
        'THE_END': '''
[[checkpoint]]
name = "top"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_TOP && port_match_to_top == 1"
within = 3000
shot = false

[[checkpoint]]
name = "stay"
after = 600
shot = false
'''}),
    'stock': (SESSION_RB_STOCK_PRESSES, {
        'RULE': '1',
        'GRID_EXPECT': '"port_match_page_rule == 1", "port_match_page_count == 2", "port_match_rule_first == 1"',
        'CREW_PLACES': '''[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false
''',
        'THE_END': '''
[[checkpoint]]
name = "back"
when = "port_on_stages == 1"
within = 3000
shot = false
expect = ["port_match_over == 1", "port_match_to_top == 0"]
'''}),
}
# the Stock session's ends: the guest's plan a second after the stage
# page, the host on alone
SESSION_RB_STOCK_END = {0: '\n[[checkpoint]]\nname = "alone"\nafter = 900\nshot = false\n',
                        1: '\n[[checkpoint]]\nname = "end"\nframes = 60\nshot = false\n'}


def session_rb_checkpoints(kind, side):
    """SESSION_RB_CREW_CHECKPOINTS for `kind` (a key of SESSION_RB_KINDS)
    and side `side`."""
    text = SESSION_RB_CREW_CHECKPOINTS
    for word, value in SESSION_RB_KINDS[kind][1].items():
        text = text.replace(word, value)
    if kind == 'stock':
        text += SESSION_RB_STOCK_END[side]
    return text


def wounding_fight():
    """Two pads' presses in a fight whose winner ends it wounded: the
    second player walks in and strikes with Cross for 260 frames, the first
    waiting, then the first walks in and strikes with Cross until one falls
    (as the port plays today the first wins it, wounded)."""
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
    return ['fight+%df %s%s' % (at, 'P2 ' if pad else '', button) for at, pad, button in sorted(presses)]


def finishing_fight(checkpoint='fight2'):
    """The first pad's presses in the fight after `checkpoint`, the second
    pressing nothing: a walk in and two low strikes a second, as
    tools/port_match.py's fights (port_stage_select.fight_presses), which
    put down a fighter nobody plays, wounded or not."""
    return ['%s+%df %s' % (checkpoint, n * 60 + offset, button) for n in range(66)
            for offset, button in ((0, 'RIGHT hold 20f'), (25, 'CIRCLE'), (40, 'CIRCLE'))]


def write_rb_crew_plan(program, side, folder, kind='crew'):
    """The plan of side `side` for the rule-first session of `kind` (a key
    of SESSION_RB_KINDS): its path."""
    presses = SESSION_RB_KINDS[kind][0] + wounding_fight() + finishing_fight()
    toml = os.path.join(folder, 'rb_crew_side%d.toml' % side)
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write('about = "A session\'s Crew match, the rule first, side %d."\n' % side)
        f.write('discs = ["port"]\nlimit = 60000\nseconds = %d\n' % SECONDS)
        f.write('input = [\n%s\n]\n' % ',\n'.join('    "%s"' % p for p in own_pad(presses, side)))
        f.write(session_rb_checkpoints(kind, side))
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), 1)
    path = os.path.join(folder, 'rb_crew_plan_side%d.txt' % side)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return path


def plan_claims(plan, out, toml_text):
    """Each checkpoint `out` printed, and the claims of its `expect` in
    `toml_text` that do not hold there: [(checkpoint, claim)], and the
    checkpoints printed."""
    import tomllib
    table = tomllib.loads(toml_text)
    expects = {cp['name']: cp.get('expect', []) for cp in table.get('checkpoint', [])}
    names = [line.split()[1] for line in read(plan).split('\n') if line.startswith('var ')]
    failed = []
    printed = []
    for line in out.split('\n'):
        words = line.split()
        if len(words) > 3 and words[0] == 'cp' and words[1] in expects:
            printed.append(words[1])
            values = dict(playtest.header_names())
            values.update(zip(names, (int(v) for v in words[3:])))
            failed += [(words[1], claim) for claim in expects[words[1]] if not playtest.evaluate(claim, values)]
    return failed, printed


def check_session_rb_crew(label, program, args, ports, kind='crew'):
    """The rule-first session of `kind`, Crew (SESSION_RB_CREW_PRESSES) or
    Stock: the problems found."""
    folder = os.path.join(args.out, 'session', label)
    os.makedirs(folder, exist_ok=True)
    plans = [write_rb_crew_plan(program, side, folder, kind) for side in (0, 1)]
    more = []
    for side in (0, 1):
        replays = os.path.join(folder, 'side%d-replays' % side)
        if os.path.isdir(replays):
            shutil.rmtree(replays)
        # (the first boundary: with ReBushido's top menu the opening movie
        # comes at once, and plays through between two boundaries)
        more.append(['--session-at', '1', '--menu', 'rebushido', '--mods', 'off', '--replays', replays])
    host = start_session_side(program, 0, folder, 'side0', plans[0], args, ports, more[0])
    time.sleep(0.3)
    guest = start_session_side(program, 1, folder, 'side1', plans[1], args, ports, more[1])
    codes = [wait_session_side(host, SECONDS), wait_session_side(guest, SECONDS)]
    problems = []
    hashes = []
    for side, code in enumerate(codes):
        who = 'host' if side == 0 else 'guest'
        err = read(os.path.join(folder, 'side%d.err' % side))
        out = read(os.path.join(folder, 'side%d.out' % side))
        toml = read(os.path.join(folder, 'rb_crew_side%d.toml' % side))
        print('port_net: session: %s: %s: exit %s' % (label, who, code), flush=True)
        if code != 0:
            problems.append('session: %s: the %s ended with %s (%s/side%d.err)' % (label, who, code, folder, side))
        if 'DESYNC' in err:
            problems.append('session: %s: the %s found a desync' % (label, who))
        if kind == 'crew' and ('the players went back to the top menu at frame' not in err or
                               "the player's own game put back" not in err):
            problems.append('session: %s: the %s\'s session did not end at the top menu, the player\'s own game '
                            'put back (%s/side%d.err)' % (label, who, folder, side))
        failed, printed = plan_claims(plans[side], out, toml)
        # (a checkpoint only a frame run again after a rollback reaches is
        # not printed: the guest's may miss one; its session's end is in
        # its log)
        last = 'top' if kind == 'crew' else 'back'
        for name in ('select', 'rule', 'grid', 'stages', 'card', 'fight2', 'result', last) if side == 0 else \
                ('select', 'result'):
            if name not in printed:
                problems.append('session: %s: the %s did not reach `%s`' % (label, who, name))
        for name, claim in failed:
            problems.append('session: %s: the %s at `%s`: not %s' % (label, who, name, claim))
        hashes.append(hashes_of(err))
    common = sorted(set(hashes[0]) & set(hashes[1]))
    differ = [frame for frame in common if hashes[0][frame] != hashes[1][frame]]
    if differ:
        problems.append('session: %s: the state hashes differ from frame %d' % (label, differ[0]))
    if len(common) < 40:
        problems.append('session: %s: too few state hashes in common (%d)' % (label, len(common)))
    if not problems and kind == 'crew':
        print('port_net: session: %s: the rule chosen by the host before the grid, the crews of different '
              'fighters, the first fight\'s winner wounded into the second, the top menu at the match\'s end '
              'and the session ended there on both; %d state hashes the same on both, the last at frame %d'
              % (label, len(common), common[-1]), flush=True)
    elif not problems:
        print('port_net: session: %s: Stock 2 chosen by the host before the grid, the first fight\'s winner '
              'wounded into the second, the loser of a life unhurt, the stage page at the match\'s end; %d state '
              'hashes the same on both, the last at frame %d' % (label, len(common), common[-1]), flush=True)
    problems += check_session_replays(label, program, folder, False)
    return problems


ARENA_SOURCE = 'tools/arenas/courtyard'
ARENA_SECONDS = 60


def check_arena_refused(label, program, args, ports):
    """The host has an arena in play and the guest has not: the guest is
    turned away at the meeting, before either starts the game, and says
    why (port_ArenaSessionHash, src/port/session.c)."""
    import arena_compile
    folder = os.path.join(args.out, 'session', label + '-arena')
    mods = os.path.join(folder, 'mods')
    os.makedirs(os.path.join(mods, 'courtyard', 'arenas'), exist_ok=True)
    with open(os.path.join(mods, 'courtyard', 'mod.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('name = Stone courtyard\n')
    with open(os.path.join(mods, 'courtyard', 'arenas', 'courtyard.arena'), 'wb') as f:
        f.write(arena_compile.compile_arena(os.path.join(REPO, ARENA_SOURCE), report=lambda text: None))
    procs = []
    for side in (0, 1):
        cmd = [program, '--image', playtest.RETAIL_CUE, '--mods', mods, '--no-cards']
        if side == 0:
            cmd += ['--host', '%s:%d' % (LOCALHOST, ports[0]), '--arena', 'courtyard']
        else:
            cmd += ['--join', '%s:%d' % (LOCALHOST, ports[0]), '--net-bind', '%s:%d' % (LOCALHOST, ports[1])]
        err = open(os.path.join(folder, 'side%d.err' % side), 'wb')
        procs.append((subprocess.Popen(PINNED + cmd, cwd=REPO, stdout=subprocess.DEVNULL, stderr=err,
                                       stdin=subprocess.DEVNULL), err))
        if side == 0:
            time.sleep(0.5)
    problems = []
    guest, guest_err = procs[1]
    try:
        code = guest.wait(timeout=ARENA_SECONDS)
    except subprocess.TimeoutExpired:
        guest.kill()
        guest.wait()
        code = None
    host, host_err = procs[0]
    host.kill()
    host.wait()
    guest_err.close()
    host_err.close()
    said = read(os.path.join(folder, 'side1.err'))
    if code in (0, None) or 'turned this program away' not in said or 'the arena in play' not in said:
        problems.append('session: %s: a guest without the host\'s arena was not turned away (exit %s; %s)'
                        % (label, code, os.path.join(folder, 'side1.err')))
    else:
        print('port_net: session: %s: a guest without the host\'s arena turned away, as it must be' % label,
              flush=True)
    return problems


def check_menu(program, args, ports):
    """The settings menu's Play online page, in two window programs: each
    is started as a player starts it, opens the menu (F1, here
    --menu-at), goes to Play online and starts there, the first as the
    host and the second as a guest who types the host's address. Each
    begins the session in its own process, with no start again
    (docs/NETPLAY.md, section 15), and the two play to the versus select
    screen and on a little. Both sockets are on 127.0.0.1: the settings
    say `net_listen = local`."""
    folder = os.path.join(args.out, 'menu')
    os.makedirs(folder, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', **NO_CONTROLLERS)
    # up from the first row comes round to the last, Quit ReBushido; then
    # Back to the game, Updates, and the row above it is Play online; on its page,
    # by address: You, the port (the host's address, typed), Start
    keys = ['up up up up accept down down accept',
            'up up up up accept down accept type:%s:%d down accept' % (LOCALHOST, ports[0])]
    settings = ['net_role = host\nnet_port = %d\nnet_listen = local\nnet_delay = 2\n' % ports[0],
                'net_role = join\nnet_listen = local\n']
    procs = []
    start = time.time()
    for side in (0, 1):
        config = os.path.join(folder, 'side%d.cfg' % side)
        with open(config, 'w', encoding='utf-8', newline='\n') as f:
            f.write('fullscreen = off\n' + settings[side])
        cmd = [program, '--image', playtest.RETAIL_CUE, '--config', config, '--windowed', '--frames', '1500',
               '--log', os.path.join(folder, 'side%d.log' % side), '--menu-at', '120', '--menu-keys', keys[side],
               '--online-keys', 'until:lobby accept',
               '--card1', os.path.join(folder, 'card1.mcd'), '--card2', os.path.join(folder, 'card2.mcd')]
        out = open(os.path.join(folder, 'side%d.out' % side), 'wb')
        err = open(os.path.join(folder, 'side%d.err' % side), 'wb')
        procs.append((subprocess.Popen(window_cmd(cmd), cwd=REPO, stdout=out, stderr=err, stdin=subprocess.DEVNULL,
                                       env=window_env(env)), out, err))
    codes = []
    for proc, out, err in procs:
        try:
            codes.append(proc.wait(timeout=max(1, SECONDS - (time.time() - start))))
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            codes.append(None)
        out.close()
        err.close()
    problems = []
    hashes = []
    for side in (0, 1):
        who = 'host' if side == 0 else 'guest'
        err = read(os.path.join(folder, 'side%d.err' % side))
        print('port_net: menu: %s: exit %s, %.0f s' % (who, codes[side], time.time() - start), flush=True)
        for line in err.split('\n'):
            if 'online:' in line or line.startswith(('session: begun', 'netplay: time')):
                print('    ' + line)
        if codes[side] != 0:
            problems.append('menu: the %s ended with %s (%s/side%d.err)' % (who, codes[side], folder, side))
        if 'session 1 begins in this program' not in err or 'session: begun' not in err or \
                'starts again' in err:
            problems.append('menu: the %s did not begin the session in its own process' % who)
        hashes.append(hashes_of(err))
    common = sorted(set(hashes[0]) & set(hashes[1]))
    if not common or any(hashes[0][f] != hashes[1][f] for f in common):
        problems.append('menu: the two programs\' state hashes are not the same (%d in common)' % len(common))
    elif not problems:
        print('port_net: menu: both began the session from the settings menu in their own processes and played '
              'on: %d state hashes, the same on both' % len(common), flush=True)
    return problems


# ---- sessions begun in the running program (docs/NETPLAY.md, section 15)

INPROCESS_SEED = 4242


def first_frame_hash(text):
    """The hash of a session's first frame (`netplay: the session's first
    frame, hash H`), or None; with `all`, every one in order."""
    found = [line.split()[-1] for line in text.split('\n')
             if line.startswith("netplay: the session's first frame, hash ")]
    return found


def start_session_side(program, side, folder, label, plan, args, ports, more):
    """One program of a session on 127.0.0.1 (the host side 0, the guest
    1), with port_net's session plan and the test's fixed seed: its
    process."""
    trouble = ['--net-latency', str(args.latency), '--net-loss', str(args.loss), '--net-seed', str(INPROCESS_SEED)]
    cmd = [program, '--image', playtest.RETAIL_CUE, '--plan', plan, '--trace',
           '--out', os.path.join(folder, label)] + trouble
    if side == 0:
        cmd += ['--host', '%s:%d' % (LOCALHOST, ports[0]), '--net-delay', str(args.delay),
                '--card1', os.path.join(folder, 'card1.mcd'), '--card2', os.path.join(folder, 'card2.mcd')]
    else:
        cmd += ['--join', '%s:%d' % (LOCALHOST, ports[0]), '--net-bind', '%s:%d' % (LOCALHOST, ports[1])]
    cmd += more
    os.makedirs(os.path.join(folder, label), exist_ok=True)
    out = open(os.path.join(folder, label + '.out'), 'wb')
    err = open(os.path.join(folder, label + '.err'), 'wb')
    proc = subprocess.Popen(PINNED + cmd, cwd=REPO, stdout=out, stderr=err, stdin=subprocess.DEVNULL)
    proc.files = (out, err)
    return proc


def wait_session_side(proc, seconds):
    try:
        code = proc.wait(timeout=max(1, seconds))
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        code = None
    for f in proc.files:
        f.close()
    return code


def inprocess_pair(program, folder, label, plans, args, ports, host_more, guest_more):
    """A session's two programs, started together and waited for: their
    exit codes and what each printed to its error output."""
    host = start_session_side(program, 0, folder, label + '-host', plans[0], args, ports, host_more)
    time.sleep(0.3)
    guest = start_session_side(program, 1, folder, label + '-guest', plans[1], args, ports, guest_more)
    codes = [wait_session_side(host, SECONDS), wait_session_side(guest, 60)]
    return codes, [read(os.path.join(folder, label + side + '.err')) for side in ('-host', '-guest')]


def check_inprocess(label, program, args, base):
    """Sessions begun in the running program (--session-at), held to one
    started for the session (--host and --join from the start), both with
    the same seed (--net-seed):

    - `restart`: the pair started for the session, as every session was
      before: the hashes every 30 frames, and the first frame's;
    - `inprocess`: the same pair, each playing its own game first (the
      host 240 frames, the guest 120) and then beginning the session in
      the same process: the first frame's hash and every one after must be
      the restarted pair's;
    - `again`: a host that begins two sessions one after the other in one
      process, with two guests one after the other: both sessions' hashes
      are the restarted pair's, so nothing of the first is left in the
      second;
    - `refused`: a guest of another build (--session-differ) is turned
      away at the meeting and goes on with its own game, in the same
      process."""
    folder = os.path.join(args.out, 'inprocess', label)
    os.makedirs(folder, exist_ok=True)
    plans = [write_session_plan(program, side, folder) for side in (0, 1)]
    problems = []
    results = {}

    def run(name, n, host_more, guest_more):
        ports = (base + 2 * n, base + 2 * n + 1)
        results[name] = inprocess_pair(program, folder, name, plans, args, ports, host_more, guest_more)

    def run_again():
        ports = (base + 4, base + 5)
        host = start_session_side(program, 0, folder, 'again-host', plans[0], args, ports,
                                  ['--session-at', '120', '--sessions', '2'])
        guests = []
        for n in (1, 2):
            time.sleep(0.3)
            guest = start_session_side(program, 1, folder, 'again-guest%d' % n, plans[1], args, ports,
                                       ['--session-at', '60'])
            guests.append(wait_session_side(guest, SECONDS))
        code = wait_session_side(host, 120)
        results['again'] = ([code] + guests, [read(os.path.join(folder, 'again-%s.err' % who))
                                              for who in ('host', 'guest1', 'guest2')])

    def run_refused():
        ports = (base + 6, base + 7)
        host = start_session_side(program, 0, folder, 'refused-host', plans[0], args, ports, ['--session-at', '60'])
        time.sleep(0.3)
        guest = start_session_side(program, 1, folder, 'refused-guest', plans[1], args, ports,
                                   ['--session-at', '60', '--session-differ', 'build'])
        code = wait_session_side(guest, 90)
        host.kill()
        wait_session_side(host, 10)
        results['refused'] = ([code], [read(os.path.join(folder, 'refused-guest.err'))])

    def run_leave():
        ports = (base + 8, base + 9)
        leave_plans = [write_leave_plan(program, side, folder) for side in (0, 1)]
        results['leave'] = inprocess_pair(program, folder, 'leave', leave_plans, args, ports,
                                          ['--session-at', '60'], ['--session-at', '60'])

    threads = [threading.Thread(target=run_leave), threading.Thread(target=run, args=('restart', 0, [], [])),
               threading.Thread(target=run, args=('inprocess', 1, ['--session-at', '240'],
                                                  ['--session-at', '120'])),
               threading.Thread(target=run_again), threading.Thread(target=run_refused)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    codes, errs = results['restart']
    reference = hashes_of(errs[0])
    first = first_frame_hash(errs[0])
    if codes != [0, 0] or not first or len(reference) < 40:
        problems.append('inprocess: %s: the pair started for the session did not play it through (exits %s, %d '
                        'hashes)' % (label, codes, len(reference)))
        return problems
    print('port_net: inprocess: %s: started for the session: the first frame %s, %d hashes' % (
        label, first[0], len(reference)), flush=True)

    def same_as_reference(what, text, who):
        hashes = hashes_of(text)
        firsts = first_frame_hash(text)
        common = sorted(set(hashes) & set(reference))
        differ = [f for f in common if hashes[f] != reference[f]]
        if firsts[:1] != first[:1]:
            problems.append('inprocess: %s: %s: the %s\'s first frame is %s, not %s as in a program started for '
                            'the session' % (label, what, who, firsts[:1], first[0]))
        elif differ:
            problems.append('inprocess: %s: %s: the %s\'s hashes part from the restarted pair\'s at frame %d'
                            % (label, what, who, differ[0]))
        elif len(common) < 40:
            problems.append('inprocess: %s: %s: the %s has too few hashes in common with the restarted pair '
                            '(%d)' % (label, what, who, len(common)))
        return len(common)

    codes, errs = results['inprocess']
    if codes != [0, 0]:
        problems.append('inprocess: %s: inprocess: exits %s (%s)' % (label, codes, folder))
    for err, who in zip(errs, ('host', 'guest')):
        if 'session 1 begins in this program' not in err or "the state of the game's start put back" not in err:
            problems.append('inprocess: %s: the %s did not begin the session in its own process' % (label, who))
    count = [same_as_reference('inprocess', err, who) for err, who in zip(errs, ('host', 'guest'))]
    if not problems:
        print('port_net: inprocess: %s: begun in the running program (the host after 240 frames of its own, the '
              'guest after 120): the same first frame and the same %d and %d hashes as the restarted pair'
              % (label, count[0], count[1]), flush=True)

    codes, errs = results['again']
    host_err = errs[0]
    parts = host_err.split('online: session 2 begins in this program')
    if codes != [0, 0, 0] or len(parts) != 2:
        problems.append('inprocess: %s: again: exits %s, %d sessions begun (%s)' % (label, codes, len(parts),
                                                                                   folder))
    else:
        counts = [same_as_reference('again, session %d' % (n + 1), part, 'host') for n, part in enumerate(parts)]
        if 'the player\'s own game put back' not in parts[0] or 'the last session asked for has ended' not in \
                parts[1]:
            problems.append('inprocess: %s: again: the host did not go back to its own game between the two'
                            % label)
        if not problems:
            print('port_net: inprocess: %s: two sessions one after the other in one host process, two guests: '
                  'both the restarted pair\'s hashes (%d and %d)' % (label, counts[0], counts[1]), flush=True)

    codes, errs = results['leave']
    said = [("the players went back to the top menu at frame" in err and
             'session 1 has ended: You both went back to the top menu' in err and
             "the player's own game put back" in err) for err in errs]
    if codes != [0, 0] or not all(said):
        problems.append('inprocess: %s: leave: the way back to the top menu from the select screen did not end the '
                        'session on both and send each back to its own game (exits %s; %s)' % (label, codes, folder))
    elif not problems:
        print('port_net: inprocess: %s: Triangle on the select screen took both back to the top menu: the session '
              'ended on both at the same frame, each back to its own game' % label, flush=True)

    codes, errs = results['refused']
    err = errs[0]
    if codes[0] != 1 or 'turned this program away' not in err or \
            'no session; the program goes on with its own game' not in err:
        problems.append('inprocess: %s: refused: a guest of another build was not turned away and sent back to '
                        'its own game (exit %s; %s)' % (label, codes[0], os.path.join(folder, 'refused-guest.err')))
    elif not problems:
        print('port_net: inprocess: %s: a guest of another build turned away at the meeting, and back to its own '
              'game in the same process' % label, flush=True)
    return problems


def check_pages(args):
    """The online pages' cases of tools/port_net_lobby.py (sessions begun
    from the pages in the running window, a guest turned away on them),
    through this system's window program, with their lobby server: the
    problems found."""
    import port_net_lobby
    return ['pages: ' + p for p in port_net_lobby.run(port_net_lobby.PAGES_CASES,
                                                       args.port_base - LOBBY_BELOW + PAGES_ABOVE)]


def check_link(label, program, args, ports):
    folder = os.path.join(args.out, 'link', label)
    os.makedirs(folder, exist_ok=True)
    plans = [write_plan(program, side, folder) for side in (0, 1)]
    trouble = ['--net-latency', str(args.latency), '--net-loss', str(args.loss)]
    results = run_pair(program, plans, folder, [['--link-side', '0'] + trouble, ['--link-side', '1'] + trouble],
                       ports)
    problems = []
    outs = []
    for side, (code, seconds) in enumerate(results):
        print('port_net: link: %s: side %d: exit %s, %.0f s' % (label, side, code, seconds), flush=True)
        err = read(os.path.join(folder, 'side%d.err' % side))
        for line in err.split('\n'):
            if line.startswith(('net:', 'link:')):
                print('    ' + line)
        out = read(os.path.join(folder, 'side%d.out' % side))
        outs.append(out)
        if code != 0:
            problems.append('link: %s: side %d ended with %s (%s/side%d.err)' % (label, side, code, folder, side))
        if 'cp end ' not in out:
            problems.append('link: %s: side %d did not reach the end of the fight' % (label, side))
    if outs[0] != outs[1]:
        problems.append('link: %s: the two traces differ at %s' % (label, first_difference(outs[0], outs[1])))
    elif not problems:
        traces = sum(1 for line in outs[0].split('\n') if line.startswith('trace '))
        print('port_net: link: %s: both consoles printed the same %d lines (%d of them traces), to the end of '
              'the fight' % (label, outs[0].count('\n'), traces), flush=True)
    return problems


def stale_window():
    """(built, head) when the window program was built from a commit
    other than HEAD (its git_hash.txt, by tools/port_build.py), or None
    when it is HEAD's or either cannot be told."""
    try:
        with open(os.path.join(REPO, os.path.dirname(WINDOW_PROGRAM), 'git_hash.txt'), encoding='utf-8') as f:
            built = f.read().strip().replace('-dirty', '')
        proc = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=REPO, capture_output=True, text=True)
    except OSError:
        return None
    head = proc.stdout.strip()
    if proc.returncode != 0 or not head or built in ('', 'unknown') or built.startswith(head)             or head.startswith(built):
        return None
    return built, head


def run_checks(args, base):
    """The checks of `args.what` once, on ports from `base` up: the
    problems found."""
    rollback = args.what != 'link'
    problems = []
    if args.what == 'window':
        program = os.path.join(REPO, WINDOW_PROGRAM)
        if not os.path.isfile(program):
            print('port_net: window: skipped: no %s (make port-sdl)' % WINDOW_PROGRAM)
        elif stale_window():
            # make port-net builds neither program: an older window
            # program's menus are not this commit's, and its key paths miss
            problems.append('window: %s was built from %s, not from HEAD (%s): make port-sdl first'
                            % ((WINDOW_PROGRAM,) + stale_window()))
        else:
            problems += check_session('window', program, args, (base + 20, base + 21), True)
            if not args.stress:
                problems += check_menu(program, args, (base + 22, base + 23))
                problems += check_pages(args)
    for n, bits in enumerate([] if args.what == 'window' else [args.bits] if args.bits else [64, 32]):
        program = os.path.join(REPO, PROGRAMS[bits])
        if not os.path.isfile(program):
            problems.append('%d-bit: no %s (make port)' % (bits, PROGRAMS[bits]))
            continue
        ports = (base + 2 * n, base + 2 * n + 1)
        if args.what == 'inprocess':
            problems += check_inprocess('%d-bit' % bits, program, args, base + 10 * n)
            continue
        if args.what == 'session':
            problems += check_session('%d-bit' % bits, program, args, (ports[0] + 10, ports[1] + 10), False)
            if not args.stress:
                problems += check_arena_refused('%d-bit' % bits, program, args,
                                                (ports[0] + 14, ports[1] + 14))
                # a Crew match, the rule chosen before the grid by the
                # host and the crews picked in the session
                problems += check_session_rb_crew('%d-bit-crew' % bits, program, args,
                                                  (ports[0] + 16, ports[1] + 16))
                # and Stock, whose winner keeps his wounds too
                problems += check_session_rb_crew('%d-bit-stock-wounds' % bits, program, args,
                                                  (ports[0] + 18, ports[1] + 18), 'stock')
            continue
        check = check_rollback if rollback else check_link
        problems += check('%d-bit' % bits, program, args, ports)
    return problems


def start_burners(count, cpus):
    """`count` programs that only burn a processor (on `cpus`, if given),
    to starve the two under test: their processes."""
    burners = []
    for _ in range(count):
        cmd = [sys.executable, '-c', 'while True: pass']
        burners.append(subprocess.Popen((['taskset', '-c', cpus] if cpus else []) + cmd, stdin=subprocess.DEVNULL,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    return burners


def stress(args):
    """--stress N: the checks N times over, under --burn's load, each run
    in a folder of its own, which is kept where it failed; a count of the
    runs that failed and of those that found a desync."""
    out = args.out
    failed = []
    desyncs = 0
    burners = start_burners(args.burn, args.cpus)
    try:
        for run in range(args.stress):
            args.out = os.path.join(out, 'stress', 'run%03d' % run)
            problems = run_checks(args, args.port_base)
            if problems:
                failed.append(run)
                desyncs += 1 if any('desync' in p or 'hashes differ' in p for p in problems) else 0
                for problem in problems:
                    print('port_net: stress: run %d: %s' % (run, problem), flush=True)
            print('port_net: stress: run %d of %d: %s' % (run + 1, args.stress, 'FAILED' if problems else 'ok'),
                  flush=True)
    finally:
        for burner in burners:
            burner.kill()
            burner.wait()
    print('port_net: stress: %s %s, %d runs under %d burners%s: %d failed, %d of them with a desync%s'
          % (args.what, '(the window\'s programs)' if args.what == 'window' else '%d-bit' % args.bits if args.bits
             else 'both sizes', args.stress, args.burn,
             ' on processors %s' % args.cpus if args.cpus else '', len(failed), desyncs,
             ' (runs %s)' % ' '.join(str(r) for r in failed) if failed else ''))
    return ['stress: %d of %d runs failed' % (len(failed), args.stress)] if failed else []


# The lobby's cases (tools/port_net_lobby.py) take their ports this far
# below --port-base, out of the way of the four's.
LOBBY_BELOW = 20000
# ... and the online pages' cases (check_pages) this far above those
PAGES_ABOVE = 500


def run_all(args):
    """link, rollback, session, window and the lobby's at once, each a program of this
    file with its own PORTS_USED ports from --port-base on and its own
    folder of --out; what each printed, in that order, and 1 if one
    failed (make port-net)."""
    whats = ('link', 'rollback', 'session', 'window', 'inprocess', 'lobby')
    if not 1024 + LOBBY_BELOW <= args.port_base <= 65535 - PORTS_USED * len(whats):
        print('FAILED: port_net: --port-base: from 1024 to %d for all' % (65535 - PORTS_USED * len(whats)))
        return 1
    os.makedirs(args.out, exist_ok=True)
    runs = []
    for n, what in enumerate(whats):
        if what == 'lobby':
            cmd = [sys.executable, os.path.join(REPO, 'tools', 'port_net_lobby.py'), 'headless', '--port-base',
                   str(args.port_base - LOBBY_BELOW)]
        else:
            cmd = [sys.executable, os.path.abspath(__file__), what, '--out', args.out, '--delay', str(args.delay),
                   '--port-base', str(args.port_base + PORTS_USED * n)]
            for flag, value in (('--bits', args.bits), ('--latency', args.latency), ('--loss', args.loss)):
                if value is not None:
                    cmd += [flag, str(value)]
        log = open(os.path.join(args.out, what + '.log'), 'wb')
        runs.append((what, log, time.time(), subprocess.Popen(cmd, cwd=REPO, stdout=log, stderr=subprocess.STDOUT,
                                                                stdin=subprocess.DEVNULL)))
    failed = []
    for what, log, start, proc in runs:
        code = proc.wait()
        log.close()
        with open(os.path.join(args.out, what + '.log'), encoding='utf-8', errors='replace') as f:
            sys.stdout.write(f.read())
        print('port_net: %s: %s (%.0fs)' % (what, 'ok' if code == 0 else 'FAILED', time.time() - start))
        if code != 0:
            failed.append(what)
    if failed:
        print('FAILED: port_net: %s' % ' '.join(failed))
        return 1
    print('OK: port_net all')
    return 0


def main():
    global PINNED
    ap = argparse.ArgumentParser(description='Two programs of the port playing each other over UDP on this PC.')
    ap.add_argument('what', choices=('link', 'rollback', 'session', 'window', 'inprocess', 'all'),
                    help="what they play: session is a session's headless programs, window the window's, "
                    "inprocess sessions begun in the running program; all of them at once (make port-net), "
                    "each with ports of its own")
    ap.add_argument('--bits', type=int, choices=(32, 64), help='one size only')
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'net'), help='where the runs go')
    ap.add_argument('--latency', type=int, help='milliseconds each datagram waits (--net-latency): 0 for the '
                    'link, 100 for rollback')
    ap.add_argument('--loss', type=int, help='datagrams in a hundred dropped (--net-loss): 0 for the link, 5 '
                    'for rollback')
    ap.add_argument('--delay', type=int, default=2, help="frames a press counts on, rollback's (--net-delay)")
    ap.add_argument('--port-base', type=int, default=BASE_PORT,
                    help='the first of the ports on 127.0.0.1 the runs use (%d; up to 23 above it)' % BASE_PORT)
    ap.add_argument('--stress', type=int, default=0, metavar='N',
                    help='run the checks N times over and count the runs that fail and the desyncs')
    ap.add_argument('--burn', type=int, default=0, metavar='K',
                    help='with --stress: K programs that burn a processor each, for load')
    ap.add_argument('--cpus', help='start both programs and the burners on these processors only (taskset -c), '
                    'so that they starve each other')
    ap.add_argument('--pause', type=int, default=0, metavar='MS',
                    help='stop one program or the other for MS milliseconds every second or so, as a busy PC does')
    args = ap.parse_args()
    if args.what == 'all':
        if args.stress or args.burn or args.cpus or args.pause:
            ap.error('all: --stress, --burn, --cpus and --pause are for one of the four')
        return run_all(args)
    Pauser.pause_ms = args.pause
    rollback = args.what != 'link'
    if args.latency is None:
        args.latency = {'link': 0, 'rollback': 100, 'session': 100, 'window': 60, 'inprocess': 60}[args.what]
    if args.loss is None:
        args.loss = 5 if rollback else 0
    if args.cpus:
        PINNED = ['taskset', '-c', args.cpus]
    if not os.path.isfile(playtest.RETAIL_CUE):
        print('port_net: skipped: no %s (your own image of the retail disc; REBUSHIDO_RETAIL names it)'
              % playtest.RETAIL_CUE)
        return 0
    problems = stress(args) if args.stress else run_checks(args, args.port_base)
    for problem in problems:
        print('FAILED: port_net: ' + problem)
    if not problems:
        print('OK: port_net %s' % args.what)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
