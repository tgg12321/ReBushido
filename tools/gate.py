#!/usr/bin/env python3
"""The whole gate of a change to `main`, its steps side by side where
they share no output (BUILD.md, "The gate"):

    python tools/gate.py                 every step, and a summary
    python tools/gate.py --owner         and then the owner's copy of the
                                         window's build (tools/owner_copy.py)
    python tools/gate.py --owner --push  and then git push
    python tools/gate.py --changed REV   only the play tests what changed
                                         since REV reaches (tools/play/affected.py)
    python tools/gate.py --list          the steps, and what each waits for

From Windows or inside WSL; a step of the builds runs in WSL (`wsl -e`
from Windows), the play tests in Windows' Python, which runs the
emulator. The steps are the eleven checks of tools/checks.sh, read from
it, so that the list is the one CI runs; the window's two builds; the
play tests' discs; `format.py --check --all`; port-rollback; port-stage-select; port-net;
port-net-windows (tools/port_net_lobby.py windows: the Windows programs
playing each other directly and through a lobby server run here, the
menu's restart and every refusal on the screen; from Windows only);
port-update (tools/port_update_test.py: the Windows program's updater
against a release server and key of its own; from Windows only);
port-menus (tools/port_menus_test.py: the window's pages beside the
running game, the settings menu and the online pages over the main menu
and the pause, held to the game's music and menu sounds, and a fight
paused with them kept and played back as a replay);
port-budget (tools/port_budget.py: the window's processor time for a
fight with run-ahead, held to a budget); port-bench (tools/port_bench.py:
the Windows build on the real graphics card, the processor's and the
card's time held to the 144 Hz budget, reported only until the targets
are met); port-pause (tools/port_pause_prove.py: the pause menu on the
card at every run-ahead); and the play tests (tools/playtest.py -j). Each waits only for the
steps whose outputs it reads, and never runs beside one that writes
what it writes (WAITS).

Each step's output goes to build/gate/<step>.log, and the summary to
build/gate/summary.txt. At the first step that fails no step is started
any more; those running are let finish, and the summary says which were
not run. Exits 1 if a step failed.
"""
import argparse
import os
import re
import subprocess
import sys
import threading
import time
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, 'build', 'gate')
CHECKS = os.path.join(REPO, 'tools', 'checks.sh')

# What each step waits for: the steps whose outputs it reads or whose
# outputs it writes too. A check of tools/checks.sh that is not here
# waits for every build (BUILDS).
WAITS = {
    'counts': [],
    'lint': [],
    'modern': [],
    'test': [],
    'rb-modern': [],
    'format': [],
    # the matching build, then what reads its objects: the relink, and the
    # GTE probe (playtest-console), which links some of them
    'game': [],
    'overlay': [],
    'shift': ['game'],
    # the ReBushido build compiles quick boot's files into build/quickboot/,
    # where the quick-boot discs are built
    'rb-build': [],
    # the GTE probe's numbers are three of the host tests of
    # build/rebushido/tests/, which it builds again: not beside rb-test
    'rb-test': [],
    'discs': ['game', 'rb-build', 'rb-test'],
    'port': [],
    'port-sdl': [],
    'port-sdl-windows': [],
    'port-rollback': ['port'],
    'port-stage-select': ['port'],
    'port-net': ['port', 'port-sdl'],
    # the Windows programs meeting each other, by address and through a
    # lobby server run here, the menu's restart and the refusals
    # (tools/port_net_lobby.py windows); after port-net, not beside it:
    # the two together ran some fifty programs at once and timed out
    # under the load (2026-11-20)
    'port-net-windows': ['port-sdl-windows', 'port-net'],
    # the Windows program's updater against a release server of its own
    # (tools/port_update_test.py), in build/tests/port_update/
    'port-update': ['port-sdl-windows'],
    # the window's pages and their sound (tools/port_menus_test.py), in
    # build/tests/port_menus/: the Windows program from Windows, the
    # Linux one elsewhere
    'port-menus': ['port-sdl', 'port-sdl-windows'],
    # the window's build timed: after every other step, alone on the
    # machine, so that its milliseconds are its own (ALONE)
    'port-budget': ['port-sdl'],
    # the Windows build on the card, alone as well
    'port-bench': ['port-sdl-windows'],
    # the pause menu on the card at every run-ahead: on the card the bench
    # measures, so after it and alone too
    'port-pause': ['port-sdl-windows'],
    'playtest': ['port', 'discs'],
}
# Steps that wait for every other step and run with nothing beside them,
# one after another in this order.
ALONE = ['port-budget', 'port-bench', 'port-pause']
# Steps that run what was built, in the checkout itself: the others build in
# tools/mirror.sh's copy of it.
IN_PLACE = ['port-rollback', 'port-stage-select', 'port-net', 'port-net-windows', 'port-update', 'port-menus',
            'port-budget',
            'port-bench', 'port-pause']
BUILDS = ['game', 'rb-build', 'discs', 'port', 'port-sdl', 'port-sdl-windows']
# The steps of the gate's own beside the checks: (name, command, where).
OWN = [
    # from Windows by Windows' Python, which lists the files with the
    # checkout's own git and runs clang-format in WSL
    ('format', [None, 'tools/format.py', '--check', '--all'], 'host'),
    ('discs', ['make', '-j8', 'playtest-console'], 'wsl'),
    ('port-sdl', ['make', 'port-sdl'], 'wsl'),
    ('port-sdl-windows', ['make', 'port-sdl-windows'], 'wsl'),
    ('port-rollback', ['make', 'port-rollback'], 'wsl'),
    ('port-stage-select', ['make', 'port-stage-select'], 'wsl'),
    ('port-net', None, 'wsl'),  # with its ports (net_base)
    ('port-net-windows', None, 'host'),  # with its ports (net_base) and Windows' Python
    # skips itself off Windows
    ('port-update', [None, 'tools/port_update_test.py'], 'host'),
    ('port-menus', [None, 'tools/port_menus_test.py'], 'host'),
    ('port-budget', ['python3', 'tools/port_budget.py'], 'wsl'),
    # --report-only until the owner's targets are met (docs/PORT.md,
    # "Measuring on the card"); skips itself where there is no card
    ('port-bench', [None, 'tools/port_bench.py', '--budget', '--report-only'], 'host'),
    # skips itself where there is no card
    ('port-pause', [None, 'tools/port_pause_prove.py'], 'host'),
    ('playtest', None, 'host'),  # with its runs at once
]


def checks_of(path=CHECKS):
    """[(name, command)] of tools/checks.sh's `check NAME COMMAND...`
    lines, in its order."""
    found = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            m = re.match(r'check\s+(\S+)\s+(.+?)\s*$', line)
            if m:
                found.append((m.group(1), m.group(2).split()))
    return found


# port-net-windows takes its ports this far below port-net's base (and
# tools/port_net.py's lobby cases LOBBY_BELOW, 20000).
WINDOWS_BELOW = 10000


def net_base():
    """port-net's first UDP port: one of ninety bases 200 apart from
    47000, chosen by the checkout's path, so that two checkouts' gates at
    once do not meet (tools/port_net.py takes 144 from its base: 24 for
    each of its six whats but the lobby's)."""
    return 47000 + 200 * (zlib.crc32(REPO.replace('\\', '/').lower().encode()) % 90)


def in_wsl(cmd, mirror=True):
    """The command as this program starts it, in WSL with the venv; a build
    where the build runs (tools/mirror.sh: a copy on WSL's own disk, build/
    brought back when the step ends), what runs the built programs here."""
    script = '[ -f .venv/bin/activate ] && source .venv/bin/activate; exec %s"$@"' % (
        'bash tools/mirror.sh run ' if mirror else '')
    if os.name == 'nt':
        return ['wsl', '-e', 'bash', '-c', script, 'gate'] + cmd
    return ['bash', '-c', script, 'gate'] + cmd


class Step:
    def __init__(self, name, cmd, where):
        self.name, self.cmd, self.where = name, cmd, where
        self.waits = WAITS.get(name, BUILDS)
        self.state = 'waiting'  # running, ok, FAILED, not run, skipped
        self.seconds = 0.0
        self.log = os.path.join(OUT_DIR, name + '.log')


def steps_of(args):
    steps = [Step(name, cmd, 'wsl') for name, cmd in checks_of()]
    python = sys.executable if os.name == 'nt' else 'python3'
    for name, cmd, where in OWN:
        if name == 'port-net':
            cmd = ['make', 'port-net', 'PORT_NET_BASE=%d' % args.net_base]
        elif name == 'port-net-windows':
            if os.name != 'nt':
                continue
            cmd = [python, 'tools/port_net_lobby.py', 'windows', '--port-base', str(args.net_base - WINDOWS_BELOW)]
        elif name == 'playtest':
            # --strict: no emulator or disc here is a failure, not a pass
            cmd = [python, 'tools/playtest.py', '--strict', '-j', str(args.jobs)]
            if args.changed:
                cmd += ['--changed', args.changed]
        else:
            cmd = [python if word is None else word for word in cmd]
        steps.append(Step(name, cmd, where))
    names = {s.name for s in steps}
    for step in steps:
        if step.name in ALONE:
            before = ALONE[:ALONE.index(step.name)]
            step.waits = [s.name for s in steps if s.name not in ALONE or s.name in before]
        step.waits = [w for w in step.waits if w in names]
        if step.name in args.skip:
            step.state = 'skipped'
    return steps


def run_step(step):
    cmd = in_wsl(step.cmd, step.name not in IN_PLACE) if step.where == 'wsl' else step.cmd
    start = time.time()
    with open(step.log, 'wb') as log:
        log.write(('$ %s\n' % ' '.join(step.cmd)).encode())
        log.flush()
        code = subprocess.call(cmd, cwd=REPO, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    step.seconds = time.time() - start
    return code


def tail(path, lines=25):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read().splitlines()[-lines:]


def run_all(steps, say):
    """Runs the steps, each as soon as what it waits for has passed;
    after a failure starts none. Returns True if every step that was not
    skipped passed."""
    by_name = {s.name: s for s in steps}
    lock = threading.Condition()
    failed = []

    def worker(step):
        code = run_step(step)
        with lock:
            step.state = 'ok' if code == 0 else 'FAILED'
            if code != 0:
                failed.append(step)
            lock.notify_all()
        say('%-17s %s (%ds)' % (step.name, step.state, step.seconds))
        if code != 0:
            say('\n'.join('    | ' + line for line in tail(step.log)))

    threads = []
    with lock:
        while True:
            if not failed:
                for step in steps:
                    if step.state != 'waiting':
                        continue
                    states = [by_name[w].state for w in step.waits]
                    if all(s in ('ok', 'skipped') for s in states):
                        step.state = 'running'
                        say('%-17s started' % step.name)
                        t = threading.Thread(target=worker, args=(step,))
                        t.start()
                        threads.append(t)
            if not any(s.state == 'running' for s in steps):
                break
            lock.wait()
    for t in threads:
        t.join()
    for step in steps:
        if step.state == 'waiting':
            step.state = 'not run'
    return not failed


def main():
    ap = argparse.ArgumentParser(description="The whole gate, its steps side by side where they can be.")
    ap.add_argument('--list', action='store_true', help='the steps and what each waits for, and stop')
    ap.add_argument('--jobs', '-j', type=int, default=4, help='play test runs at once (playtest.py -j; default 4)')
    ap.add_argument('--changed', metavar='REV', help='only the play tests what changed since REV reaches')
    ap.add_argument('--skip', action='append', default=[], metavar='STEP', help='a step not to run (again: more)')
    ap.add_argument('--net-base', type=int, default=net_base(),
                    help="port-net's first UDP port (default %d, from this checkout's path)" % net_base())
    ap.add_argument('--owner', action='store_true', help="then the owner's copy (tools/owner_copy.py)")
    ap.add_argument('--push', action='store_true', help='then git push')
    args = ap.parse_args()
    steps = steps_of(args)
    unknown = set(args.skip) - {s.name for s in steps}
    if unknown:
        ap.error('--skip: no step %s' % ', '.join(sorted(unknown)))
    if args.list:
        for step in steps:
            print('%-17s %-6s waits for %-28s %s' % (step.name, step.where, ', '.join(step.waits) or '-',
                                                    ' '.join(step.cmd)))
        return 0
    os.makedirs(OUT_DIR, exist_ok=True)
    printing = threading.Lock()

    def say(text):
        with printing:
            print(text, flush=True)

    start = time.time()
    say('gate: %d steps, logs in build/gate/' % len(steps))
    passed = run_all(steps, say)
    lines = ['', '  %-17s %-8s %s' % ('step', 'result', 'time')]
    lines += ['  %-17s %-8s %4ds' % (s.name, s.state, s.seconds) for s in steps]
    lines.append('  %ds in all' % (time.time() - start))
    if passed and args.owner:
        code = subprocess.call([sys.executable, 'tools/owner_copy.py'], cwd=REPO)
        lines.append('  owner copy: %s' % ('ok' if code == 0 else 'FAILED'))
        passed = code == 0
    if passed and args.push:
        code = subprocess.call(['git', 'push'], cwd=REPO)
        lines.append('  push: %s' % ('ok' if code == 0 else 'FAILED'))
        passed = code == 0
    lines.append('OK: the gate passes' if passed else 'FAILED: see build/gate/<step>.log')
    say('\n'.join(lines))
    with open(os.path.join(OUT_DIR, 'summary.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines).lstrip('\n') + '\n')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
