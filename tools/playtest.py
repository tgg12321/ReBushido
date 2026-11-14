#!/usr/bin/env python3
"""Scripted play tests: the game run in an emulator with no window, driven
and checked by small files (tests/play/README.md).

    python tools/playtest.py                 every test of tests/play/
    python tools/playtest.py boot fight_end  those tests
    python tools/playtest.py --list          what there is
    python tools/playtest.py path/to/x.toml  a test file anywhere

`make playtest` (`wsl bash tools/build.sh playtest`) builds the discs the
tests want and then runs this. It is for the developer's machine only: it
needs PCSX-Redux and your own image of the retail disc, neither of which is
in the repository. Where either is missing it says "skipped: ..." and exits
0; a test whose disc has not been built is skipped the same way. It exits 1
if a test fails.

    REBUSHIDO_EMULATOR   the emulator's program
                         (default tmp/pcsx-redux/pcsx-redux.main)
    REBUSHIDO_RETAIL     the retail disc's .cue
                         (default "Bushido Blade 2 (USA).cue" in the checkout)

The emulator is a Windows program here, so under WSL this starts itself
again with Windows' python (python.exe). It runs one emulator at a time,
on a desktop of its own with no sound (tools/play/launch.py), ends it by a
hard time limit, and checks after each run that no process of it is left.
The whole suite is held to ten minutes.
What a run wrote is in build/playtest/<test>/<disc>/: a picture of every
checkpoint, run.log, plan.txt, the emulator's own output and its two
memory cards.

A disc and its names: a test reads the game's memory by the names of the
source. For the retail disc they are config/symbol_addrs.txt; for a built
disc, the table written beside it when it was built (build/play/NAME.sym):

    python3 tools/playtest.py --symbols build/rebushido/rebushido.elf build/play/ReBushido.sym
"""
import argparse
import ast
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))

TESTS_DIR = os.path.join(REPO, "tests", "play")
STATE_FILE = os.path.join(TESTS_DIR, "state.toml")
OUT_DIR = os.path.join(REPO, "build", "playtest")
PLAY_DIR = os.path.join(REPO, "build", "play")
RUNNER = os.path.join(REPO, "tools", "play", "runner.lua")
EMULATOR = os.environ.get("REBUSHIDO_EMULATOR") or os.path.join(REPO, "tmp", "pcsx-redux", "pcsx-redux.main")
RETAIL_CUE = os.environ.get("REBUSHIDO_RETAIL") or os.path.join(REPO, "Bushido Blade 2 (USA).cue")
RETAIL_SYMBOLS = os.path.join(REPO, "config", "symbol_addrs.txt")

# Where the program is loaded.
PROGRAM_START = 0x80010000
# A run: its hard limit in seconds, and the longest a test may ask for.
RUN_SECONDS = 180
RUN_SECONDS_MAX = 285
# The emulator has said nothing for this long: it hangs.
SILENT_SECONDS = 60
# All the runs together; a test that would start after this is not run.
SUITE_SECONDS = 570
# What the fixed script and a plan may weigh. The script is small and the
# plan is data, so neither comes near what LuaJIT can load; a file past
# these is not what this tool wrote.
RUNNER_BYTES_MAX = 40000
PLAN_LINES_MAX = 4000
# The emulator's output when a script of its Lua fails.
LUA_FAILURE = re.compile(r"lua.*(error|failed)|attempt to |stack traceback|syntax error", re.I)

BUTTONS = ("SELECT", "START", "UP", "RIGHT", "DOWN", "LEFT", "L1", "R1", "L2", "R2",
           "TRIANGLE", "CIRCLE", "CROSS", "SQUARE")
# The memory cards of a test's runs: new and empty in each; or those the
# run before left, for every run after the first; or none in either slot.
CARDS = ("new", "keep", "none")
TYPES = {"u8": (1, 0), "s8": (1, 1), "u16": (2, 0), "s16": (2, 1), "u32": (4, 0), "s32": (4, 1)}
COMPARISONS = ("==", "!=", "<=", ">=", "<", ">")
# The files whose enums and plain numbers a test can name.
HEADERS = ("include/sys.h", "include/fight.h", "include/fighter.h", "include/stage.h", "include/weapon.h",
           "include/menu.h", "src/main/menu_screen.c",
           "include/rb_settings.h", "include/rb_save.h", "include/rb_options.h")


class TestError(Exception):
    """A test file that cannot be run as written."""


class Skip(Exception):
    """Something the test needs is not on this machine."""


# ---- names: enums and plain numbers of the headers ----

def evaluate(text, names):
    """The value of a C-like expression of numbers, names and operators."""
    text = text.replace("&&", " and ").replace("||", " or ")
    try:
        tree = ast.parse(text.strip(), mode="eval")
    except SyntaxError:
        raise TestError("cannot read `%s`" % text)

    def walk(node):
        if isinstance(node, ast.Expression):
            return walk(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.Name):
            if node.id not in names:
                raise TestError("`%s`: no value, enum or number is called `%s`" % (text.strip(), node.id))
            return names[node.id]
        if isinstance(node, ast.BoolOp):
            values = [walk(v) for v in node.values]
            return int(all(values) if isinstance(node.op, ast.And) else any(values))
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.Not, ast.Invert)):
            value = walk(node.operand)
            return -value if isinstance(node.op, ast.USub) else int(not value) if isinstance(node.op, ast.Not) \
                else ~value
        if isinstance(node, ast.BinOp):
            a, b = walk(node.left), walk(node.right)
            ops = {ast.Add: lambda: a + b, ast.Sub: lambda: a - b, ast.Mult: lambda: a * b,
                   ast.BitAnd: lambda: a & b, ast.BitOr: lambda: a | b, ast.BitXor: lambda: a ^ b,
                   ast.LShift: lambda: a << b, ast.RShift: lambda: a >> b}
            if type(node.op) in ops:
                return ops[type(node.op)]()
        if isinstance(node, ast.Compare):
            left = walk(node.left)
            for op, right in zip(node.ops, node.comparators):
                right = walk(right)
                ops = {ast.Eq: left == right, ast.NotEq: left != right, ast.Lt: left < right,
                       ast.LtE: left <= right, ast.Gt: left > right, ast.GtE: left >= right}
                if type(op) not in ops:
                    break
                if not ops[type(op)]:
                    return 0
                left = right
            else:
                return 1
        raise TestError("`%s` has something that is not a number, a name or an operator" % text.strip())

    return walk(tree)


def header_names():
    """Every enum member and every `#define NAME number` of HEADERS."""
    names = {}
    for header in HEADERS:
        path = os.path.join(REPO, header)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            text = re.sub(r"/\*.*?\*/", " ", f.read(), flags=re.S)
        for m in re.finditer(r"^#define\s+(\w+)\s+\(?\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))\s*\)?\s*$", text, re.M):
            names.setdefault(m.group(1), int(m.group(2), 0))
        for body in re.findall(r"\benum\b[^{;]*\{([^}]*)\}", text):
            value = 0
            for entry in body.split(","):
                name, _, expr = entry.strip().partition("=")
                name = name.strip()
                if not re.fullmatch(r"[A-Za-z_]\w*", name):
                    continue
                if expr.strip():
                    try:
                        value = evaluate(expr, names)
                    except TestError:
                        value = None
                if value is None:
                    continue
                names.setdefault(name, value)
                value += 1
    return names


# ---- discs and their names ----

def read_symbols(path):
    symbols = {}
    with open(path, encoding="utf-8") as f:
        for m in re.finditer(r"^(\w+) = (0x[0-9A-Fa-f]+);", f.read(), re.M):
            symbols.setdefault(m.group(1), int(m.group(2), 16))
    return symbols


def write_symbols(elf_path, out_path):
    """A table of an ELF's names for a disc built from it (--symbols)."""
    from elf32 import Elf
    found = {}
    for s in Elf(elf_path).syms:
        # no section names, nothing undefined; a global wins over a file's own
        if s["name"] and s["shndx"] != 0 and s["type"] != 3:
            if s["bind"] in (1, 2) or s["name"] not in found:
                found[s["name"]] = s["value"]
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("// The names of %s, for tools/playtest.py; written when the disc was built.\n" % elf_path)
        for name, value in sorted(found.items(), key=lambda item: (item[1], item[0])):
            f.write("%s = 0x%08X;\n" % (name, value))
    print("playtest.py: %s: %d names" % (out_path, len(found)))
    return 0


class Disc:
    """A disc image a test names: `retail`, or NAME for build/play/NAME.cue
    (`rebushido` is ReBushido, `quickboot` QuickBoot: any case). Where
    build/play/ has NAME.exe and no NAME.cue it is a program the emulator
    loads with no disc (`gteprobe`, the GTE probe)."""

    def __init__(self, name):
        self.name = name
        self.settings = {}
        self.exe = None
        if name == "retail":
            self.cue, symbols, how = RETAIL_CUE, RETAIL_SYMBOLS, "your own image of the disc; REBUSHIDO_RETAIL names it"
        else:
            stem = name
            if os.path.isdir(PLAY_DIR):
                for entry in sorted(os.listdir(PLAY_DIR)):
                    if entry.lower() == name.lower() + ".cue":
                        stem = entry[:-4]
                    elif entry.lower() == name.lower() + ".exe" and stem == name:
                        stem, self.exe = entry[:-4], os.path.join(PLAY_DIR, entry)
            self.cue = self.exe or os.path.join(PLAY_DIR, stem + ".cue")
            symbols = os.path.join(PLAY_DIR, stem + ".sym")
            how = "not built: make playtest builds the tests' discs, make quickboot-disc one of your own"
        for path in (self.cue, symbols):
            if not os.path.exists(path):
                raise Skip("no %s (%s)" % (os.path.relpath(path, REPO).replace("\\", "/"), how))
        self.symbols = read_symbols(symbols)
        # what a quick-boot disc was built to start: NAME=VALUE a line
        settings = self.cue[:-4] + ".txt"
        if os.path.exists(settings):
            with open(settings, encoding="utf-8") as f:
                for m in re.finditer(r"^(\w+)=(\S+)$", f.read(), re.M):
                    self.settings[m.group(1)] = m.group(2)

    def address(self, text):
        """`symbol`, `symbol+12`, `symbol+0x44C+4` or a bare address."""
        total = 0
        for sign, word in re.findall(r"([+-]?)\s*([\w.]+)", text):
            if re.fullmatch(r"0[xX][0-9A-Fa-f]+|\d+", word):
                value = int(word, 0)
            elif word in self.symbols:
                value = self.symbols[word]
            else:
                raise TestError("the %s disc has no symbol `%s`" % (self.name, word))
            total += -value if sign == "-" else value
        return total


# ---- a test file ----

def load_toml(path):
    try:
        import tomllib
    except ImportError:
        raise Skip("this python has no tomllib (3.11 or later is needed)")
    with open(path, "rb") as f:
        try:
            return tomllib.load(f)
        except tomllib.TOMLDecodeError as e:
            raise TestError("%s: %s" % (os.path.relpath(path, REPO), e))


class Checkpoint:
    def __init__(self, table, index):
        self.name = table.get("name") or "checkpoint%d" % (index + 1)
        if not re.fullmatch(r"[A-Za-z0-9_-]+", self.name) or self.name == "failure":
            raise TestError("a checkpoint cannot be called `%s`" % self.name)
        hows = [k for k in ("at", "after", "when") if k in table]
        if len(hows) != 1:
            raise TestError("checkpoint %s: give one of `at`, `after`, `when`" % self.name)
        self.how = hows[0]
        self.value = table[self.how]
        self.within = int(table.get("within", 0))
        self.shot = bool(table.get("shot", True))
        self.expect = list(table.get("expect", []))
        # the runs it is part of, counted from 1 in the order of `discs`; None: all
        self.runs = [int(n) for n in table["runs"]] if "runs" in table else None
        unknown = set(table) - {"name", "at", "after", "when", "within", "shot", "expect", "runs"}
        if unknown:
            raise TestError("checkpoint %s: not understood: %s" % (self.name, ", ".join(sorted(unknown))))


class Test:
    def __init__(self, path):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        data = load_toml(path)
        unknown = set(data) - {"about", "discs", "limit", "stall", "seconds", "input", "compare", "state",
                               "checkpoint", "cards", "frames_from", "dump", "common_state"}
        if unknown:
            raise TestError("not understood: %s" % ", ".join(sorted(unknown)))
        self.about = data.get("about", "")
        self.discs = list(data.get("discs", []))
        if not self.discs:
            raise TestError("no `discs`")
        self.limit = int(data.get("limit", 0))
        if self.limit <= 0:
            raise TestError("no `limit` (the vertical blank the run ends at, at the latest)")
        self.stall = int(data.get("stall", 1800))
        self.seconds = min(int(data.get("seconds", RUN_SECONDS)), RUN_SECONDS_MAX)
        self.input = list(data.get("input", []))
        self.compare = list(data.get("compare", []))
        self.cards = data.get("cards", "new")
        if self.cards not in CARDS:
            raise TestError("`cards` is one of %s" % ", ".join(CARDS))
        self.frames_from = data.get("frames_from")
        # memory to write to a file when the run ends: NAME = "symbol:bytes"
        self.dump = dict(data.get("dump", {}))
        # a test of a program that is not the game has none of its values
        self.state = dict(load_toml(STATE_FILE).get("state", {})) if data.get("common_state", True) else {}
        self.state.update(data.get("state", {}))
        self.checkpoints = [Checkpoint(t, i) for i, t in enumerate(data.get("checkpoint", []))]
        if not self.checkpoints:
            raise TestError("no checkpoint")
        names = [cp.name for cp in self.checkpoints]
        if len(set(names)) != len(names):
            raise TestError("two checkpoints of one name")
        for cp in self.checkpoints:
            for n in cp.runs or []:
                if not 1 <= n <= len(self.discs):
                    raise TestError("checkpoint %s: `runs` names run %d of %d" % (cp.name, n, len(self.discs)))
        if self.frames_from is not None and self.frames_from not in names:
            raise TestError("`frames_from`: no checkpoint `%s`" % self.frames_from)

    def checkpoints_of(self, number):
        """The checkpoints of run `number` (from 1), in order."""
        return [cp for cp in self.checkpoints if cp.runs is None or number in cp.runs]

    def state_of(self, disc):
        """The values the disc has: its program defines their symbols."""
        found = {}
        for name in sorted(self.state):
            m = re.fullmatch(r"\s*(.+?)\s*:\s*(\w+)\s*", self.state[name])
            if not m or m.group(2) not in TYPES:
                raise TestError("state `%s = %s`: write `symbol+offset:type`, the type one of %s"
                                % (name, self.state[name], ", ".join(TYPES)))
            symbol = re.match(r"\s*([A-Za-z_]\w*)", m.group(1))
            if symbol and symbol.group(1) not in disc.symbols:
                continue
            found[name] = (disc.address(m.group(1)),) + TYPES[m.group(2)]
        return found

    def plan(self, disc, names, number=1):
        """The lines of plan.txt for run `number` of the test, which is on
        `disc` (tools/play/runner.lua)."""
        state = self.state_of(disc)
        order = sorted(state)
        index = {name: i + 1 for i, name in enumerate(order)}
        checkpoints = self.checkpoints_of(number)
        if not checkpoints:
            raise TestError("run %d has no checkpoint" % number)
        lines = ["limit %d" % self.limit, "stall %d" % self.stall]
        if disc.exe:
            # a program of the tests' own: no frame count to watch, and
            # its code is wherever its file was loaded
            lines.append("text %d %d" % (PROGRAM_START & 0x1FFFFFFF,
                                         (PROGRAM_START & 0x1FFFFFFF) + os.path.getsize(disc.exe)))
        else:
            lines += ["alive %d" % disc.address("g_disp_frame_count"),
                      "movie %d %d" % (disc.address("g_sys_screen"), names["SCREEN_MOVIE"]),
                      # the program from its start to the first word of its data
                      "text %d %d" % (PROGRAM_START & 0x1FFFFFFF, disc.address("g_sys_data_start") & 0x1FFFFFFF)]
        for name in sorted(self.dump):
            m = re.fullmatch(r"\s*(.+?)\s*:\s*(\d+)\s*", self.dump[name])
            if not m or not re.fullmatch(r"[A-Za-z0-9_-]+", name):
                raise TestError("dump `%s = %s`: write `name = \"symbol+offset:bytes\"`" % (name, self.dump[name]))
            lines.append("dump %s %d %d" % (name, disc.address(m.group(1)), int(m.group(2))))
        if self.cards == "none":
            lines.append("nocards")
        for name in order:
            lines.append("var %s %d %d %d" % ((name,) + state[name]))
        cp_index = {cp.name: i + 1 for i, cp in enumerate(checkpoints)}
        every = {cp.name for cp in self.checkpoints}
        for item in self.input:
            m = re.fullmatch(r"\s*(?:(\w[\w-]*)\s*\+\s*)?(\d+)\s+(?:P([12])\s+)?(\w+)(?:\s+hold\s+(\d+))?\s*", item)
            if not m:
                raise TestError("input `%s`: write `FRAME BUTTON`, `CHECKPOINT+FRAMES BUTTON`, with "
                                "`P2` before the button for the second pad and `hold N` after it" % item)
            base, frames, pad, button, hold = m.groups()
            if base is not None and base not in every:
                raise TestError("input `%s`: no checkpoint `%s`" % (item, base))
            if base is not None and base not in cp_index:
                continue  # timed from a checkpoint this run does not have
            if button not in BUTTONS:
                raise TestError("input `%s`: the buttons are %s" % (item, " ".join(BUTTONS)))
            lines.append("in %d %d %s %s %d" % (cp_index.get(base, 0), int(frames), pad or "1", button,
                                               int(hold or 6)))
        for cp in checkpoints:
            head = "cp %s %d %d %s" % (cp.name, cp.shot, cp.within, cp.how)
            if cp.how != "when":
                lines.append("%s %d" % (head, int(cp.value)))
                continue
            tests = []
            for part in re.split(r"&&|\band\b", cp.value):
                m = re.fullmatch(r"\s*(\w+)\s*(==|!=|<=|>=|<|>)\s*(.+?)\s*", part)
                if m and m.group(1) in self.state and m.group(1) not in index:
                    raise TestError("checkpoint %s: the %s disc has no `%s` (%s)"
                                    % (cp.name, disc.name, m.group(1), self.state[m.group(1)]))
                if not m or m.group(1) not in index:
                    raise TestError("checkpoint %s: `when` is `VALUE == NUMBER`, several joined by &&, "
                                    "each VALUE a name of [state]; got `%s`" % (cp.name, part.strip()))
                tests.append("%d %s %d" % (index[m.group(1)], m.group(2), evaluate(m.group(3), names)))
            lines.append(head + " " + " ".join(tests))
        return order, lines


# ---- a run ----

def write_png(path, width, height, rgb):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def convert_shot(folder, name, width, height, bpp):
    """Makes a PNG of a picture the emulator wrote and returns a number that
    is the same for the same pixels (None if there is no picture)."""
    raw = os.path.join(folder, name + ".raw")
    if not os.path.isfile(raw):
        return None
    with open(raw, "rb") as f:
        data = f.read()
    os.remove(raw)
    if bpp == 16:
        rgb = bytearray()
        for (v,) in struct.iter_unpack("<H", data[:width * height * 2]):
            rgb += bytes(((v & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v >> 10 & 31) * 255 // 31))
    else:
        rgb = data[:width * height * 3]
    if len(rgb) != width * height * 3:
        return None
    rgb = bytes(rgb)
    same = zlib.crc32(rgb) ^ (width << 16) ^ height
    if width >= 2 * height:
        # The game's 640 by 240 picture fills a 4:3 screen: each line twice.
        row = width * 3
        rgb = b"".join(rgb[y * row:(y + 1) * row] * 2 for y in range(height))
        height *= 2
    write_png(os.path.join(folder, name + ".png"), width, height, rgb)
    return same


class Run:
    """What one run of a test on one disc gave."""

    def __init__(self):
        self.reached = {}   # checkpoint -> (frame, {state name: value})
        self.pictures = {}  # checkpoint -> a number for its picture's pixels
        self.problems = []
        self.fatal = None   # a problem that ends the whole suite
        self.seconds = 0.0
        self.frames = 0
        self.folder = None


def run_labels(discs):
    """A name for each run: the disc's, with -2, -3 for a disc run again."""
    labels, seen = [], {}
    for disc in discs:
        seen[disc.name] = seen.get(disc.name, 0) + 1
        labels.append(disc.name if seen[disc.name] == 1 else "%s-%d" % (disc.name, seen[disc.name]))
    return labels


def run_on_disc(test, disc, names, launch, number=1, label=None, cards_from=None):
    """Run `number` of the test (from 1), on `disc`, into the folder
    `label`. `cards_from` is the folder of the run whose memory cards this
    one starts with; without it the cards are new."""
    folder = os.path.join(OUT_DIR, test.name, label or disc.name)
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    os.makedirs(folder)
    run = Run()
    run.folder = folder
    for card in ("card1.mcd", "card2.mcd"):
        if cards_from and os.path.isfile(os.path.join(cards_from, card)):
            shutil.copyfile(os.path.join(cards_from, card), os.path.join(folder, card))
    order, lines = test.plan(disc, names, number)
    if len(lines) > PLAN_LINES_MAX:
        raise TestError("the plan has %d lines; %d is the most a run is given" % (len(lines), PLAN_LINES_MAX))
    if os.path.getsize(RUNNER) > RUNNER_BYTES_MAX:
        raise TestError("tools/play/runner.lua is over %d bytes: not started" % RUNNER_BYTES_MAX)
    with open(os.path.join(folder, "plan.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    start = os.path.join(folder, "start.lua")
    with open(start, "w", encoding="utf-8", newline="\n") as f:
        f.write("PLAYTEST_DIR = [==[%s]==]\ndofile([==[%s]==])\n"
                % (folder.replace("\\", "/") + "/", RUNNER.replace("\\", "/")))

    running = launch.emulators_running()
    if running:
        run.fatal = "an emulator is already running (process %s): one at a time" % running[0]
        return run
    # -safe: none of the user's settings; -no-ui: no window, a silent sound
    # driver; the interpreter with the debugger, which the exception check
    # needs; the memory cards files of the run's folder, which the emulator
    # makes new and empty if they are not there.
    cmd = [EMULATOR, "-portable", folder, "-safe", "-noupdate", "-no-ui", "-interpreter", "-debugger",
           "-fastboot"] + (["-loadexe", disc.exe] if disc.exe else ["-iso", disc.cue]) + [
           "-memcard1", os.path.join(folder, "card1.mcd"), "-memcard2", os.path.join(folder, "card2.mcd"),
           "-dofile", start, "-run"]
    log_path = os.path.join(folder, "run.log")
    emulator_log = os.path.join(folder, "emulator.log")
    seen = {"size": -1, "at": time.time()}

    def watch():
        size = os.path.getsize(log_path) if os.path.exists(log_path) else 0
        if size != seen["size"]:
            seen["size"], seen["at"] = size, time.time()
        if time.time() - seen["at"] > SILENT_SECONDS:
            return "silent"
        return None

    result = launch.run(cmd, folder, emulator_log, test.seconds, watch)
    run.seconds = result.seconds

    ended = None
    if os.path.exists(log_path):
        with open(log_path, encoding="utf-8", errors="replace") as f:
            for line in f.read().splitlines():
                word = line.split(" ")
                if word[0] == "cp" and len(word) == 3 + len(order):
                    run.reached[word[1]] = (int(word[2]), dict(zip(order, (int(v) for v in word[3:]))))
                    run.frames = int(word[2])
                elif word[0] == "shot" and len(word) == 5:
                    run.pictures[word[1]] = convert_shot(folder, word[1], int(word[2]), int(word[3]), int(word[4]))
                elif word[0] in ("done", "limit"):
                    ended = word[0]
                elif word[0] == "miss":
                    ended = "miss"
                    run.problems.append("checkpoint `%s` was not reached (gave up at blank %s)" % (word[1], word[2]))
                elif word[0] == "crash":
                    ended = "crash"
                    run.problems.append("the game crashed: " + " ".join(word[1:]))
                elif word[0] == "error":
                    ended = "error"
                    run.fatal = "tools/play/runner.lua failed: " + " ".join(word[1:])
                elif word[0] == "set" and word[-1] != "ok":
                    run.problems.append("the emulator did not take a setting: " + " ".join(word[1:]))
    with open(emulator_log, encoding="utf-8", errors="replace") as f:
        for line in f.read().splitlines():
            if LUA_FAILURE.search(line) and not run.fatal:
                run.fatal = "the emulator reported a Lua error: " + line.strip()[:200]
    if result.windows:
        run.fatal = "the emulator showed a window (%s)" % ", ".join(result.windows)
    if result.left:
        run.fatal = "%d emulator processes were still alive after the run was ended" % result.left
    if launch.emulators_running():
        run.fatal = "an emulator process is still running after the run"
    if result.timed_out:
        run.problems.append("stopped at the time limit of %d seconds (blank %d)" % (test.seconds, run.frames))
    elif result.stopped:
        run.problems.append("the emulator wrote nothing for %d seconds and was stopped" % SILENT_SECONDS)
    elif ended is None:
        run.problems.append("the emulator ended (exit code %s) before the run did; see %s"
                            % (result.code, os.path.relpath(emulator_log, REPO).replace("\\", "/")))
    elif result.code != 0 and not run.fatal:
        run.problems.append("the emulator's exit code was %s" % result.code)
    return run


def check_test(test, discs, runs, names):
    """The test's own claims, on what the runs wrote down."""
    problems = []
    labels = run_labels(discs)
    for number, (disc, run) in enumerate(zip(discs, runs), 1):
        label = labels[number - 1]
        for problem in run.problems:
            problems.append("%s: %s" % (label, problem))
        for cp in test.checkpoints_of(number):
            if cp.name not in run.reached:
                continue
            values = dict(names)
            for key, value in disc.settings.items():  # QB_STAGE and the like
                if value in names:
                    values[key] = names[value]
                elif re.fullmatch(r"-?\d+", value):
                    values[key] = int(value)
            values.update(run.reached[cp.name][1])
            values["frame"] = run.reached[cp.name][0]
            for claim in cp.expect:
                if not evaluate(claim, values):
                    held = ", ".join("%s = %d" % (n, values[n]) for n in sorted(set(re.findall(r"\w+", claim)))
                                     if n in run.reached[cp.name][1] or n == "frame")
                    problems.append("%s: at `%s`, `%s` does not hold (%s)" % (label, cp.name, claim, held))
    if not test.compare:
        return problems
    # the runs against the first, at the checkpoints both have
    first = runs[0]
    order = [cp.name for cp in test.checkpoints]
    for label, run in zip(labels[1:], runs[1:]):
        # the blank a checkpoint was reached at, counted from `frames_from`
        since = (0, 0)
        if test.frames_from is not None:
            if test.frames_from not in first.reached or test.frames_from not in run.reached:
                continue
            since = (first.reached[test.frames_from][0], run.reached[test.frames_from][0])
        for cp in test.checkpoints:
            if cp.name not in first.reached or cp.name not in run.reached:
                continue
            compare = test.compare
            if compare == ["*"]:
                compare = sorted(test.state) + ["frame", "picture"]
            for name in compare:
                if name == "frame":
                    if test.frames_from is not None and order.index(cp.name) <= order.index(test.frames_from):
                        continue
                    a, b = first.reached[cp.name][0] - since[0], run.reached[cp.name][0] - since[1]
                elif name == "picture":
                    a, b = first.pictures.get(cp.name), run.pictures.get(cp.name)
                    if a != b:
                        problems.append("at `%s`, the picture on %s is not the picture on %s"
                                        % (cp.name, labels[0], label))
                    continue
                elif name in test.state:
                    # a value only one of the two programs has is not compared
                    if name not in first.reached[cp.name][1] or name not in run.reached[cp.name][1]:
                        continue
                    a, b = first.reached[cp.name][1][name], run.reached[cp.name][1][name]
                else:
                    raise TestError("compare: `%s` is not a name of [state], `frame`, `picture` or `*`" % name)
                if a != b:
                    problems.append("at `%s`, %s is %d on %s and %d on %s"
                                    % (cp.name, name, a, labels[0], b, label))
    return problems


def find_tests(wanted):
    paths = []
    if not wanted:
        wanted = sorted(f[:-5] for f in os.listdir(TESTS_DIR) if f.endswith(".toml") and f != "state.toml")
    for name in wanted:
        path = name if name.endswith(".toml") else os.path.join(TESTS_DIR, name + ".toml")
        if not os.path.isfile(path):
            sys.exit("playtest.py: no test %s" % path)
        paths.append(os.path.abspath(path))
    return paths


def under_wsl():
    try:
        with open("/proc/version") as f:
            return "microsoft" in f.read().lower()
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser(description="Scripted play tests in an emulator with no window.")
    ap.add_argument("tests", nargs="*", help="names of tests/play/, or .toml files; all if none")
    ap.add_argument("--list", action="store_true", help="list the tests and stop")
    ap.add_argument("--symbols", nargs=2, metavar=("ELF", "OUT"),
                    help="write the table of a built disc's names and stop")
    args = ap.parse_args()
    if args.symbols:
        return write_symbols(*args.symbols)

    paths = find_tests(args.tests)
    if args.list:
        for path in paths:
            try:
                test = Test(path)
                print("%-16s %-22s %s" % (test.name, " ".join(test.discs), test.about))
            except (TestError, Skip) as e:
                print("%-16s %s" % (os.path.splitext(os.path.basename(path))[0], e))
        return 0

    # The emulator here is a Windows program: start again as one.
    if os.name != "nt" and under_wsl() and EMULATOR.lower().endswith((".main", ".exe")):
        windows_python = shutil.which("python.exe")
        if not windows_python:
            print("playtest: skipped: under WSL the emulator needs Windows' python, and python.exe was not found")
            return 0
        return subprocess.call([windows_python, "tools/playtest.py"] + args.tests, cwd=REPO)

    for path, what in ((EMULATOR, "the emulator, PCSX-Redux; REBUSHIDO_EMULATOR names it"),
                       (RETAIL_CUE, "your own image of the retail disc; REBUSHIDO_RETAIL names it")):
        if not os.path.isfile(path):
            print("playtest: skipped: no %s (%s)" % (path, what))
            return 0

    sys.path.insert(0, os.path.join(REPO, "tools", "play"))
    import launch

    names = header_names()
    rows, failed, fatal = [], False, None
    suite_start = time.time()
    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        start = time.time()
        if fatal:
            rows.append((name, "not run", 0.0, []))
            continue
        if time.time() - suite_start > SUITE_SECONDS:
            failed = True
            rows.append((name, "not run", 0.0, ["the suite's %d seconds were up" % SUITE_SECONDS]))
            continue
        try:
            test = Test(path)
            discs = [Disc(d) for d in test.discs]
            runs = []
            labels = run_labels(discs)
            for number, disc in enumerate(discs, 1):
                print("%s on %s ..." % (test.name, labels[number - 1]), flush=True)
                cards_from = runs[-1].folder if runs and test.cards == "keep" else None
                run = run_on_disc(test, disc, names, launch, number, labels[number - 1], cards_from)
                runs.append(run)
                if run.fatal:
                    fatal = run.fatal
                    break
            problems = [fatal] if fatal else check_test(test, discs, runs, names)
            result = "FAILED" if problems else "ok"
        except Skip as e:
            result, problems = "skipped", [str(e)]
        except TestError as e:
            result, problems = "FAILED", ["%s: %s" % (os.path.relpath(path, REPO).replace("\\", "/"), e)]
        failed |= result == "FAILED"
        rows.append((name, result, time.time() - start, problems))

    print()
    print("  %-16s %-8s %s" % ("test", "result", "time"))
    for name, result, seconds, problems in rows:
        print("  %-16s %-8s %3.0fs" % (name, result, seconds))
        for problem in problems:
            print("      " + problem)
    left = launch.emulators_running()
    print("  emulator processes left running: %d" % len(left))
    print("  pictures and logs: build/playtest/<test>/<disc>/   (%.0fs in all)" % (time.time() - suite_start))
    if fatal:
        print("FAILED: stopped at the first run the emulator itself went wrong in")
        return 1
    if failed or left:
        print("FAILED: play tests")
        return 1
    counts = [sum(1 for r in rows if r[1] == k) for k in ("ok", "skipped")]
    print("OK: play tests: %d passed, %d skipped" % tuple(counts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
