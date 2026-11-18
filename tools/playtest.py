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
The whole suite is held to fourteen minutes.
What a run wrote is in build/playtest/<test>/<disc>/: a picture of every
checkpoint, run.log, plan.txt, the emulator's own output and its two
memory cards; and where a test asks, the game's memory and all of video
memory at a checkpoint, which two runs are then compared by (tools/play/
snapshot.py, picture.py).

A disc and its names: a test reads the game's memory by the names of the
source. For the retail disc they are config/symbol_addrs.txt; for a built
disc, the table written beside it when it was built (build/play/NAME.sym):

    python3 tools/playtest.py --symbols build/rebushido/rebushido.elf build/play/ReBushido.sym

The disc `port` is no disc and no emulator: it is the native port
(docs/PORT.md), build/port/rebushido, a Linux program that reads the retail
image itself. It is given the same plan and writes the same log, its pictures
and its video memory among them, so a test with `discs = ["rebushido",
"port"]` holds the port to the emulator. From Windows it is run through
`wsl`.
"""
import argparse
import ast
import concurrent.futures
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))
sys.path.insert(0, os.path.join(REPO, "tools", "play"))

TESTS_DIR = os.path.join(REPO, "tests", "play")
STATE_FILE = os.path.join(TESTS_DIR, "state.toml")
OUT_DIR = os.path.join(REPO, "build", "playtest")
PLAY_DIR = os.path.join(REPO, "build", "play")
RUNNER = os.path.join(REPO, "tools", "play", "runner.lua")
EMULATOR = os.environ.get("REBUSHIDO_EMULATOR") or os.path.join(REPO, "tmp", "pcsx-redux", "pcsx-redux.main")
RETAIL_CUE = os.environ.get("REBUSHIDO_RETAIL") or os.path.join(REPO, "Bushido Blade 2 (USA).cue")
RETAIL_SYMBOLS = os.path.join(REPO, "config", "symbol_addrs.txt")
# The native port: its program, and the names tools/port_build.py wrote.
PORT_PROGRAM = os.environ.get("REBUSHIDO_PORT") or os.path.join(REPO, "build", "port", "rebushido")
PORT_SYMBOLS = PORT_PROGRAM + ".sym"
# What the port exits with when the game crashed (include/port_host.h).
PORT_EXIT_CRASH = 70

# Where the program is loaded.
PROGRAM_START = 0x80010000
# A run's result is decided by the vertical blanks it counts, never by
# the clock: a checkpoint is given up `within` blanks, and a run ends at
# its `limit`. The clock only stops a run that hangs. A test's `seconds`
# is about what its run takes on a quiet machine, at most RUN_SECONDS_MAX;
# the run is stopped at LOAD_SLACK times that, so that a machine made
# slower by other work (several runs at once among it) still passes.
RUN_SECONDS = 180
RUN_SECONDS_MAX = 285
LOAD_SLACK = 4
# The emulator has said nothing for this long: it hangs. (A run says a
# line every 300 blanks, about three seconds of a quiet machine.)
SILENT_SECONDS = 120
# All the runs together; a run that would start after this is not run.
SUITE_SECONDS = 2400
# Runs at once where -j does not say: one for every two free processors,
# up to this.
JOBS_DEFAULT_MAX = 4
# The lines a test's failure gives to memory that differs ([snapshot]).
SNAPSHOT_PROBLEMS_MAX = 8
# What the fixed script and a plan may weigh. The script is small and the
# plan is data, so neither comes near what LuaJIT can load; a file past
# these is not what this tool wrote.
RUNNER_BYTES_MAX = 40000
PLAN_LINES_MAX = 6000
# The emulator's output when a script of its Lua fails.
LUA_FAILURE = re.compile(r"lua.*(error|failed)|attempt to |stack traceback|syntax error", re.I)

# How long a press is held where the test does not say: vertical blanks,
# or frames of the game for a press counted in those.
HOLD_BLANKS = 6
HOLD_FRAMES = 3
# The checkpoints a run may have (src/port/plan.c holds as many).
CHECKPOINTS_MAX = 2048

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

def read_port_symbols(path):
    """The names of the native port's program, its variables' sizes (its
    `sym ADDRESS KIND SIZE NAME` lines), and the layouts of the variables
    a 64-bit program does not lay out as the PlayStation does: {name:
    (bytes on the PlayStation, [(offset there, offset here, bytes)])}, its
    `layout NAME BYTES` lines with the `run THERE HERE BYTES` lines after
    each (tools/port_layout.py, console_runs)."""
    symbols, sizes, layouts = {}, {}, {}
    with open(path, encoding="utf-8") as f:
        text = f.read()
    for m in re.finditer(r"^sym ([0-9a-f]+) (\w) (\d+) (\w+)$", text, re.M):
        if m.group(4) not in symbols:
            symbols[m.group(4)] = int(m.group(1), 16)
            if m.group(2) == "o":
                sizes[m.group(4)] = int(m.group(3))
    runs = None
    for line in text.split("\n"):
        word = line.split()
        if len(word) == 3 and word[0] == "layout":
            runs = []
            layouts[word[1]] = (int(word[2]), runs)
        elif len(word) == 4 and word[0] == "run" and runs is not None:
            runs.append((int(word[1]), int(word[2]), int(word[3])))
        elif word and word[0] != "run":
            runs = None
    return symbols, sizes, layouts


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
    loads with no disc (`gteprobe`, the GTE probe). `port` is the native
    port's program, which is run in the emulator's place."""

    def __init__(self, name):
        self.name = name
        self.settings = {}
        self.exe = None
        self.sizes = {}  # the variables' sizes, where the names' table has them
        self.layouts = {}  # the variables laid out otherwise than on the PlayStation
        self.port = name == "port"
        if self.port:
            for path in (PORT_PROGRAM, PORT_SYMBOLS):
                if not os.path.exists(path):
                    raise Skip("no %s (not built: make port)" % os.path.relpath(path, REPO).replace("\\", "/"))
            self.cue = PORT_PROGRAM
            self.symbols, self.sizes, self.layouts = read_port_symbols(PORT_SYMBOLS)
            return
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

    def as_console(self, name, memory):
        """The bytes of this disc's variable `name` as the PlayStation lays
        the variable out: themselves, but in a 64-bit port, where a
        structure with pointers has grown. Each member's bytes go where the
        PlayStation has the member; of a pointer, the low four of its eight,
        which are all of an address."""
        if name not in self.layouts:
            return memory
        size, runs = self.layouts[name]
        out = bytearray(size)
        for there, here, count in runs:
            out[there:there + count] = memory[here:here + count]
        return bytes(out)

    def value_address(self, text, size):
        """The address of a value of `size` bytes written as `address`
        takes it, its offsets counted as the PlayStation lays the variable
        out: in a 64-bit port, where the variable has grown, the value is
        where the layout says."""
        inside = [word for word in re.findall(r"[A-Za-z_]\w*", text) if word in self.layouts]
        if len(inside) != 1:
            return self.address(text)
        start = self.address(inside[0])
        offset = self.address(text) - start
        for there, here, count in self.layouts[inside[0]][1]:
            if there <= offset and offset + size <= there + count:
                return start + here + offset - there
        raise TestError("`%s` is not one value of `%s` as the 64-bit port lays it out" % (text, inside[0]))

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
        hows = [k for k in ("at", "after", "frames", "call", "when") if k in table]
        if len(hows) != 1:
            raise TestError("checkpoint %s: give one of `at`, `after`, `frames`, `call`, `when`" % self.name)
        self.how = hows[0]
        self.value = table[self.how]
        self.within = int(table.get("within", 0))
        self.shot = bool(table.get("shot", True))
        # the [snapshot] memory written at it: all of it, or the pieces named
        self.snapshot = table.get("snapshot", False)
        if not isinstance(self.snapshot, (bool, list)):
            raise TestError("checkpoint %s: `snapshot` is true, or a list of [snapshot]'s names" % self.name)
        # `when`, looked for only as a frame of the game begins
        self.at_frame = bool(table.get("at_frame", False))
        if self.at_frame and self.how != "when":
            raise TestError("checkpoint %s: `at_frame` goes with `when`" % self.name)
        # `call`: the call of the function that is waited for (the first)
        self.count = int(table.get("count", 1))
        # the port lists the calls from here to the next checkpoint
        self.calls = bool(table.get("calls", False))
        # video memory is written at it (README.md, "The picture"), and how
        # many of its pixels may differ from the first run's
        self.vram = bool(table.get("vram", False))
        self.vram_differ = int(table.get("vram_differ", 0))
        if self.vram_differ and not self.vram:
            raise TestError("checkpoint %s: `vram_differ` goes with `vram = true`" % self.name)
        # sound memory is written at it (README.md, "The sound")
        self.spu = bool(table.get("spu", False))
        self.expect = list(table.get("expect", []))
        # the runs it is part of, counted from 1 in the order of `discs`; None: all
        self.runs = [int(n) for n in table["runs"]] if "runs" in table else None
        unknown = set(table) - {"name", "at", "after", "frames", "call", "count", "when", "within", "shot",
                                "expect", "runs", "snapshot", "repeat", "calls", "at_frame", "vram",
                                "vram_differ", "spu"}
        if unknown:
            raise TestError("checkpoint %s: not understood: %s" % (self.name, ", ".join(sorted(unknown))))


class Test:
    def __init__(self, path):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        data = load_toml(path)
        unknown = set(data) - {"about", "discs", "limit", "stall", "seconds", "input", "compare", "state",
                               "checkpoint", "cards", "frames_from", "dump", "common_state", "snapshot",
                               "port_gpu"}
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
        # the port's GPU draws as the console, or as the emulator where the
        # two differ (include/port_gpu.h)
        self.port_gpu = data.get("port_gpu", "console")
        if self.port_gpu not in ("console", "emulator"):
            raise TestError("`port_gpu` is \"console\" or \"emulator\"")
        self.cards = data.get("cards", "new")
        if self.cards not in CARDS:
            raise TestError("`cards` is one of %s" % ", ".join(CARDS))
        self.frames_from = data.get("frames_from")
        # memory to write to a file when the run ends: NAME = "symbol:bytes"
        self.dump = dict(data.get("dump", {}))
        # memory to write at the checkpoints that say `snapshot = true`
        self.snapshot = dict(data.get("snapshot", {}))
        # the discs of the test, once they are known: `variables` is what
        # all of them have (set_discs)
        self.variables = None
        # a test of a program that is not the game has none of its values
        self.state = dict(load_toml(STATE_FILE).get("state", {})) if data.get("common_state", True) else {}
        self.state.update(data.get("state", {}))
        self.checkpoints = []
        for i, table in enumerate(data.get("checkpoint", [])):
            times = int(table.get("repeat", 1))
            if times < 1 or (times > 1 and not ({"after", "frames", "call"} & set(table))):
                raise TestError("checkpoint %s: `repeat` is for a checkpoint reached `after`, `frames` or `call`"
                                % table.get("name", i + 1))
            if times == 1:
                self.checkpoints.append(Checkpoint(table, i))
            # NAME_1 to NAME_N, one after another
            for n in range(1, times + 1 if times > 1 else 0):
                self.checkpoints.append(Checkpoint(dict(table, name="%s_%d" % (table.get("name", "checkpoint"), n)),
                                                   i))
        if len(self.checkpoints) > CHECKPOINTS_MAX:
            raise TestError("%d checkpoints; %d is the most" % (len(self.checkpoints), CHECKPOINTS_MAX))
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

    def set_discs(self, discs):
        """The discs the test runs on: what `variables` of [snapshot] is,
        the variables all of them have."""
        if "variables" not in self.snapshot.values():
            return
        import snapshot
        sizes = [disc.sizes for disc in discs if disc.sizes]
        if not sizes:
            raise TestError("[snapshot] `variables` needs the variables' sizes, which only the port's names have: "
                            "one of the discs has to be `port`")
        # on a PlayStation program, from the first word of its data
        starts = [None if disc.port else disc.address("g_sys_data_start") for disc in discs]
        # a variable's size is the PlayStation's: the port's own, but
        # where a 64-bit port lays the variable out otherwise
        console = dict(sizes[0])
        for disc in discs:
            for name in disc.layouts:
                console[name] = disc.layouts[name][0]
        self.variables = snapshot.common_variables([disc.symbols for disc in discs], console, starts)
        if not self.variables:
            raise TestError("[snapshot] `variables`: the discs have no variable in common")

    def snapshot_of(self, disc):
        """{name: (address, bytes)} of [snapshot] on a disc."""
        found = {}
        for name in sorted(self.snapshot):
            spec = self.snapshot[name]
            if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
                raise TestError("snapshot `%s`: a name of letters, digits, _ and -" % name)
            if spec == "variables":
                if self.variables is None:
                    raise TestError("[snapshot] `variables`: the test's discs are not known")
                import snapshot
                here = {variable: disc.sizes.get(variable, size) if variable in disc.layouts else size
                        for variable, size in self.variables.items()}
                found[name] = snapshot.variables_range(disc.symbols, here)
                continue
            m = re.fullmatch(r"\s*(.+?)\s*:\s*(0[xX][0-9A-Fa-f]+|\d+)\s*", spec.partition(" except ")[0])
            if not m:
                raise TestError("snapshot `%s = %s`: write `name = \"symbol+offset:bytes\"`, with "
                                "` except OFFSET:BYTES ...` after it for what is not compared, or \"variables\""
                                % (name, spec))
            found[name] = (disc.address(m.group(1)), int(m.group(2), 0))
            variable = m.group(1).strip()
            if variable in disc.layouts:
                # the whole variable, as this disc lays it out
                if int(m.group(2), 0) != disc.layouts[variable][0]:
                    raise TestError("snapshot `%s`: `%s` is laid out otherwise in the 64-bit port than on the "
                                    "PlayStation, and is compared whole or not at all: %d bytes"
                                    % (name, variable, disc.layouts[variable][0]))
                found[name] = (disc.address(variable), disc.sizes[variable])
            elif any(re.match(r"%s\b" % re.escape(other), variable) for other in disc.layouts):
                raise TestError("snapshot `%s`: a piece of a variable that is laid out otherwise in the 64-bit "
                                "port than on the PlayStation; name the whole variable" % name)
        return found

    def snapshot_variable(self, name):
        """The variable a [snapshot] piece is all of, if it names one."""
        spec = self.snapshot[name].partition(" except ")[0].partition(":")[0].strip()
        return spec if re.fullmatch(r"\w+", spec) else None

    def pieces_at(self, cp):
        """The names of [snapshot] that are written at a checkpoint."""
        if cp.snapshot is True:
            return sorted(self.snapshot)
        for name in cp.snapshot or []:
            if name not in self.snapshot:
                raise TestError("checkpoint %s: [snapshot] has no `%s`" % (cp.name, name))
        return sorted(cp.snapshot or [])

    def snapshot_holes(self, name):
        """The (offset, bytes) of a [snapshot] piece that are not compared:
        what follows `except` in it."""
        holes = []
        for hole in self.snapshot[name].partition(" except ")[2].split():
            m = re.fullmatch(r"(0[xX][0-9A-Fa-f]+|\d+):(0[xX][0-9A-Fa-f]+|\d+)", hole)
            if not m:
                raise TestError("snapshot `%s`: after `except`, OFFSET:BYTES and no `%s`" % (name, hole))
            holes.append((int(m.group(1), 0), int(m.group(2), 0)))
        return holes

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
            found[name] = (disc.value_address(m.group(1), TYPES[m.group(2)][0]),) + TYPES[m.group(2)]
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
        if disc.port and self.port_gpu == "emulator":
            lines.append("gpu emulator")
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
        snaps = self.snapshot_of(disc)
        for name in sorted(snaps):
            lines.append("snap %s %d %d" % ((name,) + snaps[name]))
        if self.cards == "none":
            lines.append("nocards")
        for name in order:
            lines.append("var %s %d %d %d" % ((name,) + state[name]))
        cp_index = {cp.name: i + 1 for i, cp in enumerate(checkpoints)}
        every = {cp.name for cp in self.checkpoints}
        in_frames = any(cp.how == "frames" or cp.at_frame for cp in checkpoints)
        for item in self.input:
            m = re.fullmatch(r"\s*(?:(\w[\w-]*)\s*\+\s*)?(\d+)(f?)\s+(?:P([12])\s+)?(\w+)"
                             r"(?:\s+hold\s+(\d+)(f?))?\s*", item)
            if not m:
                raise TestError("input `%s`: write `BLANK BUTTON`, `CHECKPOINT+BLANKS BUTTON` or "
                                "`CHECKPOINT+FRAMESf BUTTON`, with `P2` before the button for the second pad "
                                "and `hold N` (or `hold Nf`) after it" % item)
            base, frames, unit, pad, button, hold, hold_unit = m.groups()
            if unit and base is None:
                raise TestError("input `%s`: a press counted in the game's frames is timed from a checkpoint" % item)
            if hold is not None and hold_unit != unit:
                raise TestError("input `%s`: `hold` is in frames (`hold 3f`) where the press is, in blanks where "
                                "it is not" % item)
            if hold is not None and int(hold) < 1:
                raise TestError("input `%s`: held for one at least" % item)
            if base is not None and base not in every:
                raise TestError("input `%s`: no checkpoint `%s`" % (item, base))
            if base is not None and base not in cp_index:
                continue  # timed from a checkpoint this run does not have
            if button not in BUTTONS:
                raise TestError("input `%s`: the buttons are %s" % (item, " ".join(BUTTONS)))
            in_frames |= bool(unit)
            lines.append("in %d %d %s %s %d%s" % (cp_index.get(base, 0), int(frames), pad or "1", button,
                                                 int(hold or (HOLD_FRAMES if unit else HOLD_BLANKS)),
                                                 " f" if unit else ""))
        if in_frames and not disc.port:
            # where the emulator stops to count the game's frames
            if disc.exe:
                raise TestError("the game's frames are counted only on a disc of the game")
            lines.append("frame %d" % disc.address("ResetRCnt"))
        for cp in checkpoints:
            # what is written at it: 1 a picture, 2 the [snapshot] memory
            what = cp.shot + 2 * (bool(cp.snapshot) and bool(snaps)) + 4 * cp.calls + 8 * cp.vram + 16 * cp.spu
            for piece in self.pieces_at(cp):
                if cp.snapshot is not True:
                    what += 256 << sorted(snaps).index(piece)
            head = "cp %s %d %d %s" % (cp.name, what, cp.within, "whenf" if cp.at_frame else cp.how)
            if cp.how == "call":
                if cp.count < 1:
                    raise TestError("checkpoint %s: `count` is 1 or more" % cp.name)
                lines.append("%s %d %d" % (head, disc.address(str(cp.value)), cp.count))
                continue
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


def read_log(run, log_path, order, folder):
    """What a run's log says, into `run`. Returns how the run ended: done,
    limit, miss, crash, error, or None if the log does not say."""
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
    return ended


def host_path(path):
    """A path of the checkout as the port's program is given it: from the
    checkout's folder, which is where it is started."""
    relative = os.path.relpath(path, REPO)
    if relative.startswith(".."):
        raise Skip("the port is given paths inside the checkout, and %s is not" % path)
    return relative.replace("\\", "/")


def run_port(test, folder, log_path, port_log):
    """The native port in the emulator's place: the same plan, the same
    log. Returns the program's exit code, None if it was stopped at the
    time limit."""
    cmd = ["./" + host_path(PORT_PROGRAM), "--image", host_path(RETAIL_CUE),
           "--plan", host_path(os.path.join(folder, "plan.txt")), "--out", host_path(folder)]
    # the memory cards are the run's folder's own files, as the emulator's
    # are: the port makes one, formatted and empty, where there is none
    if test.cards == "none":
        cmd.append("--no-cards")
    else:
        cmd += ["--card1", host_path(os.path.join(folder, "card1.mcd")),
                "--card2", host_path(os.path.join(folder, "card2.mcd"))]
    if os.name == "nt":
        cmd = ["wsl", "-e"] + cmd
    with open(log_path, "wb") as out, open(port_log, "wb") as err:
        try:
            return subprocess.run(cmd, cwd=REPO, stdout=out, stderr=err, stdin=subprocess.DEVNULL,
                                  timeout=test.seconds * LOAD_SLACK).returncode
        except subprocess.TimeoutExpired:
            return None
        except OSError as e:
            raise Skip("the port's program does not run here (%s)" % e)


def run_on_port(test, run, order, folder):
    """A run on the native port: what its log says, and how it ended."""
    log_path = os.path.join(folder, "run.log")
    port_log = os.path.join(folder, "port.log")
    start = time.time()
    code = run_port(test, folder, log_path, port_log)
    run.seconds = time.time() - start
    ended = read_log(run, log_path, order, folder)
    with open(port_log, encoding="utf-8", errors="replace") as f:
        said = [line for line in f.read().splitlines() if line.startswith("rebushido:")]
    if code is None:
        run.problems.append("stopped at the time limit of %d seconds (blank %d)"
                            % (test.seconds * LOAD_SLACK, run.frames))
    elif code == PORT_EXIT_CRASH or (code != 0 and ended != "miss"):
        run.problems.append("the port stopped (exit code %d): %s" % (code, " / ".join(said[:4]) or "it said nothing"))
    elif ended is None:
        run.problems.append("the port ended before the run did; see %s"
                            % os.path.relpath(port_log, REPO).replace("\\", "/"))
    return run


def run_on_disc(test, disc, names, launch, number=1, label=None, cards_from=None, suite=None):
    """Run `number` of the test (from 1), on `disc`, into the folder
    `label`, which is this run's alone while it lasts (FolderLock).
    `cards_from` is the folder of the run whose memory cards this one
    starts with; without it the cards are new. `suite` is told of the
    emulator's process."""
    folder = os.path.join(OUT_DIR, test.name, label or disc.name)
    lock = FolderLock(folder)
    if not lock.acquire():
        run = Run()
        run.folder = folder
        run.problems.append("%s is another run's now (another play test of this checkout): not run"
                            % os.path.relpath(folder, REPO).replace("\\", "/"))
        return run
    try:
        return _run_on_disc(test, disc, names, launch, number, folder, cards_from, suite)
    finally:
        lock.release()


def _run_on_disc(test, disc, names, launch, number, folder, cards_from, suite):
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

    if disc.port:
        return run_on_port(test, run, order, folder)

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

    result = launch.run(cmd, folder, emulator_log, test.seconds * LOAD_SLACK, watch)
    run.seconds = result.seconds
    if suite is not None and result.pid:
        with suite.lock:
            suite.pids.append(result.pid)

    ended = read_log(run, log_path, order, folder)
    with open(emulator_log, encoding="utf-8", errors="replace") as f:
        for line in f.read().splitlines():
            if LUA_FAILURE.search(line) and not run.fatal:
                run.fatal = "the emulator reported a Lua error: " + line.strip()[:200]
    if result.windows:
        run.fatal = "the emulator showed a window (%s)" % ", ".join(result.windows)
    if result.left:
        run.fatal = "%d emulator processes were still alive after the run was ended" % result.left
    if result.pid and launch.alive([result.pid]):
        run.fatal = "the emulator's process %d is still running after the run" % result.pid
    if result.timed_out:
        run.problems.append("stopped at the time limit of %d seconds (blank %d)"
                            % (test.seconds * LOAD_SLACK, run.frames))
    elif result.stopped:
        run.problems.append("the emulator wrote nothing for %d seconds and was stopped" % SILENT_SECONDS)
    elif ended is None:
        run.problems.append("the emulator ended (exit code %s) before the run did; see %s"
                            % (result.code, os.path.relpath(emulator_log, REPO).replace("\\", "/")))
    elif result.code != 0 and not run.fatal:
        run.problems.append("the emulator's exit code was %s" % result.code)
    return run


def compare_dumps(test, first, run, first_label, label):
    """The pieces of memory two runs wrote at their ends ([dump]), byte for
    byte. Nothing of them is kept but the two files in the runs' folders."""
    problems = []
    for name in sorted(test.dump):
        pieces = []
        for folder in (first.folder, run.folder):
            path = os.path.join(folder, name + ".bin")
            if not os.path.isfile(path):
                problems.append("%s was not written" % os.path.relpath(path, REPO).replace("\\", "/"))
                break
            with open(path, "rb") as f:
                pieces.append(f.read())
        if len(pieces) == 2 and pieces[0] != pieces[1]:
            differing = [i for i, (a, b) in enumerate(zip(*pieces)) if a != b]
            problems.append("the memory `%s` on %s is not that on %s: %d of %d bytes differ, the first at +0x%X"
                            % (name, first_label, label, len(differing) + abs(len(pieces[0]) - len(pieces[1])),
                               len(pieces[0]), differing[0] if differing else min(map(len, pieces))))
    return problems


def compare_snapshots(test, discs, first, run, first_label, label):
    """The memory two runs wrote at the checkpoints that asked for it
    ([snapshot]), each piece against the one of its name: byte for byte,
    but that an address of the same place of the same name is the same
    (tools/play/snapshot.py). The first few places that differ are named;
    `python tools/port_compare.py TEST` shows them all."""
    import snapshot
    disc_a, disc_b = discs[0], discs[runs_index(discs, label)]
    names_a = snapshot.Names(disc_a.symbols, disc_a.layouts)
    names_b = snapshot.Names(disc_b.symbols, disc_b.layouts)
    pieces_a, pieces_b = test.snapshot_of(disc_a), test.snapshot_of(disc_b)
    problems = []
    for cp in test.checkpoints:
        if not cp.snapshot or cp.name not in first.reached or cp.name not in run.reached:
            continue
        for name in test.pieces_at(cp):
            memory = []
            for folder in (first.folder, run.folder):
                path = os.path.join(folder, "%s.%s.snap" % (cp.name, name))
                if not os.path.isfile(path):
                    problems.append("%s was not written" % os.path.relpath(path, REPO).replace("\\", "/"))
                    break
                with open(path, "rb") as f:
                    memory.append(f.read())
            if len(memory) != 2:
                continue
            if test.snapshot[name] == "variables":
                found = snapshot.variable_differences(memory[0], memory[1], pieces_a[name][0], pieces_b[name][0],
                                                      names_a, names_b, test.variables,
                                                      (disc_a.as_console, disc_b.as_console),
                                                      (disc_a.sizes, disc_b.sizes))
                where = ["%s+0x%X" % (variable, offset) for variable, offset, _ in found]
                count = sum(length for _, _, length in found)
            else:
                variable = test.snapshot_variable(name)
                if variable:
                    memory = [disc_a.as_console(variable, memory[0]), disc_b.as_console(variable, memory[1])]
                memory[1] = snapshot.without(memory[0], memory[1], test.snapshot_holes(name))
                found = snapshot.differences(memory[0], memory[1], pieces_a[name][0], pieces_b[name][0],
                                             names_a, names_b)
                where = ["+0x%X" % offset for offset, _ in found]
                count = sum(length for _, length in found)
            if found:
                problems.append("at `%s`, the memory `%s` on %s is not that on %s: %d bytes in %d places, at %s%s"
                                % (cp.name, name, first_label, label, count, len(found), ", ".join(where[:4]),
                                   " ..." if len(where) > 4 else ""))
            if len(problems) >= SNAPSHOT_PROBLEMS_MAX:
                problems.append("(and no more of the memory is compared: python tools/port_compare.py %s)"
                                % test.name)
                return problems
    return problems


def compare_vram(test, first, run, first_label, label):
    """Video memory as two runs wrote it at the checkpoints that say `vram`,
    pixel for pixel (tools/play/picture.py). A checkpoint fails if more
    pixels differ than its `vram_differ` allows. What differed at every
    checkpoint, a line each, is left in the second run's folder, vram.txt;
    `python tools/port_compare.py TEST --vram` shows it, with pictures."""
    import picture
    problems, lines = [], []
    for cp in test.checkpoints:
        if not cp.vram or cp.name not in first.reached or cp.name not in run.reached:
            continue
        memory = []
        for folder in (first.folder, run.folder):
            path = os.path.join(folder, cp.name + ".vram")
            try:
                memory.append(picture.read(path))
            except (OSError, ValueError):
                problems.append("%s was not written" % os.path.relpath(path, REPO).replace("\\", "/"))
                break
        if len(memory) != 2:
            continue
        found = picture.Difference(memory[0], memory[1])
        lines.append("%-16s %s" % (cp.name, found.summary()))
        if found.count > cp.vram_differ:
            problems.append("at `%s`, video memory on %s is not that on %s: %s (%d may)"
                            % (cp.name, first_label, label, found.summary(), cp.vram_differ))
    with open(os.path.join(run.folder, "vram.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(line + "\n" for line in lines))
    return problems


def compare_spu(test, first, run, first_label, label):
    """The sound chip as two runs wrote it at the checkpoints that say
    `spu`: sound memory and the registers a program writes
    (tools/play/spu.py). The emulator's is cut out of the whole state its
    script wrote, CHECKPOINT.state, which is then removed: 22 MB each. What
    differed at every checkpoint, a line each, is left in the second run's
    folder, spu.txt."""
    import spu
    problems, lines = [], []
    for cp in test.checkpoints:
        if not cp.spu or cp.name not in first.reached or cp.name not in run.reached:
            continue
        chips = []
        for folder in (first.folder, run.folder):
            path = os.path.join(folder, cp.name + ".spu")
            state = os.path.join(folder, cp.name + ".state")
            try:
                if os.path.exists(state):
                    with open(state, "rb") as f:
                        chip = spu.extract(f.read())
                    with open(path, "wb") as f:
                        f.write(chip)
                    os.remove(state)
                chips.append(spu.read(path))
            except (OSError, ValueError) as e:
                problems.append("%s was not written, or is not the sound chip (%s)"
                                % (os.path.relpath(path, REPO).replace("\\", "/"), e))
                break
        if len(chips) != 2:
            continue
        found = spu.Difference(chips[0], chips[1])
        lines.append("%-16s %s" % (cp.name, found.summary()))
        if found.count:
            problems.append("at `%s`, the sound chip on %s is not that on %s: %s"
                            % (cp.name, first_label, label, found.summary()))
    with open(os.path.join(run.folder, "spu.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(line + "\n" for line in lines))
    return problems


def runs_index(discs, label):
    """Which of `discs` the run called `label` was on."""
    return run_labels(discs).index(label)


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
                if name in ("dump", "snapshot", "vram", "spu"):
                    continue  # the pieces of memory: once, below
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
                    raise TestError("compare: `%s` is not a name of [state], `frame`, `picture`, `dump`, "
                                    "`snapshot`, `vram`, `spu` or `*`" % name)
                if a != b:
                    problems.append("at `%s`, %s is %d on %s and %d on %s"
                                    % (cp.name, name, a, labels[0], b, label))
        if "dump" in test.compare:
            problems += compare_dumps(test, first, run, labels[0], label)
        if "snapshot" in test.compare:
            problems += compare_snapshots(test, discs, first, run, labels[0], label)
        if "vram" in test.compare:
            problems += compare_vram(test, first, run, labels[0], label)
        if "spu" in test.compare:
            problems += compare_spu(test, first, run, labels[0], label)
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


def free_cores():
    """How many of this machine's processors were idle over half a
    second: on Windows by its own count of idle time, elsewhere by the load
    average."""
    count = os.cpu_count() or 1
    if os.name == "nt":
        import ctypes
        import ctypes.wintypes as wt

        def times():
            idle, kernel, user = wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
            ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user))
            return [(t.dwHighDateTime << 32) | t.dwLowDateTime for t in (idle, kernel, user)]

        first = times()
        time.sleep(0.5)
        idle, kernel, user = (b - a for a, b in zip(first, times()))
        # the kernel's time has the idle time in it
        return count * idle / max(1, kernel + user)
    try:
        return max(0.0, count - os.getloadavg()[0])
    except OSError:
        return float(count)


def default_jobs():
    """Runs at once where -j does not say: one for every two processors
    that are free (an emulator keeps one busy and its helpers part of
    another), from 1 to JOBS_DEFAULT_MAX."""
    return max(1, min(JOBS_DEFAULT_MAX, int(free_cores() // 2)))


class FolderLock:
    """A run's folder is one run's at a time: a lock beside it, the
    operating system's own, so that it is let go of when the program that
    held it ends, however it ends."""

    def __init__(self, folder):
        self.path = folder.rstrip("\\/") + ".lock"
        self.file = None

    def acquire(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        f = open(self.path, "a+b")
        try:
            if os.name == "nt":
                import msvcrt
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            f.close()
            return False
        self.file = f
        return True

    def release(self):
        if self.file is None:
            return
        try:
            if os.name == "nt":
                import msvcrt
                self.file.seek(0)
                msvcrt.locking(self.file.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
        self.file.close()
        self.file = None


class Suite:
    """The runs of the tests, up to `jobs` at once. A test's runs are
    independent of each other, so each is a job of its own; but where the
    test keeps the memory cards (`cards = "keep"`) each run starts with
    the cards the one before it left, and its runs are one job, in order."""

    def __init__(self, jobs, launch, names):
        self.jobs = jobs
        self.launch = launch
        self.names = names
        self.lock = threading.Lock()
        self.fatal = None
        self.start = time.time()
        self.pids = []  # every emulator this suite started

    def say(self, text):
        with self.lock:
            print(text, flush=True)

    def run_chain(self, test, discs, numbers):
        """Runs `numbers` of the test, one after another: [(number, Run)]."""
        labels = run_labels(discs)
        done = []
        for number in numbers:
            label = labels[number - 1]
            if self.fatal or time.time() - self.start > SUITE_SECONDS:
                break
            self.say("%s on %s ..." % (test.name, label))
            cards_from = done[-1][1].folder if done and test.cards == "keep" else None
            run = run_on_disc(test, discs[number - 1], self.names, self.launch, number, label, cards_from, self)
            done.append((number, run))
            if run.fatal:
                with self.lock:
                    self.fatal = self.fatal or run.fatal
                break
        return done


def main():
    ap = argparse.ArgumentParser(description="Scripted play tests in an emulator with no window.")
    ap.add_argument("tests", nargs="*", help="names of tests/play/, or .toml files; all if none")
    ap.add_argument("--list", action="store_true", help="list the tests and stop")
    ap.add_argument("--symbols", nargs=2, metavar=("ELF", "OUT"),
                    help="write the table of a built disc's names and stop")
    ap.add_argument("-j", "--jobs", type=int,
                    help="runs at once (default: one for every two free processors, %d at most)" % JOBS_DEFAULT_MAX)
    ap.add_argument("--changed", metavar="REV",
                    help="only the tests that what changed since the commit REV can reach (tools/play/"
                         "affected.py); every one where that cannot be told")
    args = ap.parse_args()
    if args.symbols:
        return write_symbols(*args.symbols)
    if args.jobs is not None and args.jobs < 1:
        sys.exit("playtest.py: -j is 1 or more")

    wanted = list(args.tests)
    if args.changed:
        import affected
        picked, why = affected.tests_for(args.changed)
        print("playtest: --changed %s: %s" % (args.changed, why), flush=True)
        if picked is not None:
            wanted = [t for t in wanted if t in picked] if wanted else picked
            if not wanted:
                print("OK: play tests: the change reaches none of them")
                return 0
    paths = find_tests(wanted)
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
        more = ["-j", str(args.jobs)] if args.jobs else []
        tests = [os.path.relpath(path, REPO).replace("\\", "/") for path in paths]
        return subprocess.call([windows_python, "tools/playtest.py"] + more + tests, cwd=REPO)

    for path, what in ((EMULATOR, "the emulator, PCSX-Redux; REBUSHIDO_EMULATOR names it"),
                       (RETAIL_CUE, "your own image of the retail disc; REBUSHIDO_RETAIL names it")):
        if not os.path.isfile(path):
            print("playtest: skipped: no %s (%s)" % (path, what))
            return 0

    import launch

    jobs = args.jobs or default_jobs()
    others = launch.emulators_running()
    print("playtest: %d runs at once%s" % (jobs, "; %d emulator processes of others are running" % len(others)
                                            if others else ""), flush=True)
    names = header_names()
    suite = Suite(jobs, launch, names)
    # rows[name] = (result, seconds, problems)
    tests, rows = [], {}
    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            test = Test(path)
            discs = [Disc(d) for d in test.discs]
            test.set_discs(discs)
            tests.append((name, test, discs))
        except Skip as e:
            rows[name] = ("skipped", 0.0, [str(e)])
        except TestError as e:
            rows[name] = ("FAILED", 0.0, ["%s: %s" % (os.path.relpath(path, REPO).replace("\\", "/"), e)])

    # The longest runs first, so that the last to end is a short one.
    work = []
    for name, test, discs in tests:
        numbers = list(range(1, len(discs) + 1))
        for chain in [numbers] if test.cards == "keep" else [[n] for n in numbers]:
            work.append((-test.limit * len(chain), name, chain[0], test, discs, chain))
    work.sort(key=lambda w: w[:3])
    results = {name: {} for name, _, _ in tests}
    seconds = {name: 0.0 for name, _, _ in tests}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(suite.run_chain, test, discs, chain): name for _, name, _, test, discs, chain in work}
        for future in concurrent.futures.as_completed(futures):
            name = futures[future]
            try:
                for number, run in future.result():
                    results[name][number] = run
                    seconds[name] += run.seconds
            except (Skip, TestError) as e:
                results[name]["error"] = e

    failed = False
    for name, test, discs in tests:
        got = results[name]
        try:
            if "error" in got:
                raise got.pop("error")
            fatal = next((run.fatal for run in got.values() if run.fatal), None)
            if fatal:
                rows[name] = ("FAILED", seconds[name], [fatal])
            elif len(got) < len(discs):
                rows[name] = ("not run", seconds[name],
                              [] if suite.fatal else ["the suite's %d seconds were up" % SUITE_SECONDS])
                failed |= not suite.fatal
            else:
                problems = check_test(test, discs, [got[n] for n in range(1, len(discs) + 1)], names)
                rows[name] = ("FAILED" if problems else "ok", seconds[name], problems)
        except Skip as e:
            rows[name] = ("skipped", seconds[name], [str(e)])
        except TestError as e:
            rows[name] = ("FAILED", seconds[name],
                          ["%s: %s" % (os.path.relpath(test.path, REPO).replace("\\", "/"), e)])

    print()
    print("  %-16s %-8s %s" % ("test", "result", "time"))
    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        result, took, problems = rows[name]
        failed |= result == "FAILED"
        print("  %-16s %-8s %3.0fs" % (name, result, took))
        for problem in problems:
            print("      " + problem)
    left = launch.alive(suite.pids)
    print("  emulator processes of these runs left running: %d" % len(left))
    print("  pictures and logs: build/playtest/<test>/<disc>/   (%.0fs in all, %d runs at once)"
          % (time.time() - suite.start, jobs))
    if suite.fatal:
        print("FAILED: stopped at the first run the emulator itself went wrong in")
        return 1
    if failed or left:
        print("FAILED: play tests")
        return 1
    counts = [sum(1 for r in rows.values() if r[0] == k) for k in ("ok", "skipped")]
    print("OK: play tests: %d passed, %d skipped" % tuple(counts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
