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
play tests' discs; `format.py --check --all`; port-rollback; port-net;
and the play tests (tools/playtest.py -j). Each waits only for the
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
    'port-net': ['port', 'port-sdl'],
    'playtest': ['port', 'discs'],
}
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
    ('port-net', None, 'wsl'),  # with its ports (net_base)
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


def net_base():
    """port-net's first UDP port: one of a hundred bases 100 apart from
    47000, chosen by the checkout's path, so that two checkouts' gates at
    once do not meet (tools/port_net.py takes 96 from its base)."""
    return 47000 + 100 * (zlib.crc32(REPO.replace('\\', '/').lower().encode()) % 100)


def in_wsl(cmd):
    """The command as this program starts it, in WSL with the venv."""
    script = '[ -f .venv/bin/activate ] && source .venv/bin/activate; exec "$@"'
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
        elif name == 'playtest':
            cmd = [python, 'tools/playtest.py', '-j', str(args.jobs)]
            if args.changed:
                cmd += ['--changed', args.changed]
        else:
            cmd = [python if word is None else word for word in cmd]
        steps.append(Step(name, cmd, where))
    names = {s.name for s in steps}
    for step in steps:
        step.waits = [w for w in step.waits if w in names]
        if step.name in args.skip:
            step.state = 'skipped'
    return steps


def run_step(step):
    cmd = in_wsl(step.cmd) if step.where == 'wsl' else step.cmd
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
