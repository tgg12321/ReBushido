#!/usr/bin/env python3
"""Where two runs of a play test differ: the values and the memory they
wrote at their checkpoints, laid side by side (tests/play/README.md,
"Memory at a checkpoint").

    python tools/port_compare.py TEST                 every checkpoint, in order
    python tools/port_compare.py TEST CHECKPOINT...   those checkpoints
    python tools/port_compare.py TEST --first         stop after the first that differs
    python tools/port_compare.py TEST --new           only what did not differ at the
                                                      checkpoint before
    python tools/port_compare.py TEST --only NAME     only that piece of [snapshot]
    python tools/port_compare.py TEST --lines N       at most N lines a checkpoint (40)
    python tools/port_compare.py TEST --runs A B      those two runs' folders
                                                      (the first two if not given)
    python tools/port_compare.py TEST --holes         also what the test leaves out
                                                      of a piece (`except`)

It is for finding where the native port (docs/PORT.md) leaves the emulator,
and works for any two runs: it reads what `python tools/playtest.py TEST`
left in build/playtest/TEST/, and runs nothing. TEST is a name of
tests/play/ or a .toml file.

For each checkpoint both runs reached it prints the values of the test's
state that differ, then each piece of [snapshot] memory that does, a line
for each run of bytes: the variable and the offset into it, and the bytes
on each run. A word that is the address of the same place of the same name
in both programs is not a difference (tools/play/snapshot.py). Exits 1 if
anything differed.
"""
import argparse
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))
sys.path.insert(0, os.path.join(REPO, "tools", "play"))

import playtest  # noqa: E402
import snapshot  # noqa: E402


def read_run(folder, order):
    """What a run's log says of its checkpoints: {name: {value: number}},
    and the names in the order they were reached."""
    reached, names = {}, []
    with open(os.path.join(folder, "run.log"), encoding="utf-8", errors="replace") as f:
        for line in f.read().splitlines():
            word = line.split(" ")
            if word[0] == "cp" and len(word) == 3 + len(order) and word[1] != "(failure)":
                reached[word[1]] = dict(zip(order, (int(v) for v in word[3:])))
                names.append(word[1])
    return reached, names


def main():
    ap = argparse.ArgumentParser(description="Where two runs of a play test differ.")
    ap.add_argument("test")
    ap.add_argument("checkpoints", nargs="*")
    ap.add_argument("--first", action="store_true")
    ap.add_argument("--new", action="store_true")
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--lines", type=int, default=40)
    ap.add_argument("--runs", nargs=2, metavar=("A", "B"))
    ap.add_argument("--holes", action="store_true")
    args = ap.parse_args()

    try:
        test = playtest.Test(playtest.find_tests([args.test])[0])
        discs = [playtest.Disc(d) for d in test.discs]
        test.set_discs(discs)
        labels = playtest.run_labels(discs)
        wanted = args.runs or labels[:2]
        if len(labels) < 2 or any(label not in labels for label in wanted):
            sys.exit("port_compare.py: the runs of %s are %s" % (test.name, ", ".join(labels)))
        pair = [discs[labels.index(label)] for label in wanted]
        folders = [os.path.join(playtest.OUT_DIR, test.name, label) for label in wanted]
        for folder in folders:
            if not os.path.isfile(os.path.join(folder, "run.log")):
                sys.exit("port_compare.py: no %s: run `python tools/playtest.py %s` first"
                         % (os.path.relpath(os.path.join(folder, "run.log"), REPO), args.test))
        runs = [read_run(folder, sorted(test.state_of(disc))) for folder, disc in zip(folders, pair)]
        pieces = [test.snapshot_of(disc) for disc in pair]
    except (playtest.TestError, playtest.Skip) as e:
        sys.exit("port_compare.py: %s" % e)
    names = [snapshot.Names(disc.symbols) for disc in pair]

    differed = False
    before = set()  # what differed at the checkpoint before
    for cp in test.checkpoints:
        if args.checkpoints and cp.name not in args.checkpoints:
            continue
        if cp.name not in runs[0][0] or cp.name not in runs[1][0]:
            continue
        lines, now = [], set()
        a, b = runs[0][0][cp.name], runs[1][0][cp.name]
        for value in sorted(set(a) & set(b)):
            if a[value] != b[value] and value != "frames":
                now.add(("value", value))
                lines.append((("value", value), "  %-18s %d on %s, %d on %s"
                              % (value, a[value], wanted[0], b[value], wanted[1])))
        for piece in test.pieces_at(cp):
            if args.only and piece not in args.only:
                continue
            memory = []
            for folder in folders:
                path = os.path.join(folder, "%s.%s.snap" % (cp.name, piece))
                if os.path.isfile(path):
                    with open(path, "rb") as f:
                        memory.append(f.read())
            if len(memory) != 2:
                continue
            bases = [pieces[0][piece][0], pieces[1][piece][0]]
            if test.snapshot[piece] == "variables":
                found = snapshot.variable_differences(memory[0], memory[1], bases[0], bases[1],
                                                      names[0], names[1], test.variables)
                found = [(variable, offset, length, names[0].symbols[variable] - bases[0] + offset,
                          names[1].symbols[variable] - bases[1] + offset) for variable, offset, length in found]
            else:
                if not args.holes:
                    memory[1] = snapshot.without(memory[0], memory[1], test.snapshot_holes(piece))
                found = snapshot.differences(memory[0], memory[1], bases[0], bases[1], names[0], names[1])
                found = [(piece, offset, length, offset, offset) for offset, length in found]
            for variable, offset, length, at_a, at_b in found:
                key = (piece, variable, offset)
                now.add(key)
                lines.append((key, "  %-18s %s+0x%X (%d): %s | %s"
                              % (piece, variable, offset, length, snapshot.show(memory[0], at_a, length),
                                 snapshot.show(memory[1], at_b, length))))
        if args.new:
            lines = [(key, line) for key, line in lines if key not in before]
        before = now
        if not lines:
            continue
        differed = True
        print("%s:" % cp.name)
        for _, line in lines[:args.lines]:
            print(line)
        if len(lines) > args.lines:
            print("  ... and %d more" % (len(lines) - args.lines))
        if args.first:
            break
    if not differed:
        print("port_compare: %s and %s agree at every checkpoint both reached" % tuple(wanted))
    return 1 if differed else 0


if __name__ == "__main__":
    sys.exit(main())
