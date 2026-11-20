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

`session` and `window` are a session's, set up by --host and --join
(section 13). `session` also holds the two players to the same arena
(docs/MODS.md, "Custom arenas"): the sample arena compiled at test time,
the host with it in play (--arena) and the guest without, which the host
must turn away.

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


def write_session_plan(program, side, folder):
    """The plan of the host (side 0) or the guest (1) for `program`: its
    path."""
    fight = port_game_fight()
    rematch = [p.replace('fight+', 'rematch+', 1) for p in fight]
    presses = own_pad(SESSION_SELECT + fight + rematch, side)
    if side == 0:
        presses.append("after_fight+120 CROSS")
    toml = os.path.join(folder, 'session_side%d.toml' % side)
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write('about = "A session of netplay, side %d."\n' % side)
        # (the cards are the host's own, as the command line names them)
        f.write('discs = ["port"]\nlimit = 16000\nseconds = %d\n' % SECONDS)
        f.write('input = [\n%s\n]\n' % ',\n'.join('    "%s"' % p for p in presses))
        f.write(SESSION_CHECKPOINTS + SESSION_END[side])
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


def check_session(label, program, args, ports, window):
    """A session between two programs, set up by --host and --join, through
    the versus select screen, a fight, a rematch and the guest's leaving.
    `window`: the programs are the window's, headless (dummy drivers), in
    real time; or else the headless ones, as fast as they go."""
    folder = os.path.join(args.out, 'session', label)
    os.makedirs(folder, exist_ok=True)
    plans = [write_session_plan(program, side, folder) for side in (0, 1)]
    trouble = ['--net-latency', str(args.latency), '--net-loss', str(args.loss)]
    env = dict(os.environ)
    procs = []
    start = time.time()
    for side in (0, 1):
        cmd = [program, '--image', playtest.RETAIL_CUE, '--plan', plans[side], '--trace',
               '--out', os.path.join(folder, 'side%d' % side)] + trouble
        if side == 0:
            # the host's cards, which it sends the guest: new ones here,
            # which no program writes (both play on copies in memory)
            cmd += ['--host', '%s:%d' % (LOCALHOST, ports[0]), '--net-delay', str(args.delay),
                    '--card1', os.path.join(folder, 'card1.mcd'), '--card2', os.path.join(folder, 'card2.mcd')]
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
    program starts again as the session's (sdl_main.c), and the two play
    to the versus select screen and on a little. Both sockets are on
    127.0.0.1: the settings say `net_listen = local`."""
    folder = os.path.join(args.out, 'menu')
    os.makedirs(folder, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', **NO_CONTROLLERS)
    # up from the first row comes round to the last, Quit ReBushido; then
    # Back to the game, and the row above it is Play online; on its page,
    # by address: You, the port (the host's address, typed), Start
    keys = ['up up up accept down down accept',
            'up up up accept down accept type:%s:%d down accept' % (LOCALHOST, ports[0])]
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
        if 'the program starts again as the %s' % who not in err or 'session: begun' not in err:
            problems.append('menu: the %s did not start again as the session\'s %s' % (who, who))
        hashes.append(hashes_of(err))
    common = sorted(set(hashes[0]) & set(hashes[1]))
    if not common or any(hashes[0][f] != hashes[1][f] for f in common):
        problems.append('menu: the two programs\' state hashes are not the same (%d in common)' % len(common))
    elif not problems:
        print('port_net: menu: both started again from the settings menu and played on: %d state hashes, the '
              'same on both' % len(common), flush=True)
    return problems


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


def run_checks(args, base):
    """The checks of `args.what` once, on ports from `base` up: the
    problems found."""
    rollback = args.what != 'link'
    problems = []
    if args.what == 'window':
        program = os.path.join(REPO, WINDOW_PROGRAM)
        if not os.path.isfile(program):
            print('port_net: window: skipped: no %s (make port-sdl)' % WINDOW_PROGRAM)
        else:
            problems += check_session('window', program, args, (base + 20, base + 21), True)
            if not args.stress:
                problems += check_menu(program, args, (base + 22, base + 23))
    for n, bits in enumerate([] if args.what == 'window' else [args.bits] if args.bits else [64, 32]):
        program = os.path.join(REPO, PROGRAMS[bits])
        if not os.path.isfile(program):
            problems.append('%d-bit: no %s (make port)' % (bits, PROGRAMS[bits]))
            continue
        ports = (base + 2 * n, base + 2 * n + 1)
        if args.what == 'session':
            problems += check_session('%d-bit' % bits, program, args, (ports[0] + 10, ports[1] + 10), False)
            if not args.stress:
                problems += check_arena_refused('%d-bit' % bits, program, args,
                                                (ports[0] + 14, ports[1] + 14))
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


def run_all(args):
    """link, rollback, session, window and the lobby's at once, each a program of this
    file with its own PORTS_USED ports from --port-base on and its own
    folder of --out; what each printed, in that order, and 1 if one
    failed (make port-net)."""
    whats = ('link', 'rollback', 'session', 'window', 'lobby')
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
    ap.add_argument('what', choices=('link', 'rollback', 'session', 'window', 'all'),
                    help="what they play: session is a session's headless programs, window the window's; all "
                    "the four at once (make port-net), each with ports of its own")
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
        args.latency = {'link': 0, 'rollback': 100, 'session': 100, 'window': 60}[args.what]
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
