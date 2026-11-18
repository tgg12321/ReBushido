"""Which play tests a change can reach, for tools/playtest.py --changed REV
(tests/play/README.md, "Only what a change reaches").

    python tools/play/affected.py REV       the tests, and why

A change is what `git diff REV` and the files git does not know yet
name. Each path is matched against RULES, the first rule that matches
saying which tests it reaches; a path no rule matches reaches every test.
The rules lean to too many: a file the PlayStation's builds read reaches
all of them, as does anything this tool cannot tell (a revision git does
not know, a deleted file of the tests).
"""
import fnmatch
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TESTS_DIR = os.path.join(REPO, "tests", "play")

ALL = "all"
NONE = ()
# The tests that run the native port beside the emulator, and the GTE
# probe, whose expected numbers the port's C computes (tests/port/).
PORT = ("port_bugfixes", "port_card", "port_game", "port_movie", "port_other_fight", "port_picture",
        "port_sound")
PROBE = ("gte_model",)
SOUND = ("port_sound", "port_movie")

# (pattern, tests): fnmatch patterns on the path from the checkout's root,
# `*` reaching across folders; the first that matches.
RULES = [
    # the tests themselves: a test's own file is that test; the values
    # every test reads, and the tool that runs them, are all of them
    ("tests/play/README.md", NONE),
    ("tests/play/probe/*", PROBE),
    ("tests/play/state.toml", ALL),
    ("tests/play/*.toml", "own"),
    ("tools/playtest.py", ALL),
    ("tools/play/affected.py", NONE),
    ("tools/play/*", ALL),
    # the window's program alone: the play tests run the port without a
    # window (tools/port_build.py compiles src/port/*.c and no folder of it)
    ("src/port/gl/*", NONE),
    ("src/port/window/*", NONE),
    ("src/port/fonts/*", NONE),
    ("tools/port_sdl_build.py", NONE),
    ("tools/port_gl_prove.py", NONE),
    ("tools/port_fps_prove.py", NONE),
    ("tools/port_stage_prove.py", NONE),
    # the port's sound chip and what feeds it
    ("src/port/spu.c", SOUND),
    ("src/port/sound.c", SOUND),
    ("src/port/xa.c", SOUND),
    ("src/port/wav.c", SOUND),
    # the rest of the port, and how it is built; the GTE's C is also the
    # probe's expected numbers
    ("src/port/*", PORT + PROBE),
    ("tools/port_build.py", PORT),
    ("tools/port_gte_macros.py", PORT + PROBE),
    ("tests/port/*", PROBE),
    ("include/port_*", PORT + PROBE),
    # what no play test runs: the documents, the other tools and tests,
    # the patcher, CI
    ("docs/*", NONE),
    ("*.md", NONE),
    (".github/*", NONE),
    ("tools/patcher/*", NONE),
    ("tools/port_net.py", NONE),
    ("tools/port_rollback.py", NONE),
    ("tools/port_compare.py", NONE),
    ("tools/port_wav.py", NONE),
    ("tools/gate.py", NONE),
    ("tools/checks.sh", NONE),
    ("tools/lint.py", NONE),
    ("tools/lint_baseline.tsv", NONE),
    ("tools/count.py", NONE),
    ("tools/format.py", NONE),
    ("tools/owner_copy.py", NONE),
    ("tools/rebushido_patch.py", NONE),
    ("tools/modern/*", NONE),
    ("tests/rebushido/*", NONE),
    ("tests/*.c", NONE),
    ("tests/*.h", NONE),
    # everything else (src/main/, src/rebushido/, include/, asm/, bb2.ld,
    # the Makefile, config/, movovl/, the build's tools): every test
]


def tests_of_path(path, names):
    """The tests `path` reaches: a set of names."""
    for pattern, tests in RULES:
        if fnmatch.fnmatch(path, pattern):
            if tests == ALL:
                return set(names)
            if tests == "own":
                name = os.path.splitext(os.path.basename(path))[0]
                return {name} if name in names else set(names)
            return set(tests) & set(names)
    return set(names)


def changed_paths(rev):
    """The paths changed since `rev`, in the working tree and the index
    too; None if git cannot say."""
    paths = []
    for cmd in (["git", "diff", "--name-only", rev, "--"],
                ["git", "ls-files", "--others", "--exclude-standard"]):
        proc = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
        if proc.returncode != 0:
            return None
        for line in proc.stdout.splitlines():
            path = line.strip()
            # a new file, and not a link to another checkout's folder (a
            # worktree's .venv), which git cannot tell from a file
            full = os.path.join(REPO, path)
            if path and (cmd[1] == "diff" or (os.path.isfile(full) and not os.path.islink(full))):
                paths.append(path)
    return sorted(set(paths))


def tests_for(rev):
    """(the tests' names, sorted, or None for every one; why)."""
    names = sorted(f[:-5] for f in os.listdir(TESTS_DIR) if f.endswith(".toml") and f != "state.toml")
    paths = changed_paths(rev)
    if paths is None:
        return None, "git cannot compare with %s: every test" % rev
    picked, reasons = set(), []
    for path in paths:
        reached = tests_of_path(path, names)
        if reached == set(names):
            return None, "%s reaches every test" % path
        picked |= reached
        if reached:
            reasons.append(path)
    if not paths:
        return [], "nothing changed"
    why = "%d files changed; %d reach a test%s" % (len(paths), len(reasons),
                                                   (" (%s)" % ", ".join(reasons[:6]) + (" ..." if len(reasons) > 6
                                                                                       else "")) if reasons else "")
    return sorted(picked), why


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: python tools/play/affected.py REV")
    picked, why = tests_for(sys.argv[1])
    print(why)
    print(" ".join(picked) if picked is not None else "(every test)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
