#!/usr/bin/env python3
"""Two programs of the native port playing each other over UDP on this PC
(docs/NETPLAY.md, sections 11 and 12), headless:

    python3 tools/port_net.py link    [--bits 32|64] [--out FOLDER]
                                      [--latency MS] [--loss PERCENT]

`link`: the game's own link-cable versus mode, in lockstep with a frame of
delay. Each program is one console (--link-side 0 and 1) with its own plan:
both go from power on to the top menu's link row as port_game's plan goes
to versus; then the first console's pad picks the first player's fighter,
weapon and stage and fights as port_game's first pad, and the second
console's pad picks and fights as port_game's second. It passes if both
reach the end of the fight and the two traces are the same, line for
line.

Both sockets are bound to 127.0.0.1, on two ports from 47000 up: nothing
listens on any other address. The programs are the ones `make port` built
(build them again first), and the retail disc's image is needed
(REBUSHIDO_RETAIL, or the .cue at the checkout's root); no emulator.
Without the image it says so and passes, as the play tests do."""
import argparse
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

import playtest  # noqa: E402  (tools/playtest.py)

PROGRAMS = {64: 'build/port/rebushido', 32: 'build/port32/rebushido'}
SECONDS = 600
BASE_PORT = 47000
LOCALHOST = '127.0.0.1'

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
        procs.append((subprocess.Popen(cmd, cwd=REPO, stdout=out, stderr=err, stdin=subprocess.DEVNULL), out, err))
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
    return results


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


def main():
    ap = argparse.ArgumentParser(description='Two programs of the port playing each other over UDP on this PC.')
    ap.add_argument('what', choices=('link',), help='what they play')
    ap.add_argument('--bits', type=int, choices=(32, 64), help='one size only')
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'net'), help='where the runs go')
    ap.add_argument('--latency', type=int, default=0, help='milliseconds each datagram waits (--net-latency)')
    ap.add_argument('--loss', type=int, default=0, help='datagrams in a hundred dropped (--net-loss)')
    args = ap.parse_args()
    if not os.path.isfile(playtest.RETAIL_CUE):
        print('port_net: skipped: no %s (your own image of the retail disc; REBUSHIDO_RETAIL names it)'
              % playtest.RETAIL_CUE)
        return 0
    problems = []
    for n, bits in enumerate([args.bits] if args.bits else [64, 32]):
        program = os.path.join(REPO, PROGRAMS[bits])
        if not os.path.isfile(program):
            problems.append('%d-bit: no %s (make port)' % (bits, PROGRAMS[bits]))
            continue
        ports = (BASE_PORT + 2 * n, BASE_PORT + 2 * n + 1)
        problems += check_link('%d-bit' % bits, program, args, ports)
    for problem in problems:
        print('FAILED: port_net: ' + problem)
    if not problems:
        print('OK: port_net %s' % args.what)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
