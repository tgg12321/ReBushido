#!/usr/bin/env python3
"""The native port's rollback self-test (docs/NETPLAY.md, "Save, load and
hash"), in both sizes of the program:

    python3 tools/port_rollback.py [--bits 32|64] [--test NAME] [--frames N]
                                   [--every M] [--out FOLDER] [--plain-only]
                                   [--program PATH] [--with=ARG ...]

`make port-rollback` runs it. For each size it writes the plan of a play
test's run on the port (tests/play/NAME.toml, `port_game` if nothing
says) for that program's addresses, as tools/playtest.py writes it, and
runs the program twice with that plan, `--no-cards` and `--trace`,
headless:

  - as it is, which is the run the play test's would be;
  - with `--rollback-test N` (`--rollback-every M`): every so often it
    saves the state, runs N frames, puts the state back, runs the same N
    again and stops on any difference of the state or of what the run
    printed (src/port/state.c).

It fails if either run fails, if the second finds a difference, or if
the two printed anything different: going back and running again must
change nothing a run shows. What each run printed is in FOLDER/64-bit/
and 32-bit/ (build/rollback/ if nothing says); the second's report of its rollbacks
and of how long a save and a load took is the last lines of
rollback.err.

It needs the retail disc's image (REBUSHIDO_RETAIL, or the .cue at the
checkout's root, as tools/playtest.py) and the programs `make port`
builds; no emulator. Without the image it says so and passes, as the
play tests do."""
import argparse
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

import playtest  # noqa: E402  (tools/playtest.py)

PROGRAMS = {64: 'build/port/rebushido', 32: 'build/port32/rebushido'}
# How many frames are run twice each time, and how often that starts.
DEFAULT_FRAMES = 8
DEFAULT_EVERY = 24
SECONDS = 900


def write_plan(program, test_name, folder):
    """plan.txt of the test's run on `program`, into `folder`: its path."""
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(os.path.join(REPO, 'tests', 'play', test_name + '.toml'))
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), test.discs.index('port') + 1)
    path = os.path.join(folder, 'plan.txt')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return path


def run(program, plan, folder, name, more):
    """The program with the plan: (exit code, seconds). Its output goes to
    FOLDER/NAME.out and .err."""
    cmd = [program, '--image', playtest.RETAIL_CUE, '--plan', plan,
           '--no-cards', '--trace', '--out', os.path.join(folder, name)] + more
    os.makedirs(os.path.join(folder, name), exist_ok=True)
    start = time.time()
    with open(os.path.join(folder, name + '.out'), 'wb') as out, \
            open(os.path.join(folder, name + '.err'), 'wb') as err:
        try:
            code = subprocess.run(cmd, cwd=REPO, stdout=out, stderr=err, stdin=subprocess.DEVNULL,
                                  timeout=SECONDS).returncode
        except subprocess.TimeoutExpired:
            code = None
    return code, time.time() - start


def read(folder, name):
    with open(os.path.join(folder, name), encoding='utf-8', errors='replace') as f:
        return f.read()


def variables_of(program):
    """[(address, bytes, name)] of the program's variables, from the names
    the build wrote beside it (`sym ADDRESS o BYTES NAME`)."""
    found = []
    with open(program + '.sym', encoding='utf-8') as f:
        for line in f:
            word = line.split()
            if len(word) == 5 and word[0] == 'sym' and word[2] == 'o':
                found.append((int(word[1], 16), int(word[3]), word[4]))
    return sorted(found)


def name_page(variables, address, page_bytes=0x1000):
    """The variables a page of the state from `address` holds, as text."""
    names = [name for start, size, name in variables
             if start < address + page_bytes and start + max(size, 1) > address]
    return ', '.join(names[:6]) + (' and %d more' % (len(names) - 6) if len(names) > 6 else '')


def first_difference(first, second):
    """The first line where two outputs differ, as a line of text."""
    a, b = first.split('\n'), second.split('\n')
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return 'line %d: "%s" against "%s"' % (i + 1, x, y)
    return 'line %d: one ends there (%d lines against %d)' % (min(len(a), len(b)) + 1, len(a), len(b))


def check_program(label, program, args):
    """Both runs of one program; a list of what went wrong."""
    folder = os.path.join(args.out, label)
    os.makedirs(folder, exist_ok=True)
    plan = write_plan(program, args.test, folder)
    problems = []
    code, seconds = run(program, plan, folder, 'plain', args.more)
    print('port_rollback: %s: the plain run: exit %s, %.0f s' % (label, code, seconds), flush=True)
    if code != 0:
        problems.append('%s: the plain run ended with %s (%s/plain.err)' % (label, code, folder))
    if args.plain_only:
        return problems
    code, seconds = run(program, plan, folder, 'rollback',
                        args.more + ['--rollback-test', str(args.frames), '--rollback-every', str(args.every)])
    print('port_rollback: %s: the run with rollbacks: exit %s, %.0f s' % (label, code, seconds), flush=True)
    report = [line for line in read(folder, 'rollback.err').split('\n') if line.startswith('rollback')]
    variables = variables_of(program)
    for line in report:
        # a page or a word among the program's variables: which ones
        at = re.search(r' at (0x[0-9A-F]+)', line) if line.startswith('rollback: differs:') else None
        if at:
            size = 4 if 'the word at' in line else 0x1000
            print('    %s  (%s)' % (line, name_page(variables, int(at.group(1), 16), size) or 'no variable'))
        else:
            print('    ' + line)
    if code != 0:
        problems.append('%s: the run with rollbacks ended with %s (%s/rollback.err)' % (label, code, folder))
    if not any(line.startswith('rollback: ok') for line in report):
        problems.append('%s: the run with rollbacks did not report that it passed' % label)
    plain, rolled = read(folder, 'plain.out'), read(folder, 'rollback.out')
    if plain != rolled:
        problems.append('%s: the run with rollbacks printed otherwise than the plain run, at %s'
                        % (label, first_difference(plain, rolled)))
    # what the plan's checkpoints wrote (snapshots of memory, pictures)
    written = sorted(set(os.listdir(os.path.join(folder, 'plain'))) | set(os.listdir(os.path.join(folder, 'rollback'))))
    for name in written:
        paths = [os.path.join(folder, run_name, name) for run_name in ('plain', 'rollback')]
        contents = [open(path, 'rb').read() if os.path.isfile(path) else None for path in paths]
        if contents[0] != contents[1]:
            problems.append('%s: the run with rollbacks wrote %s otherwise than the plain run' % (label, name))
    if not problems:
        print('port_rollback: %s: %d lines printed and %d files written, the same in both runs'
              % (label, plain.count('\n'), len(written)), flush=True)
    return problems


def main():
    ap = argparse.ArgumentParser(description="The native port's rollback self-test, in both sizes.")
    ap.add_argument('--bits', type=int, choices=(32, 64), help='one size only')
    ap.add_argument('--test', default='port_game', help="the play test whose plan is run (tests/play/)")
    ap.add_argument('--frames', type=int, default=DEFAULT_FRAMES, help='frames run twice each time')
    ap.add_argument('--every', type=int, default=DEFAULT_EVERY, help='frames from one save to the next')
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'rollback'), help='where the runs go')
    ap.add_argument('--plain-only', action='store_true', help='only the run without rollbacks')
    ap.add_argument('--program', help='another build of the port that takes the same options, alone '
                    '(build/port-sdl/linux/rebushido), with its names beside it')
    ap.add_argument('--with', dest='more', action='append', default=[], metavar='ARG',
                    help="one more argument to the program's two runs (--with=--fast)")
    args = ap.parse_args()
    if not os.path.isfile(playtest.RETAIL_CUE):
        print('port_rollback: skipped: no %s (your own image of the retail disc; REBUSHIDO_RETAIL names it)'
              % playtest.RETAIL_CUE)
        return 0
    if args.program:
        program = os.path.abspath(args.program)
        programs = [(os.path.basename(os.path.dirname(program)), program)]
    else:
        programs = [('%d-bit' % bits, os.path.join(REPO, PROGRAMS[bits]))
                    for bits in ([args.bits] if args.bits else [64, 32])]
    problems = []
    for label, program in programs:
        if not os.path.isfile(program):
            problems.append('%s: no %s (make port)' % (label, os.path.relpath(program, REPO)))
            continue
        problems += check_program(label, program, args)
    for problem in problems:
        print('FAILED: port_rollback: ' + problem)
    if not problems:
        print('OK: port_rollback')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
