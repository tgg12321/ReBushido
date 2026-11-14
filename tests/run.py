#!/usr/bin/env python3
"""Build and run the tests of what the game's C computes (tests/README.md).

    python3 tests/run.py               every test; a line each; exit 1 if one failed
    python3 tests/run.py NAME...       only those
    python3 tests/run.py -v NAME...    and show what each program printed
    python3 tests/run.py --list        the tests' names

`make test` and `tools/build.sh test` run them all. Run it in WSL or on
Linux: it needs the host's gcc (for x86, with -m32) and python3, and nothing
else: no 32-bit C library, no build of the game.

A test is tests/NAME.c, a program that includes one of the game's own .c
files and calls its functions, and, where the answers come from somewhere
else than the test's own C, tests/NAME.py, which gives the program its
arguments and reads what it printed. The program is built as tools/modern/
check.py builds the game for its warnings (32-bit, the same shim and
rewritten GTE headers), linked with tests/runtime/runtime.c and nothing
else, into build/tests/.
"""
import importlib.util
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'modern'))

import check as modern  # noqa: E402  (tools/modern/check.py)
import support  # noqa: E402

OUT_DIR = 'build/tests'

# As tools/modern/check.py compiles the game, and then: -fwrapv, since the
# game's arithmetic wraps as the PlayStation's does; each function in a
# section of its own and the unused ones dropped, so that a test has to
# stand in only for what the functions it calls reach; no C library.
COMPILE_FLAGS = ['-m32', '-std=gnu89', '-ffreestanding', '-fno-builtin', '-funsigned-char',
                 '-fno-strict-aliasing', '-fwrapv', '-nostdinc', '-O1', '-w',
                 '-fno-pic', '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
                 '-ffunction-sections', '-fdata-sections',
                 '-include', modern.SHIM, '-I' + OUT_DIR + '/include',
                 '-I' + modern.OUT_DIR + '/include', '-Iinclude', '-Itests', '-I.']
LINK_FLAGS = ['-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-Wl,--build-id=none']
RUNTIME = 'tests/runtime/runtime.c'

TIMEOUT_SECONDS = 300


# Sony's libsn.h has the one instruction the game's C spells out itself: the
# break that lets the development host talk. A test program is assembled,
# for another processor, so the tests get the header with that taken out.
POLLHOST_OLD = '#define\tpollhost()\tasm("break 1024")'
POLLHOST_NEW = '#define pollhost()'


def write_headers():
    """The headers tools/modern/check.py rewrites, and one more."""
    modern.write_headers()
    with open('include/psxsdk/libsn.h', encoding='utf-8') as f:
        text = f.read()
    if POLLHOST_OLD not in text:
        sys.exit('run.py: include/psxsdk/libsn.h no longer has `%s`' % POLLHOST_OLD)
    os.makedirs(OUT_DIR + '/include/psxsdk', exist_ok=True)
    with open(OUT_DIR + '/include/psxsdk/libsn.h', 'w', encoding='utf-8', newline='\n') as f:
        f.write(text.replace(POLLHOST_OLD, POLLHOST_NEW))


def test_names():
    return sorted(f[:-2] for f in os.listdir('tests') if f.endswith('.c'))


def title(name):
    """What a test says it is: the first line of its file's comment."""
    with open('tests/%s.c' % name, encoding='utf-8') as f:
        line = f.readline().strip()
    return line[2:].strip().rstrip('*/').strip() if line.startswith('/*') else ''


def python_side(name):
    path = 'tests/%s.py' % name
    if not os.path.exists(path):
        return None
    spec = importlib.util.spec_from_file_location('test_' + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build(name):
    """Compile and link one test. Returns the program's path, or the
    compiler's complaint."""
    program = '%s/%s' % (OUT_DIR, name)
    cmd = (['gcc'] + COMPILE_FLAGS + modern.DEFS + LINK_FLAGS
           + ['tests/%s.c' % name, RUNTIME, '-o', program])
    # the compiler's temporary files beside the program: /tmp is emptied
    # under a WSL that has just started
    env = dict(os.environ, TMPDIR=os.path.abspath(OUT_DIR), LC_ALL='C')
    proc = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env)
    if proc.returncode != 0:
        return None, proc.stderr.strip().split('\n')
    return program, []


def run_one(name, verbose):
    """(state, count, lines): state is ok, skip or FAIL; lines say why, or
    for a test that passed what it could not do here."""
    side = python_side(name)
    args = []
    if side and hasattr(side, 'arguments'):
        try:
            args = [str(a) for a in side.arguments()]
        except support.Skip as skip:
            return 'skip', 0, [str(skip)]
    program, errors = build(name)
    if not program:
        return 'FAIL', 0, ['it does not build:'] + errors[:30]
    try:
        proc = subprocess.run([program] + args, capture_output=True, timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        return 'FAIL', 0, ['still running after %d seconds' % TIMEOUT_SECONDS]
    except OSError as error:
        return 'FAIL', 0, ['the program does not run here (a 32-bit x86 Linux program): %s' % error]
    lines = proc.stdout.decode('utf-8', 'replace').split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    if verbose:
        for line in lines:
            print('    | ' + line)
    last = lines[-1].split() if lines else []
    if len(last) != 4 or last[0] != 'checks' or last[2] != 'failed':
        # it never got to its last line
        return 'FAIL', 0, ['the program stopped early (exit code %d)' % proc.returncode] + lines[-5:]
    count, failed = int(last[1]), int(last[3])
    failures = [line for line in lines if line.startswith('FAIL ')]
    if failed > len(failures):
        failures.append('... and %d more' % (failed - len(failures)))
    data = [line for line in lines[:-1] if not line.startswith('FAIL ')]
    notes = []
    if side and hasattr(side, 'check'):
        checks = support.Checks()
        try:
            side.check(data, checks)
        except support.Skip as skip:
            return 'skip', 0, [str(skip)]
        count += checks.count
        notes = checks.notes
        failures += checks.failures[:20]
        if len(checks.failures) > 20:
            failures.append('... and %d more' % (len(checks.failures) - 20))
    elif data:
        failures.append('the program printed %d lines that nothing reads' % len(data))
    if count == 0 and not failures:
        failures.append('it checked nothing')
    if failures:
        return 'FAIL', count, failures
    return 'ok', count, notes


def main():
    os.chdir(ROOT)
    args = sys.argv[1:]
    verbose = '-v' in args
    names = [a for a in args if not a.startswith('-')]
    known = test_names()
    if '--list' in args:
        print('\n'.join(known))
        return 0
    for name in names:
        if name not in known:
            sys.exit('run.py: no test tests/%s.c' % name)
    os.makedirs(OUT_DIR, exist_ok=True)
    write_headers()

    tally = {'ok': 0, 'skip': 0, 'FAIL': 0}
    for name in names or known:
        state, count, lines = run_one(name, verbose)
        tally[state] += 1
        if state == 'ok':
            print('ok    %-16s %7d checks  %s' % (name, count, title(name)))
            for line in lines:
                print('        (' + line + ')')
        elif state == 'skip':
            print('skip  %-16s %s' % (name, '; '.join(lines)))
        else:
            print('FAIL  %-16s %s' % (name, title(name)))
            for line in lines:
                print('        ' + line)
    summary = '%d passed, %d skipped, %d failed' % (tally['ok'], tally['skip'], tally['FAIL'])
    if tally['FAIL']:
        print('FAILED: ' + summary)
        return 1
    print('OK: tests: ' + summary)
    return 0


if __name__ == '__main__':
    sys.exit(main())
