#!/usr/bin/env python3
"""The window's build on the real graphics card: a fight measured, and the
budget held (docs/PORT.md, "Measuring on the card"; BUILD.md, "The gate").

    python tools/port_bench.py                       the cases, a report
    python tools/port_bench.py --case ra1 --case ra1-stress
    python tools/port_bench.py --budget [--report-only]
    python tools/port_bench.py --against main        and what changed since
                                                     main's numbers
    python tools/port_bench.py --paced --case ra1    paced to a display of
                                                     144 refreshes a second

From Windows' Python, on the PC with the card: it runs
build/port-sdl/windows/rebushido.exe (`make port-sdl-windows`) with
--hidden (a window never shown, on the card all the same), SDL's dummy
sound, a settings file of its own, no memory cards and a log of its own,
at 2560 by 1440, unpaced (--fast), through port_game's plan with Kannuki
against Mikado on Tatara Shrine (both wear cloth), every default feature
on and the four mods: the shipped english and ui, and art and
stages-hd from the main checkout's tmp/mods/ (read where they are, by
junctions in build/bench/mods/). One case at a time: two runs side by
side would share the card.

For each case, over the blanks of the fight: the processor's work of the
window's thread (every part of include/port_profile.h but the waits for
the clock and for the display) and the card's time (every pass of
src/port/gl/gl_bench.h), each part's median, and the work's median, 95th
and 99th centiles and the worst; the hitches, blanks whose work or card
time is over the 144 Hz budget of 6.94 ms; and the stalls, blanks over
three times the median, with the part that took the time (a texture
decoded at its first use, a cloth worked out the first time).

--paced runs the cases at the game's own pace instead, against a display
of 144 refreshes a second made up by the program (--display-rate: a
hidden window's swaps are not held to the PC's own display), and says
what only a paced run shows: the refreshes after the fight's first second
that no new picture reached (the display shows the one before again), and
the lag from the game taking its input at a frame's boundary to the first
picture of that frame handed to the display, in refreshes; and a press's
way to the screen part by part, in the fight and in the menus before it
(docs/PORT.md, "The press to the screen, part by part"). --rate N makes
the display N a second (0: the PC's own, which a hidden window's swaps
wait for). Its numbers are kept apart (+paced). The budget's case
ra1-paced is ra1 paced at 144 a second whatever --paced says.

--budget runs the gate's cases and holds them to LIMITS: what the owner
targets (2026-11-18: 144 pictures a second at 2560 by 1440, with run-ahead,
and room for four players and a larger stage), not what the program
does today. It fails where one is over, unless --report-only, which the
gate gives it until the targets are met. Where this is not Windows, or
there is no NVIDIA, AMD or Intel card, no window's build, no disc image
it says so and passes.

Every run's numbers go to build/bench/<commit>/ in the main checkout
(`-dirty` after a commit with changes not committed), so that one
checkout can hold itself to another's (`--against REV`) without running
it again.
"""
import argparse
import bisect
import json
import os
import statistics
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

import baseline  # noqa: E402
import port_budget  # noqa: E402

PROGRAM = os.path.join(REPO, 'build', 'port-sdl', 'windows', 'rebushido.exe')
OUT_DIR = os.path.join(REPO, 'build', 'bench')
WINDOW = '2560x1440'
# The fight's blanks of port_game's plan, at the latest: the run ends
# here; the fight itself is found in the log.
FRAMES = 3360
# The budget of one refresh at 144 a second.
BUDGET_MS = 1000.0 / 144
# A blank this many times the median work of the fight's frames is a
# stall.
STALL_TIMES = 3.0
# A run that has not ended by now is stopped.
RUN_SECONDS = 240
# --paced: the display made up, refreshes a second.
PACED_RATE = 144
PACED = False
# --paced: whether the display is waited for (vsync, from --set)
VSYNC = True
# A `l` line (the keys read again late) belongs to the boundary before it
# when it is this soon after it, in ms.
LATE_MOST_MS = 20.0
# A press's way to the screen (--paced), part by part: (measure, what it
# is). The parts after the read add up to `shown` but for the refresh the
# swap waited for and its own return, which a made-up display has none of.
LAG_PARTS = [
    ('to_step', "hand-over to the step's thread"),
    ('step', 'the step and the frames ahead'),
    ('from_step', 'hand-over back'),
    ('record', 'the record drawn'),
    ('to_picture', "the blanks' turns to the picture begun"),
    ('picture', 'the picture drawn'),
    ('card', 'handed to the card done'),
    ('swap', 'to the swap back'),
    ('shown', 'the read to the screen'),
]
DISC_MADE_MODS = ('art', 'stages-hd')  # not hd: the owner plays the game's own fighters (2026-11-19)
SHIPPED_MODS = ('english', 'ui')

# The cases: (name, run_ahead, fighter_models, --bench-stress).
CASES = {
    'ra0': (0, 'on', None),
    'ra1': (1, 'on', None),
    'ra2': (2, 'on', None),
    'ra1-plain': (1, 'off', None),
    # four HD fighters' worth and the ground twice over: four players on
    # a larger stage
    'ra1-stress': (1, 'on', 'fighters=4,stage=2,lights=16'),
    # ra1 at the game's pace against a display of 144 a second whatever
    # --paced says: the press's way to the screen (docs/PORT.md, "Input
    # lag")
    'ra1-paced': (1, 'on', None),
}
PACED_CASES = ('ra1-paced',)
DEFAULT_CASES = ('ra0', 'ra1', 'ra2', 'ra1-plain', 'ra1-stress')
BUDGET_CASES = ('ra0', 'ra1', 'ra2', 'ra1-stress', 'ra1-paced')

# The budget (--budget): (case, measure, at most, what it is). The
# measures are of the fight's blanks in which the game drew a frame,
# in milliseconds, but `hitches`, a percentage of all its blanks.
LIMITS = [
    ('ra1', 'cpu_p95', BUDGET_MS, "the processor's work of a fight frame, 95%, run_ahead 1"),
    ('ra1', 'all_cpu_p95', BUDGET_MS, "the processor's work of any blank of the fight, 95%"),
    ('ra1', 'hitches', 1.0, 'blanks of the fight over 6.94 ms, percent'),
    ('ra1', 'worst', 2 * BUDGET_MS, 'the worst blank of the fight (a stall)'),
    ('ra2', 'cpu_p95', BUDGET_MS, 'the same, run_ahead 2'),
    ('ra1-stress', 'cpu_p95', BUDGET_MS, 'four fighters, the ground twice: the processor'),
    # the worst blanks, which decide whether a game is smooth: after the
    # fight's first second, each blank's whole time on the window's
    # thread but the wait for the clock (the swap counts: the card
    # waited for is waited for there)
    ('ra1', 'settled_p99', BUDGET_MS, "any blank after the fight's first second, 99%"),
    ('ra2', 'settled_p99', BUDGET_MS, 'the same, run_ahead 2'),
    ('ra1-stress', 'settled_p99', BUDGET_MS, 'the same, four fighters, the ground twice'),
    # the step on its own thread (`s` lines): what it takes from the input
    # to the end of its frame, which the frame's first picture waits for
]
# What the program meets with room to spare (2026-11-18: the card's 95th
# centile of a fight frame 3.9 to 5.2 ms with run_ahead 0 to 2, the RTX
# 3090 at 2560 by 1440, with other agents' work on the machine), held
# whatever --report-only says: (case, measure, at most, what it is).
# How far over its limit a met measure may go before it fails: the owner
# (2026-11-19) wants good performance kept in general, not a gate that a
# browser in the background tips over (ra0's cpu_p95 4.2 to 5.1 ms at 4% of
# the processors busy, past 7 at 10%, the same code).
MET_SLACK = 1.25
MET_LIMITS = [
    ('ra0', 'gpu_p95', BUDGET_MS, "the card's time of a fight frame, 95%, run_ahead 0"),
    ('ra1', 'gpu_p95', BUDGET_MS, "the card's time of a fight frame, 95%, run_ahead 1"),
    ('ra2', 'gpu_p95', BUDGET_MS, "the card's time of a fight frame, 95%, run_ahead 2"),
    # Met since the step runs on a thread of its own (docs/PORT.md, "The step
    # on a thread of its own"): the window's thread's own work, a picture and
    # a record, held to a refresh whatever --report-only says. ra1's cpu_p95
    # 1.2 to 1.6 ms and settled_p99 1.6 to 2.8 in interleaved runs on a
    # machine other agents were using; the step's thread has its own
    # measures (step_p95, LIMITS).
    ('ra1', 'cpu_p95', BUDGET_MS, "the window's thread's work of a fight frame, 95%, run_ahead 1"),
    ('ra1', 'settled_p99', BUDGET_MS, "the window's thread, any blank after the fight's first second, 99%"),
    ('ra2', 'settled_p99', BUDGET_MS, 'the same, run_ahead 2'),
    ('ra1-stress', 'settled_p99', BUDGET_MS, 'the same, four fighters, the ground twice'),
    # Met on a quiet machine at c212be752 (2026-11-18) with room to spare:
    # ra0's processor 3.78, the stress case's card 4.28, ra1's worst settled
    # blank 1.45, the step's thread 3.56 (ra1) and 4.66 (ra2).
    ('ra0', 'cpu_p95', BUDGET_MS, 'the same, run_ahead 0'),
    ('ra1-stress', 'gpu_p95', BUDGET_MS, 'four fighters, the ground twice: the card'),
    ('ra1', 'settled_worst', 2 * BUDGET_MS, "the worst blank after the fight's first second"),
    ('ra1', 'step_p95', BUDGET_MS, "the step's own thread, a fight frame, 95%, run_ahead 1"),
    ('ra2', 'step_p95', BUDGET_MS, 'the same, run_ahead 2'),
    # The press's way to the screen (docs/PORT.md, "The press to the screen,
    # part by part"), ra1 paced at 144 a second with late_input: the read to
    # the refresh that shows the frame's first picture 10.6 to 12.1 ms on
    # average (12.4 to 14.5 before it), the refreshes no new picture
    # reached 0.3 to 9% (16 to 18% before), interleaved with other agents'
    # work on the machine; with the step slowed by a busy machine (5.7 ms
    # against 3.5) 13.4 ms and 12.6%. Two refreshes are 13.9 ms: the limits
    # catch a first picture a refresh later than it was (the late read's
    # first version, aiming too far, made it 17 to 20 ms), not the noise.
    ('ra1-paced', 'lag_shown_mean', 15.0, "a fight frame's input read to the screen, mean, paced at 144"),
    ('ra1-paced', 'missed_refreshes', 15.0, 'refreshes no new picture reached, percent, paced at 144'),
]
# The lighting's own share of the card (docs/PORT.md, "The lighting
# overhaul"): the passes it adds, summed over a fight frame, its 95th
# centile, held to LIGHT_BUDGET_MS whatever --report-only says. The stress
# case's (sixteen lights, four of them casting) is reported: its 95th
# centile rises with the cards' contention more than its median does.
LIGHT_PASSES = ('sun', 'lights', 'ssr', 'ao', 'bloom', 'post')
LIGHT_BUDGET_MS = 2.0
MET_LIMITS += [
    ('ra1', 'light_p95', LIGHT_BUDGET_MS, "the lighting's passes on the card, a fight frame, 95%, run_ahead 1"),
]
# The fight's first second is the end of its load: the limits on the
# worst blanks are of what follows it.
SETTLE_BLANKS = 60
# A blank of the fight, after its first second, over this is a stall
# (the card's driver doing something for the first time, a file read on
# the thread that draws): it fails the budget even with --report-only,
# since nothing the program does in play should take it.
STALL_HARD_MS = 50.0
# Limits that hold now, whatever --report-only says: (case, measure, at
# most, what it is).
HARD_LIMITS = [
    ('ra1', 'settled_stalls', 0, 'blanks of the fight after its first second over %.0f ms' % STALL_HARD_MS),
    ('ra0', 'settled_stalls', 0, 'the same, run_ahead 0'),
    ('ra2', 'settled_stalls', 0, 'the same, run_ahead 2'),
    ('ra1-stress', 'settled_stalls', 0, 'the same, four fighters, the ground twice'),
]
# --against: a measure that grows by more than this share and this many
# milliseconds is a regression.
REGRESSION_SHARE = 0.15
REGRESSION_MS = 0.5


# The machine's load over a case, kept beside its numbers: what else of
# ours was running when it started (by these names, in Windows and in WSL)
# and the share of all the processors busy while it ran. Over LOAD_BUSY_MAX
# per cent, or with another of these running, the case's numbers are
# reported as taken on a loaded machine.
OTHERS = ('rebushido', 'pcsx', 'redux')
LOAD_BUSY_MAX = 30.0


def system_times():
    """Windows' idle and all processors' time so far, or None."""
    if os.name != 'nt':
        return None
    import ctypes

    idle, kernel, user = (ctypes.c_ulonglong(), ctypes.c_ulonglong(), ctypes.c_ulonglong())
    if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
        return None
    # the kernel's time counts the idle time too
    return idle.value, kernel.value + user.value


def busy_since(before):
    now = system_times()
    if before is None or now is None or now[1] == before[1]:
        return None
    return round(100.0 * (1 - (now[0] - before[0]) / (now[1] - before[1])), 1)


def others_running():
    """The programs of ours running now, Windows' and WSL's."""
    seen = []
    for where, cmd in (('windows', ['tasklist', '/fo', 'csv', '/nh']), ('wsl', ['wsl.exe', '-e', 'ps', '-eo', 'comm='])):
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=20).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        for line in out.splitlines():
            name = line.split(',')[0].strip().strip('"').lower()
            if any(word in name for word in OTHERS):
                seen.append('%s (%s)' % (name, where))
    return seen


def git(*args, cwd=REPO):
    try:
        return subprocess.run(['git'] + list(args), cwd=cwd, capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


LABEL = ''


def commit_name():
    head = git('rev-parse', '--short=9', 'HEAD') or 'unknown'
    dirty = git('status', '--porcelain', '--untracked-files=no')
    return head + ('-dirty' if dirty else '') + LABEL


def cache_dir(name):
    return os.path.join(port_budget.main_checkout(), 'build', 'bench', name)


def card_name():
    """The PC's graphics card, or None where there is none to measure on."""
    if os.name != 'nt':
        return None
    try:
        out = subprocess.run(['powershell', '-NoProfile', '-Command',
                              '(Get-CimInstance Win32_VideoController).Name'],
                             capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    for line in out.splitlines():
        if any(word in line for word in ('NVIDIA', 'AMD', 'Radeon', 'Intel(R) Arc')):
            return line.strip()
    return None


def make_mods(main_repo):
    """build/bench/mods/: the shipped mods copied from the build, and the
    disc-made ones joined to the main checkout's tmp/mods/ (never copied,
    never deleted through)."""
    import shutil

    mods = os.path.join(OUT_DIR, 'mods')
    os.makedirs(mods, exist_ok=True)
    for name in SHIPPED_MODS:
        source = os.path.join(os.path.dirname(PROGRAM), 'mods', name)
        target = os.path.join(mods, name)
        if os.path.isdir(source):
            if os.path.isdir(target) and not os.path.islink(target):
                shutil.rmtree(target)
            shutil.copytree(source, target)
    missing = []
    for name in DISC_MADE_MODS:
        source = os.path.join(main_repo, 'tmp', 'mods', name)
        target = os.path.join(mods, name)
        if not os.path.isdir(source):
            missing.append(name)
            continue
        if not os.path.exists(target):
            subprocess.run(['cmd', '/c', 'mklink', '/J', target, source], capture_output=True, check=True)
    return mods, missing


def write_plan(out):
    import playtest

    playtest.PORT_PROGRAM = PROGRAM
    playtest.PORT_SYMBOLS = PROGRAM + '.sym'
    test = playtest.Test(os.path.join(REPO, 'tests', 'play', 'port_game.toml'))
    _, lines = test.plan(playtest.Disc('port'), playtest.header_names(), 1)
    where = {}
    for line in lines:
        words = line.split()
        if words and words[0] == 'var':
            where[words[1]] = (int(words[2]), int(words[3]))
    cps = [line.split()[1] for line in lines if line.startswith('cp ')]
    select = cps.index('select') + 1
    for name, value in port_budget.POKES:
        address, size = where[name]
        lines.append('poke %d 0 %d %d %d %d' % (select, FRAMES, address, size, value))
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def run(case, image, mods, plan, overrides=()):
    run_ahead, fighters, stress = CASES[case]
    folder = os.path.join(OUT_DIR, 'runs', case)
    os.makedirs(folder, exist_ok=True)
    config = os.path.join(folder, 'rebushido.cfg')
    # the bench is the card's, and draws what the window draws built in:
    # the sun's shadow maps (port_budget's processor-only runs keep cast,
    # which Mesa's software renderer draws at no cost to the processor)
    settings = port_budget.SETTINGS.format(run_ahead=run_ahead).replace(
        'fighter_models = on', 'fighter_models = ' + fighters).replace(
        'shadows = cast', 'shadows = map')
    for key, value in overrides:
        lines = [line for line in settings.splitlines() if line.split('=')[0].strip() != key]
        settings = '\n'.join(lines + ['%s = %s' % (key, value)]) + '\n'
    with open(config, 'w', encoding='utf-8', newline='\n') as f:
        f.write(settings)
    profile = os.path.join(folder, 'profile.txt')
    log = os.path.join(folder, 'log.txt')
    if not (PACED or case in PACED_CASES):
        pace = ['--fast']
    elif PACED_RATE and case not in PACED_CASES:
        pace = ['--display-rate', str(PACED_RATE)]
    elif case in PACED_CASES:
        pace = ['--display-rate', '144']
    else:
        # --rate 0: the PC's own display, which a hidden window's swaps
        # wait for
        pace = []
    cmd = [PROGRAM, '--hidden', '--image', image, '--config', config, '--mods', mods, '--plan', plan,
           '--out', folder, '--no-cards'] + pace + ['--run-ahead', str(run_ahead), '--window', WINDOW,
           '--frames', str(FRAMES), '--profile', profile, '--log', log]
    if stress:
        cmd += ['--bench-stress', stress]
    env = dict(os.environ, SDL_AUDIODRIVER='dummy')
    env.pop('SDL_VIDEODRIVER', None)
    start = time.time()
    with open(os.path.join(folder, 'output.txt'), 'w', encoding='utf-8', errors='replace') as out:
        process = subprocess.Popen(cmd, env=env, stdout=out, stderr=out, stdin=subprocess.DEVNULL)
        try:
            process.wait(timeout=RUN_SECONDS)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            sys.exit('port-bench: %s did not end in %d s (%s)' % (case, RUN_SECONDS, folder))
    # 1: ended at --frames
    if process.returncode not in (0, 1):
        sys.exit('port-bench: %s ended with %d (%s)' % (case, process.returncode, folder))
    # The window's thread runs none of the game's code while the step runs
    # beside it (docs/PORT.md, "The window's thread and the game's state"):
    # whatever --report-only says, a run that did is a fault in the port.
    with open(log, encoding='utf-8', errors='replace') as f:
        foreign = [line.strip() for line in f if 'beside the step, off its thread' in line]
    if foreign:
        sys.exit('port-bench: %s: %s (%s)' % (case, foreign[0], log))
    return folder, time.time() - start


def centile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))] if ordered else 0.0


def fight_blanks(log):
    """The fight's first and last blank, from the log's screens."""
    first = last = None
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            if 'game: screen 1, fight;' in line and first is None:
                first = int(line.split('blank ')[1].split(',')[0])
            elif first is not None and last is None and 'game: screen' in line and 'screen 1,' not in line:
                last = int(line.split('blank ')[1].split(',')[0]) - 1
    return first, last


def menu_blanks(log):
    """The menu's first and last blank before the fight, from the log's
    screens (a menu's frame is a blank), or None."""
    first = last = None
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            if 'game: screen' not in line:
                continue
            blank = int(line.split('blank ')[1].split(',')[0])
            if first is None and ', menu;' in line:
                first = blank
            elif first is not None and last is None:
                last = blank - 1
    return (first, last) if first is not None and last is not None else None


def case_paced_at_144(folder):
    return os.path.basename(folder) in PACED_CASES


def display_rate(log):
    """The PC's own display's refreshes a second, from the log."""
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            if 'video: display' in line and 'pictures a second' in line:
                return int(line.split(', ')[1].split()[0])
    return 60


def measure(folder, paced=False):
    """The case's numbers over its fight (the module's doc)."""
    first, last = fight_blanks(os.path.join(folder, 'log.txt'))
    if first is None:
        sys.exit('port-bench: %s never reached the fight (%s)' % (folder, os.path.join(folder, 'log.txt')))
    if last is None:
        last = FRAMES
    parts, gpu_parts, rows, gpu = [], [], {}, {}
    # the step's own thread, where it ran beside the window's: its parts
    # at each blank that took them (docs/PORT.md, "The step on a thread of
    # its own")
    steps = {}
    # each picture handed to the display: when its swap came back (ms),
    # the blank whose line follows it, and whether it was shown beside
    # the step (docs/PORT.md, "The step beside the pictures")
    swaps = []
    # when the game took its input at each frame's boundary (ms)
    boundaries = []
    # a held step's end: when the window's thread heard it, when the step's
    # thread took it up and said it was done, and when its record was drawn
    # (ms)
    ends, records = [], []
    # the keys read again late, just before the step began (late_input)
    lates = []
    blank = None
    with open(os.path.join(folder, 'profile.txt'), encoding='utf-8') as f:
        for line in f:
            words = line.split()
            if not words:
                continue
            if words[0] == 'b':
                blank = int(words[1])
            elif words[0] == 'p':
                swaps.append((int(words[1]) / 1000.0, (blank + 1) if blank is not None else 0,
                              len(words) > 3 and words[3] == '1',
                              int(words[4]) / 1000.0 if len(words) > 4 else 0.0,
                              int(words[5]) / 1000.0 if len(words) > 6 else None,
                              int(words[6]) / 1000.0 if len(words) > 6 else None,
                              int(words[7]) / 1000.0 if len(words) > 7 and words[7] != '0' else None))
            elif words[0] == 'k':
                boundaries.append((int(words[1]) / 1000.0, (blank + 1) if blank is not None else 0))
            elif words[0] == 'e' and len(words) > 3:
                ends.append(tuple(int(w) / 1000.0 for w in words[1:4]))
            elif words[0] == 'r':
                records.append(int(words[1]) / 1000.0)
            elif words[0] == 'l':
                lates.append(int(words[1]) / 1000.0)
            if words[0] == 'parts':
                parts = words[1:]
            elif words[0] == 'gpu_parts':
                gpu_parts = words[1:]
            elif words[0] == 'b' and first <= int(words[1]) <= last:
                rows[int(words[1])] = (int(words[2]), [int(w) / 1000.0 for w in words[3:]])
            elif words[0] == 'g' and first <= int(words[1]) <= last:
                gpu[int(words[1])] = [int(w) / 1000.0 for w in words[2:]]
            elif words[0] == 's' and first <= int(words[1]) <= last:
                steps[int(words[1])] = [int(w) / 1000.0 for w in words[2:]]
    work_parts = [i for i, name in enumerate(parts) if name not in ('wait', 'swap')]
    held_parts = [i for i, name in enumerate(parts) if name != 'wait']

    def work(values):
        return sum(values[i] for i in work_parts)

    def held(values):
        return sum(values[i] for i in held_parts)

    settled = [held(v) for b, (_, v) in rows.items() if b >= first + SETTLE_BLANKS]
    settled_stalls = sorted((b, round(held(v), 1)) for b, (_, v) in rows.items()
                            if b >= first + SETTLE_BLANKS and held(v) > STALL_HARD_MS)

    frames = [b for b, (new, _) in rows.items() if new]
    cpu = [work(rows[b][1]) for b in frames]
    all_cpu = [work(v) for _, v in rows.values()]
    card = [sum(gpu[b]) for b in frames if b in gpu]
    light_index = [i for i, name in enumerate(gpu_parts) if name in LIGHT_PASSES]
    light = [sum(gpu[b][i] for i in light_index if i < len(gpu[b])) for b in frames if b in gpu]
    all_card = {b: sum(v) for b, v in gpu.items()}
    over = sum(1 for b, (_, v) in rows.items() if work(v) > BUDGET_MS or all_card.get(b, 0.0) > BUDGET_MS)
    # a stall is against a frame's own work: the blanks between frames
    # draw a picture and little else
    median_work = statistics.median(cpu) if cpu else 0.0
    stalls = []
    for b in sorted(rows):
        w = work(rows[b][1])
        if w > STALL_TIMES * median_work and w > BUDGET_MS:
            big = max(work_parts, key=lambda i: rows[b][1][i])
            stalls.append((b, round(w, 2), parts[big], round(rows[b][1][big], 2)))
    # The time from one picture handed to the display to the next, over
    # the fight after its first second: what a refresh costs, whichever
    # thread drew the picture. With the pictures beside the step, a
    # frame's blank has two (docs/PORT.md, "The step beside the
    # pictures"); without, a blank's work is the gap before its picture.
    gaps = [now[0] - was[0] - now[3] for was, now in zip(swaps, swaps[1:])
            if first + SETTLE_BLANKS <= now[1] <= last]
    beside = sum(1 for swap in swaps if swap[2] and first <= swap[1] <= last)
    # a boundary read late is read when it was read again
    late_at = []
    for n, (at_ms, at) in enumerate(boundaries):
        i = bisect.bisect_right(lates, at_ms)
        before = boundaries[n + 1][0] if n + 1 < len(boundaries) else at_ms + LATE_MOST_MS
        late_at.append((lates[i] if i < len(lates) and lates[i] < before and lates[i] - at_ms < LATE_MOST_MS
                        else at_ms, at))
    rate = 144 if case_paced_at_144(folder) else PACED_RATE
    if rate == 0:
        rate = -display_rate(os.path.join(folder, 'log.txt'))
    paced = paced_measures(swaps, late_at, ends, records, first + SETTLE_BLANKS, last,
                           frames, rate) if paced else {}
    menu = menu_blanks(os.path.join(folder, 'log.txt')) if paced else None
    if menu:
        # the menus before the fight: every blank a frame
        in_menu = paced_measures(swaps, late_at, ends, records, menu[0] + SETTLE_BLANKS, menu[1],
                                 [], rate)
        paced.update(('menu_' + key, value) for key, value in in_menu.items() if key.startswith('lag_'))
    if paced and late_at:
        paced['late_mean'] = statistics.mean(t - b[0] for t, b in zip((t for t, _ in late_at), boundaries)
                                             if first + SETTLE_BLANKS <= b[1] <= last)
    # The step on its own thread: its work, a step a blank that took it.
    # The window's thread's numbers above are then that thread's alone.
    step_work = [work(v) for b, v in steps.items() if b >= first + SETTLE_BLANKS]
    on_its_own = {
        'step_p50': statistics.median(step_work),
        'step_p95': centile(step_work, 0.95),
        'step_p99': centile(step_work, 0.99),
        'step_worst': max(step_work),
        'step_parts': {parts[i]: statistics.median(v[i] for v in steps.values()) for i in work_parts},
    } if step_work else {}
    paced = dict(paced, **on_its_own)
    said = port_budget.counts(os.path.join(folder, 'output.txt'))
    return dict(paced, **{
        'fight': [first, last],
        'blanks': len(rows),
        'frames': len(frames),
        'cpu_p50': statistics.median(cpu) if cpu else 0.0,
        'cpu_p95': centile(cpu, 0.95),
        'cpu_p99': centile(cpu, 0.99),
        'all_cpu_p95': centile(all_cpu, 0.95),
        'gpu_p50': statistics.median(card) if card else 0.0,
        'gpu_p95': centile(card, 0.95),
        'gpu_p99': centile(card, 0.99),
        'worst': max(all_cpu) if all_cpu else 0.0,
        'gpu_worst': max(all_card.values()) if all_card else 0.0,
        'light_p95': centile(light, 0.95),
        'hitches': 100.0 * over / len(rows) if rows else 0.0,
        'stalls': stalls[:12],
        'settled_p99': centile(settled, 0.99),
        'settled_worst': max(settled) if settled else 0.0,
        'settled_stalls': len(settled_stalls),
        'settled_stall_blanks': settled_stalls[:12],
        'cpu_parts': {parts[i]: statistics.median(rows[b][1][i] for b in frames) for i in work_parts} if frames else {},
        'gpu_parts': {name: statistics.median(gpu[b][i] for b in frames if b in gpu)
                      for i, name in enumerate(gpu_parts)} if card else {},
        'state_ms': said.get('state'),
        'gap_p50': statistics.median(gaps) if gaps else 0.0,
        'gap_p95': centile(gaps, 0.95),
        'gap_p99': centile(gaps, 0.99),
        'gap_worst': max(gaps) if gaps else 0.0,
        'gap_over': 100.0 * sum(1 for g in gaps if g > BUDGET_MS) / len(gaps) if gaps else 0.0,
        'pictures_beside': beside,
    })


def paced_measures(swaps, boundaries, ends, records, first, last, frames, rate):
    """--paced: the refreshes of the made-up display no new picture
    reached, and a press's way to the picture, part by part (docs/PORT.md,
    "Input lag"): for each frame's boundary, the game taking its input
    (`k`), the step's thread taking the step up and saying it was done and
    the window's thread hearing it (`e`), its record drawn (`r`), the
    frame's first picture begun and handed over and its swap back (`p`),
    and the refresh that shows it."""
    # the PC's own display (rate 0 asked for): its refreshes are the
    # swaps' return
    real = rate < 0
    period = 1000.0 / abs(rate)
    # a swap comes back just after the refresh it waited for
    shown = sorted({int(swap[0] / period) for swap in swaps if first <= swap[1] <= last})
    if not shown:
        return {}
    seen = set(shown)
    missed = sum(1 for i in range(shown[0], shown[-1] + 1) if i not in seen)
    total = shown[-1] - shown[0] + 1
    # A frame's boundary: the first of its blank's swaps that is not one
    # shown beside the step is the first picture of the frame (with
    # run-ahead the step's own frame is on show from its first blank).
    # (with `frame_rate original` no blank says it drew a frame: every
    # boundary of the fight is one)
    frame_blanks = set(frames) if frames else {at for _, at in boundaries}
    swap_times = [swap[0] for swap in swaps]
    end_times = [end[0] for end in ends]
    lags = []
    parts = {name: [] for name, _ in LAG_PARTS}
    for at_ms, at in boundaries:
        if not (first <= at <= last) or at not in frame_blanks:
            continue
        i = bisect.bisect_right(swap_times, at_ms)
        while i < len(swaps) and (swaps[i][2] or swaps[i][1] < at):
            i += 1
        if i == len(swaps):
            continue
        swap, _, _, _, began, handed, drawn = swaps[i]
        lags.append(int(swap / period) - int(at_ms / period))
        # the refresh that shows it: the one the swap waited for, or with
        # nothing waited for, the swap itself (torn in)
        on_screen = int(swap / period) * period if VSYNC and not real else swap
        parts['shown'].append(on_screen - at_ms)
        if began is None:
            continue
        j = bisect.bisect_right(end_times, at_ms)
        k = bisect.bisect_right(records, at_ms)
        if j < len(ends) and ends[j][0] < swap and k < len(records) and records[k] < swap:
            heard, woke, ended = ends[j]
            parts['to_step'].append(woke - at_ms)
            parts['step'].append(ended - woke)
            parts['from_step'].append(heard - ended)
            parts['record'].append(records[k] - heard)
            ready = records[k]
        else:
            ready = at_ms
        parts['to_picture'].append(began - ready)
        parts['picture'].append(handed - began)
        if drawn is not None:
            parts['card'].append(drawn - handed)
            parts['swap'].append(swap - drawn)
        else:
            parts['swap'].append(swap - handed)
    # A press comes at any moment and waits for the next boundary's read: on
    # average the sum of the gaps' squares over twice their sum.
    times = [t for t, at in boundaries if first <= at <= last]
    # (a gap of more than four blanks is a load, where nothing is pressed)
    gaps = [b - a for a, b in zip(times, times[1:]) if b - a < 70.0]
    wait = sum(g * g for g in gaps) / (2 * sum(gaps)) if gaps else 0.0
    measures = {
        'rate': abs(rate),
        'missed_refreshes': 100.0 * missed / total,
        'refreshes': total,
        'lag_p50': statistics.median(lags) if lags else 0.0,
        'lag_mean': statistics.mean(lags) if lags else 0.0,
        'lag_p95': centile(lags, 0.95),
        'lag_read_wait': wait,
    }
    for name, values in parts.items():
        if values:
            measures['lag_%s_mean' % name] = statistics.mean(values)
            measures['lag_%s_p95' % name] = centile(values, 0.95)
    if parts['shown']:
        measures['lag_press_mean'] = wait + measures['lag_shown_mean']
    return measures


def report(name, m):
    print('%s: fight blanks %d-%d, %d of them frames' % (name, m['fight'][0], m['fight'][1], m['frames']))
    print('  processor: median %.2f  95%% %.2f  99%% %.2f  worst blank %.2f   any blank 95%% %.2f'
          % (m['cpu_p50'], m['cpu_p95'], m['cpu_p99'], m['worst'], m['all_cpu_p95']))
    print('  card:      median %.2f  95%% %.2f  99%% %.2f  worst blank %.2f   the lighting 95%% %.2f'
          % (m['gpu_p50'], m['gpu_p95'], m['gpu_p99'], m['gpu_worst'], m.get('light_p95', 0.0)))
    print('  hitches (over %.2f ms): %.1f%% of the fight\'s blanks' % (BUDGET_MS, m['hitches']))
    print('  processor parts (median ms): ' + ' '.join(
        '%s %.2f' % kv for kv in sorted(m['cpu_parts'].items(), key=lambda kv: -kv[1]) if kv[1] >= 0.01))
    print('  card passes (median ms):     ' + ' '.join(
        '%s %.2f' % kv for kv in sorted(m['gpu_parts'].items(), key=lambda kv: -kv[1]) if kv[1] >= 0.01))
    print('  after the first second: 99%% %.2f  worst %.2f; blanks over %.0f ms: %d%s'
          % (m['settled_p99'], m['settled_worst'], STALL_HARD_MS, m['settled_stalls'],
             ' (' + ', '.join('blank %d %.1f ms' % s for s in m['settled_stall_blanks']) + ')'
             if m['settled_stalls'] else ''))
    if 'missed_refreshes' in m:
        print('  paced at %d a second: %.1f%% of %d refreshes after the first second reached by no new picture;'
              ' a frame\'s input to its first picture: median %.1f, mean %.2f, 95%% %.1f refreshes'
              % (m.get('rate', PACED_RATE), m['missed_refreshes'], m['refreshes'], m['lag_p50'], m['lag_mean'], m['lag_p95']))
        if 'lag_shown_mean' in m:
            print('  a press to the screen, mean ms: %.2f = waiting for the read %.2f + the read to the screen %.2f'
                  ' (95%% %.2f; read %.2f ms after the boundary); of that: %s' % (
                      m['lag_press_mean'], m['lag_read_wait'], m['lag_shown_mean'], m['lag_shown_p95'],
                      m.get('late_mean', 0.0),
                      ', '.join('%s %.2f' % (what, m['lag_%s_mean' % name]) for name, what in LAG_PARTS
                                if name != 'shown' and 'lag_%s_mean' % name in m)))
        if 'menu_lag_shown_mean' in m:
            print('  in the menus: a press to the screen %.2f = waiting for the read %.2f + the read to the screen %.2f'
                  ' (95%% %.2f)' % (m['menu_lag_press_mean'], m['menu_lag_read_wait'], m['menu_lag_shown_mean'],
                                   m['menu_lag_shown_p95']))
    if 'gap_p50' in m:
        print('  picture to picture, after the first second: median %.2f  95%% %.2f  99%% %.2f  worst %.2f;'
              ' over %.2f ms: %.1f%%; %d pictures beside the step'
              % (m['gap_p50'], m['gap_p95'], m['gap_p99'], m['gap_worst'], BUDGET_MS, m['gap_over'],
                 m['pictures_beside']))
    if 'step_p50' in m:
        print("  the step's own thread, after the first second: median %.2f  95%% %.2f  99%% %.2f  worst %.2f;"
              ' parts (median ms): %s' % (m['step_p50'], m['step_p95'], m['step_p99'], m['step_worst'], ' '.join(
                  '%s %.2f' % kv for kv in sorted(m['step_parts'].items(), key=lambda kv: -kv[1]) if kv[1] >= 0.01)))
    if m['stalls']:
        print('  stalls: ' + ', '.join('blank %d %.1f ms (%s %.1f)' % s for s in m['stalls']))


def main():
    global PACED, PACED_RATE, VSYNC
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--case', action='append', choices=sorted(CASES), help='a case to run (again: more)')
    ap.add_argument('--budget', action='store_true', help="the gate's cases, held to LIMITS")
    ap.add_argument('--report-only', action='store_true', help='say what is over the budget, and pass')
    ap.add_argument('--against', metavar='REV', help="another commit's numbers (build/bench/), regressions")
    ap.add_argument('--set', action='append', default=[], metavar='KEY=VALUE',
                    help="a setting of rebushido.cfg changed for every case (a trial: its numbers are kept "
                         "as the commit's name with +KEY-VALUE)")
    ap.add_argument('--reuse', action='store_true', help='measure the runs already in build/bench/runs/')
    ap.add_argument('--program', help='another build of the window to measure (a variant of the flags)')
    ap.add_argument('--label', help="kept as the commit's name and +LABEL, beside the commit's own numbers")
    ap.add_argument('--paced', action='store_true',
                    help='at the game\'s pace against a display of %d a second (the module\'s doc)' % PACED_RATE)
    ap.add_argument('--rate', type=int, default=PACED_RATE,
                    help="--paced: the made-up display's refreshes a second (%d)" % PACED_RATE)
    args = ap.parse_args()
    PACED = args.paced
    PACED_RATE = args.rate
    VSYNC = not any(item.replace(' ', '') == 'vsync=off' for item in args.set)
    global PROGRAM
    if args.program:
        PROGRAM = os.path.abspath(args.program)
    global LABEL
    LABEL = (('+' + args.label if args.label else '') + ('+paced' if args.paced else '')
             + ('%d' % PACED_RATE if args.paced and PACED_RATE != 144 else ''))
    cases = args.case or (BUDGET_CASES if args.budget else DEFAULT_CASES)
    main_repo = port_budget.main_checkout()
    image = port_budget.find(os.environ.get('REBUSHIDO_RETAIL'),
                             os.path.join(REPO, 'Bushido Blade 2 (USA).cue'),
                             os.path.join(main_repo, 'Bushido Blade 2 (USA).cue'))
    card = card_name()
    for what, there in (('a graphics card to measure on (Windows, NVIDIA, AMD or Intel)', card),
                        ("the window's Windows build (make port-sdl-windows)", os.path.exists(PROGRAM) or None),
                        ('the disc image (REBUSHIDO_RETAIL)', image)):
        if there is None:
            print('port-bench: skipped: there is no %s' % what)
            return 0
    os.makedirs(OUT_DIR, exist_ok=True)
    mods, missing = make_mods(main_repo)
    if missing:
        print('port-bench: without the mods %s (not in tmp/mods/)' % ', '.join(missing))
    plan = os.path.join(OUT_DIR, 'plan.txt')
    write_plan(plan)
    print('port-bench: %s, %s, the window %s, unpaced; commit %s' % (card, os.path.relpath(PROGRAM, REPO),
                                                                     WINDOW, commit_name()), flush=True)
    theirs = None
    if args.against:
        # the commit's own numbers, or its nearest ancestor's (tools/baseline.py)
        found = baseline.nearest(args.against, lambda commit: os.path.join(cache_dir(commit[:9]), 'results.json'))
        if not found:
            print('port-bench: no numbers of %s or of its last %d ancestors (%s): run tools/port_bench.py '
                  'in a checkout of it' % (args.against, baseline.ANCESTORS, cache_dir('')))
        else:
            theirs_path, commit, n = found
            with open(theirs_path, encoding='utf-8') as f:
                stored = json.load(f)
            theirs = stored['cases']
            print('port-bench: against %s' % baseline.describe(args.against, commit, n))
            changed = baseline.stale(stored.get('code', {}), baseline.code_hashes())
            if changed:
                # numbers another bench measured are not these numbers' to be held to
                print('port-bench: STALE: the bench\'s code is not what measured %s\'s numbers: %s'
                      % (commit[:9], ', '.join(changed) if stored.get('code') else
                         'they were kept before the bench kept its code\'s hash'))
                return 2
            busy = [case for case, m in theirs.items() if m.get('load') and
                    (m['load']['others'] or (m['load']['busy'] or 0) > LOAD_BUSY_MAX)]
            if busy:
                print('port-bench: (%s\'s numbers of %s were measured on a loaded machine)'
                      % (commit[:9], ', '.join(sorted(busy))))
    # it times: alone on the card, no prover or other bench of any checkout
    # beside it (tools/card_lock.py)
    if not args.reuse:
        import card_lock

        card_lock.hold(True, 'port-bench')
    results = {}
    loaded = []
    for case in cases:
        if args.reuse:
            folder, seconds = os.path.join(OUT_DIR, 'runs', case), 0.0
            load = None
        else:
            seen = others_running()
            before = system_times()
            folder, seconds = run(case, image, mods, plan, [tuple(s.split("=", 1)) for s in args.set])
            load = {'others': seen, 'busy': busy_since(before)}
        results[case] = measure(folder, PACED or case in PACED_CASES)
        report('%s (%.0f s)' % (case, seconds), results[case])
        if load is not None:
            results[case]['load'] = load
            busy = load['busy']
            print('  load: %s of the processors busy; %s' % ('?' if busy is None else '%.0f%%' % busy,
                                                           ', '.join(seen) if seen else 'nothing else of ours running'))
            if seen or (busy is not None and busy > LOAD_BUSY_MAX):
                loaded.append(case)
        sys.stdout.flush()
    if loaded:
        print('port-bench: WARNING: the machine was loaded during %s (another program of ours, or over %.0f%% of '
              'the processors busy): their numbers are not to be trusted' % (', '.join(loaded), LOAD_BUSY_MAX))
    name = commit_name()
    if args.set:
        # a trial's numbers are kept apart from the commit's own
        name += '+' + ','.join(item.replace('=', '-').replace(' ', '') for item in args.set)
    keep = cache_dir(name)
    os.makedirs(keep, exist_ok=True)
    old = {}
    path = os.path.join(keep, 'results.json')
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            old = json.load(f).get('cases', {})
    old.update(results)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump({'commit': name, 'card': card, 'window': WINDOW, 'cases': old,
                   'code': baseline.code_hashes()}, f, indent=1)
    print('port-bench: the numbers in %s' % path)
    failed = False
    if theirs is not None:
        for case, m in results.items():
            for key in ('cpu_p50', 'cpu_p95', 'gpu_p50', 'gpu_p95', 'worst', 'settled_p99',
                        'gap_p95', 'gap_p99'):
                if case in theirs and key in theirs[case]:
                    was, now = theirs[case][key], m[key]
                    worse = now > was * (1 + REGRESSION_SHARE) and now - was > REGRESSION_MS
                    failed = failed or worse
                    print('  %-11s %-8s %7.2f -> %7.2f%s' % (case, key, was, now, '  REGRESSION' if worse else ''))
    if args.budget:
        print('port-bench: the budget (2560x1440 at 144 a second: %.2f ms a refresh)' % BUDGET_MS)
        for case, key, most, what in LIMITS:
            if case not in results:
                continue
            # the step's own measures only where it ran on its own thread
            value = results[case].get(key, 0.0)
            over = value > most
            failed = failed or over
            print('  %-11s %-12s %7.2f (at most %6.2f) %-5s %s' % (case, key, value, most,
                                                                 'OVER' if over else 'ok', what))
    stalled = False
    if args.budget:
        print('port-bench: what is met, which fails whatever --report-only says')
        for case, key, most, what in MET_LIMITS:
            if case not in results:
                continue
            value = results[case][key]
            over = value > most
            if over and case in PACED_CASES and case in loaded:
                # a paced run's measures are the clock's, which another
                # program on the machine lengthens: said, not held
                print('  %-11s %-12s %7.2f (at most %6.2f) %-5s %s' % (case, key, value, most,
                                                                     'over, on a loaded machine: not held', what))
                continue
            if over and value <= most * MET_SLACK:
                # the owner (2026-11-19): not so strict, as long as it stays
                # generally fast; a little over, as a busy machine makes it
                print('  %-11s %-12s %7.2f (at most %6.2f) %-5s %s' % (case, key, value, most,
                                                                     'over, within the slack: not held', what))
                continue
            stalled = stalled or over
            print('  %-11s %-12s %7.2f (at most %6.2f) %-5s %s' % (case, key, value, most,
                                                                 'OVER' if over else 'ok', what))
        print('port-bench: stalls, which fail whatever --report-only says')
        for case, key, most, what in HARD_LIMITS:
            if case not in results:
                continue
            value = results[case][key]
            over = value > most
            stalled = stalled or over
            print('  %-11s %-14s %5d (at most %d) %-5s %s' % (case, key, value, most,
                                                           'OVER' if over else 'ok', what))
    if theirs is not None:
        for case, m in results.items():
            was = theirs.get(case, {}).get('settled_stalls')
            if was is not None and m['settled_stalls'] > was:
                print('  %-11s a stall more than %s: %d -> %d  REGRESSION'
                      % (case, args.against, was, m['settled_stalls']))
                stalled = True
    if loaded:
        print('port-bench: (measured on a loaded machine: %s; see "load" above)' % ', '.join(loaded))
    if stalled:
        print('port-bench: FAILED (a stall, or a measure that was met)')
        return 1
    if failed and args.report_only:
        print('port-bench: over the budget, reported only (--report-only)')
        return 0
    print('port-bench: %s' % ('FAILED' if failed else 'OK'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
