#!/usr/bin/env python3
"""Build the native port (docs/PORT.md): build/port/rebushido.

    python3 tools/port_build.py            build what changed, link, write the names
    python3 tools/port_build.py --bits N   the program of 64-bit pointers, or of 32-bit
                                           ones (PORT_BITS in the environment says the
                                           same; DEFAULT_BITS below if neither does)
    python3 tools/port_build.py --list     the files it is made of
    python3 tools/port_build.py --host     only say whether this host can build it

`make port` and `wsl bash tools/build.sh port` run it, once for each size:
the default's program is build/port/rebushido and the other's is beside it,
build/port64/rebushido or build/port32/rebushido (docs/PORT.md, "64 bits").
The program is the game's own C with the port's switch on (RB_PORT,
include/rebushido.h):

  - every file of src/main/ and src/rebushido/ (quick boot's aside, which is
    its own build), compiled as the host tests compile them;
  - Sony's library code only where it is plain arithmetic on memory
    (SONY_FOLDERS and SONY_FILES below; the rule is in docs/PORT.md,
    "Sony's libraries in the port");
  - the movie player, which on the console is a second PlayStation
    program: the C of movovl/ that is plain C over memory (MOVOVL_FILES;
    docs/PORT.md, "The movie"), with two names changed on the command
    line so that they do not clash with the game's;
  - src/port/: the GTE, the platform layer (every other function of Sony's
    and of the BIOS the game calls), memory, the disc image's reader.

It is an x86 Linux program linked with no C library, as the host tests are
(tests/README.md): a C library for 32-bit programs is not always installed,
and the port so far needs a dozen calls of the system's. So it needs the
host's gcc and python3, and nothing else; where gcc cannot make such a
program this says so and exits 1. The game keeps addresses in 32-bit
integers: in either size the program is linked at a fixed address below
4 GB, so that an address of its own is such a number.

The game's files are compiled with their warnings off (`make
rebushido-modern` is where those are read) but the three of GAME_ERRORS,
which stop the build: they are what holds the source to ADDR() and PTR()
(include/common.h). The port's own files are compiled with that check's
warnings as errors.

The game's variables are linked in the order the PlayStation program has
them (config/symbol_addrs.txt), each kind among its own (set, not set,
constant): the game takes some variables for neighbours, reading two or
three of them as one array, and they have to stay neighbours. Each is
compiled into a section of its own, and the link is by a script this
writes, build/port/rebushido.ld. After the link it counts the pairs of
neighbours that are as far apart in the port as on the PlayStation, and
writes the ones that are not to build/port/layout.txt.

Beside the program it writes rebushido.sym: every name of the program with
its address, and the values of tests/play/state.toml as the play tests'
plans name them. The program reads it for --trace and --plan, and to say
where it was if it crashes. For a 64-bit program it also says how each
variable that has grown lies against the PlayStation's (tools/
port_layout.py, console_runs), which tools/playtest.py reads to compare
such a variable with the emulator's.
"""
import concurrent.futures
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'modern'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import check as modern  # noqa: E402  (tools/modern/check.py)
import run as host_tests  # noqa: E402  (tests/run.py)
import port_layout  # noqa: E402
from elf32 import Elf  # noqa: E402

# The size of the program's pointers: 64 bits, or 32 (docs/PORT.md, "64
# bits"). `--bits N` or PORT_BITS in the environment chooses; the one that
# is not the default is built beside it, into build/port32 or build/port64.
DEFAULT_BITS = 64
BITS = DEFAULT_BITS
OUT_DIR = 'build/port'
OBJ_DIR = OUT_DIR + '/obj'
PROGRAM = OUT_DIR + '/rebushido'
NAMES = PROGRAM + '.sym'
LD_SCRIPT = PROGRAM + '.ld'
LAYOUT = OUT_DIR + '/layout.txt'
PLAYSTATION_NAMES = 'config/symbol_addrs.txt'
STATE = 'tests/play/state.toml'

# Sony's library code that the port compiles as it is: whole folders, and
# single files of others. Everything else of Sony's is the port's to supply
# (src/port/). libgte's one file left out patches the console's kernel.
SONY = 'src/main/psxsdk/'
SONY_FOLDERS = {'libgte': ('patchgte.c',), 'libsnd': (), 'libspu': ('spu.c',)}
SONY_FILES = ['libgpu/prim.c', 'libgpu/ext.c', 'libetc/vmode.c', 'libc2/memcpy.c', 'libc2/memmove.c', 'libc2/memchr.c',
              'libc2/strcpy.c', 'libc2/strlen.c', 'libc2/rand.c', 'libc2/ctype.c',
              # _card_clear, which is Sony's C over two BIOS calls the port supplies
              'libcard/card.c']
# Files of src/rebushido/ that are another build's.
NOT_THE_PORTS = ('quickboot.c',)

# The movie player (docs/PORT.md, "The movie"). The overlay is a second
# PlayStation program with its own main and its own copies of 43 of Sony's
# library modules; the port compiles the files of it that are plain C over
# memory, as it does the rest of Sony's code, and supplies what talks to the
# console (src/port/mdec.c, stream.c). Its own main is compiled under
# another name, because the game's main is in the same program; nothing else
# of it clashes with a name of the game's.
MOVOVL_FILES = [
    ('movovl/src/main.c', ['-Dmain=port_MovieOverlayMain']),
    ('movovl/src/movie.c', []),
    # libcd's streaming half, less the two modules that talk to the drive
    # (c_003 clears its registers, c_011 takes a sector in by the DMA).
    # StGetNext is compiled under another name: the port's waits on the
    # drive around it (src/port/stream.c).
    ('movovl/sdk/libcd/c_002.c', []),
    ('movovl/sdk/libcd/c_004.c', []),
    ('movovl/sdk/libcd/c_005.c', []),
    ('movovl/sdk/libcd/c_007.c', []),
    ('movovl/sdk/libcd/c_008.c', []),
    ('movovl/sdk/libcd/c_009.c', ['-DStGetNext=port_StGetNextRing']),
    ('movovl/sdk/libcd/c_010.c', []),
    ('movovl/sdk/libcd/cdread2.c', []),
    ('movovl/sdk/libcd/cdrom.c', []),
    # libetc's older pad interface, over the BIOS's reader (src/port/pad.c).
    ('movovl/sdk/libetc/pad.c', []),
]
# What those files include by name.
MOVOVL_INCLUDES = ['-Imovovl/include', '-Imovovl/sdk/libcd']

# As tests/run.py compiles the game, and then: no section of its own for a
# function (nothing is dropped at the link: every name the game's code uses
# has to be there), but one for each variable, which the link puts in the
# PlayStation's order; no variable given more room than its type asks
# (-malign-data=abi: gcc for x86 otherwise starts every large array on 32
# bytes); frame pointers kept so that a crash can say who called.
GAME_FLAGS = [f for f in host_tests.COMPILE_FLAGS if f != '-ffunction-sections']
GAME_FLAGS += ['-malign-data=abi', '-fno-omit-frame-pointer', '-g']
# Sony's modules include their library's own header by its name alone.
GAME_FLAGS += ['-I' + SONY + folder for folder in sorted(SONY_FOLDERS)]
# The game's files have their warnings off but these, which stop the build
# (docs/PORT.md, "64 bits"): a number cast to a pointer of another size (it
# goes through ADDR(), include/common.h), and a number or a pointer of
# another kind where a pointer is wanted (a PTR32 slot used without PTR()).
GAME_ERRORS = ['-Werror=int-to-pointer-cast', '-Werror=int-conversion', '-Werror=incompatible-pointer-types']
# Each function says when it begins and ends (src/port/plan.c), so that a
# run's plan can stop at a call of one and list the calls of a frame.
INSTRUMENT = '-finstrument-functions'
# -Wstrict-prototypes is left out: Sony's headers, which the port's files
# include for the names they define, declare functions with no parameters
# written (`s32 (*func)()`). -Wredundant-decls too, for the one of those the
# port has to define: include/port_card.h declares _new_card with the
# parameter Sony's libapi.h leaves out, so that it can be defined at all
# (docs/PORT.md, "The memory card").
PORT_WARNINGS = ([f for f in modern.WARN_FLAGS
                  if f not in ('-Wstrict-prototypes', '-Wredundant-decls')] + ['-Werror'])
LINK_FLAGS = ['-m32', '-nostdlib', '-static', '-no-pie', '-Wl,--build-id=none']
# The game takes some variables for neighbours, so each has to start where
# the PlayStation's compiler would start it after the one before: an array
# or a structure on a word's boundary, anything else on its own size's, and
# nothing on more than a word's. A PC's compiler starts an array on its
# elements' boundary (two bytes for halfwords), and a 64-bit one starts
# every array of 16 bytes or more on a 16-byte boundary, whatever
# -malign-data says, and a pointer on an 8-byte one. So the boundary is
# set in the object the compiler made (cap_alignment). A 64-bit compiler
# would also copy and clear with the processor's vector registers, by
# instructions that want the 16-byte boundary it gave: those are not to be
# used. Nothing in the program is a floating-point number.
NO_VECTORS = ['-mno-sse', '-mno-mmx']
WORD = 4
# The kinds of variable, by the section the compiler puts each in.
DATA_KINDS = ('.rodata', '.data', '.bss')

# The program is linked 4 MB up from where Linux usually has one, and says
# where its variables begin and end: an ordering table's link is the low 24
# bits of an address, and those of the program's variables must not be
# those of the PlayStation's RAM, which is the 2 MB from 0x80000000
# (src/port/gpu.c, port_AddLinkRegion).
LD_SCRIPT_TEXT = '''/* Written by tools/port_build.py: the native port's link. The game's
 * variables are in the order of the PlayStation program, kind by kind;
 * then the rest of the game's state, then what is not (config/port_state.txt). */
ENTRY(_start)
SECTIONS
{
    . = 0x08400000 + SIZEOF_HEADERS;
    .text : { *(.text .text.*) }
    . = ALIGN(0x1000);
    g_port_data_start = .;
    .rodata : {
%(rodata)s
        *(.rodata .rodata.*)
    }
    . = ALIGN(0x1000);
    .data : {
        g_port_game_data = .;
%(data)s
        g_port_game_data_end = .;
        EXCLUDE_FILE(%(outside)s) *(.data .data.*)
        g_port_state_data_end = .;
        *(.data .data.*)
    }
    .bss : {
        g_port_game_bss = .;
%(bss)s
        g_port_game_bss_end = .;
        EXCLUDE_FILE(%(outside)s) *(.bss .bss.*) *(COMMON)
        g_port_state_bss_end = .;
        *(.bss .bss.*) *(COMMON)
    }
    g_port_data_end = .;
    /DISCARD/ : { *(.note.GNU-stack) *(.comment) *(.eh_frame) *(.note.gnu.property) }
}
'''

# Each of the port's files is in one of the link's two groups
# (docs/NETPLAY.md, "State apart from presentation"): the game's state, or
# outside it (the picture, the run's output, the program, its host, the
# snapshot's own bookkeeping). The variables of the files outside are
# linked after the rest of their kind, so that what src/port/state.c saves
# of the program's variables is two runs of memory (include/port_state.h).
STATE_GROUPS = 'config/port_state.txt'
GROUPS = ('state', 'outside')


def state_groups():
    """{path: 'state' or 'outside'} of config/port_state.txt."""
    groups = {}
    with open(STATE_GROUPS, encoding='utf-8') as f:
        for number, line in enumerate(f, 1):
            words = line.split()
            if not words or words[0].startswith('#'):
                continue
            if words[0] not in GROUPS or len(words) < 2 or words[1] in groups:
                raise SystemExit('port: %s:%d: not "state PATH" nor "outside PATH", or a path said twice: %s'
                                 % (STATE_GROUPS, number, line.strip()))
            groups[words[1]] = words[0]
    return groups


def check_state_groups(paths, folder):
    """Each of the port's files of `folder` that the build links (`paths`)
    is in one group, and each file of that folder the groups name is there:
    a line for each that is not, or none."""
    groups = state_groups()
    named = [path for path in groups if os.path.dirname(path) == folder.rstrip('/')]
    problems = ['%s is in neither group of %s: put it in "state" or "outside" (docs/NETPLAY.md, '
                '"State apart from presentation")' % (path, STATE_GROUPS)
                for path in paths if path not in groups]
    problems += ['%s names %s, which is not built' % (STATE_GROUPS, path) for path in named if path not in paths]
    return problems


def outside_state_patterns():
    """The objects of the files outside the state, as a link script's
    EXCLUDE_FILE names them: by the object's name (object_of), in whatever
    folder."""
    return ' '.join('*%s.o' % path[:-2].replace('/', '_')
                    for path, group in sorted(state_groups().items()) if group == 'outside')


HOST_PROBE ='void _start(void) { for (;;) { } }\n'


def bits_of(args):
    """The size the command line (`--bits N`, which is taken out of
    `args`) or PORT_BITS asks for, the default if neither does; None, with
    a line saying so, if it is neither 32 nor 64."""
    bits = os.environ.get('PORT_BITS', '') or str(DEFAULT_BITS)
    if '--bits' in args:
        at = args.index('--bits')
        bits = args[at + 1] if at + 1 < len(args) else ''
        del args[at:at + 2]
    if bits not in ('32', '64'):
        print('port: the program has 32-bit pointers or 64-bit ones (--bits, PORT_BITS), not "%s"' % bits)
        return None
    return int(bits)


def set_bits(bits):
    """The program of `bits`-bit pointers: where it is built, and how."""
    global BITS, OUT_DIR, OBJ_DIR, PROGRAM, NAMES, LD_SCRIPT, LAYOUT, GAME_FLAGS, LINK_FLAGS
    BITS = bits
    OUT_DIR = 'build/port' if bits == DEFAULT_BITS else 'build/port%d' % bits
    # PORT_OPT=2: the game and the port optimised at -O2, into a folder of
    # its own (build/port-O2/), to be measured against the program the
    # checks and the play tests use (docs/NETPLAY.md, section 10)
    optimise = os.environ.get('PORT_OPT', '')
    if optimise:
        OUT_DIR += '-O' + optimise
        GAME_FLAGS = ['-O' + optimise if f == '-O1' else f for f in GAME_FLAGS]
    OBJ_DIR = OUT_DIR + '/obj'
    PROGRAM = OUT_DIR + '/rebushido'
    NAMES = PROGRAM + '.sym'
    LD_SCRIPT = PROGRAM + '.ld'
    LAYOUT = OUT_DIR + '/layout.txt'
    size = '-m%d' % bits
    GAME_FLAGS = [size if f in ('-m32', '-m64') else f for f in GAME_FLAGS if f not in NO_VECTORS]
    if bits == 64:
        GAME_FLAGS += NO_VECTORS
    LINK_FLAGS = [size if f in ('-m32', '-m64') else f for f in LINK_FLAGS]


# Flags of one of the port's files. src/port/vorbis.c compiles stb_vorbis
# as it ships (docs/MODS.md, "Music"): the C library's headers it names are
# the port's empty ones, and it is the one file whose numbers are not all
# whole, so the 64-bit program lets it have the vector registers that the
# x86-64 calling convention passes such numbers in.
PORT_OWN_FLAGS = {'src/port/vorbis.c': ['-Isrc/port/vendor/libc', '-msse', '-msse2', '-Os']}
# Files whose variables keep the boundaries the compiler gave them
# (cap_alignment is not run on them): none of the game's variables is
# among them, and the vector instructions of src/port/vorbis.c read its
# constants on the 16-byte boundaries it was compiled for.
NATURAL_ALIGNMENT = {'src/port/vorbis.c'}


def sources():
    """(path, the port's own, flags of its own) for each file of the program."""
    out = [('src/main/' + f, False, []) for f in sorted(os.listdir('src/main')) if f.endswith('.c')]
    out += [('src/rebushido/' + f, False, []) for f in sorted(os.listdir('src/rebushido'))
            if f.endswith('.c') and f not in NOT_THE_PORTS]
    for folder in sorted(SONY_FOLDERS):
        out += [(SONY + folder + '/' + f, False, []) for f in sorted(os.listdir(SONY + folder))
                if f.endswith('.c') and f not in SONY_FOLDERS[folder]]
    out += [(SONY + f, False, []) for f in SONY_FILES]
    out += [(path, False, flags) for path, flags in MOVOVL_FILES]
    out += [('src/port/' + f, True, PORT_OWN_FLAGS.get('src/port/' + f, []))
            for f in sorted(os.listdir('src/port')) if f.endswith('.c')]
    return out


def environment():
    # the compiler's temporary files beside the objects: /tmp is emptied
    # under a WSL that has just started
    return dict(os.environ, TMPDIR=os.path.abspath(OUT_DIR), LC_ALL='C')


def check_host():
    """None, or why this host cannot build the port."""
    os.makedirs(OUT_DIR, exist_ok=True)
    try:
        proc = subprocess.run(['gcc', '-m%d' % BITS, '-ffreestanding', '-nostdinc', '-nostdlib', '-static', '-no-pie',
                               '-x', 'c', '-', '-o', OUT_DIR + '/host_probe'],
                              input=HOST_PROBE, capture_output=True, text=True, env=environment())
    except OSError as error:
        return 'no gcc here (%s)' % error
    if proc.returncode != 0:
        return ('this gcc cannot make a %d-bit x86 program (gcc -m%d -nostdlib): %s'
                % (BITS, BITS, (proc.stderr.strip().split('\n') or ['?'])[-1]))
    os.remove(OUT_DIR + '/host_probe')
    return None


def git_hash():
    try:
        short = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True)
        if short.returncode != 0:
            return 'unknown'
        dirty = subprocess.run(['git', 'diff', '--quiet', 'HEAD']).returncode != 0
        return short.stdout.strip() + ('-dirty' if dirty else '')
    except OSError:
        return 'unknown'


def object_of(path):
    return '%s/%s.o' % (OBJ_DIR, path[:-2].replace('/', '_'))


def newest_input(obj):
    """The time of the newest file the object was made from, by the list
    the compiler left beside it; None if there is no list."""
    try:
        with open(obj[:-2] + '.d', encoding='utf-8') as f:
            words = f.read().replace('\\\n', ' ').split()
    except OSError:
        return None
    newest = 0.0
    for word in words[1:]:
        if word == '-' or word.endswith(':'):
            continue
        try:
            newest = max(newest, os.path.getmtime(word))
        except OSError:
            return None
    return newest


def compile_one(job):
    path, own, mine, flags, stamp = job
    obj = object_of(path)
    if os.path.exists(obj):
        newest = newest_input(obj)
        made = os.path.getmtime(obj)
        if newest is not None and newest < made and os.path.getmtime(path) < made and stamp < made:
            return path, obj, False, ''
    if own:
        # the port's own: its warnings on, and errors
        flags = [f for f in flags if f != '-w'] + PORT_WARNINGS
    else:
        # -w would silence the ones that are errors too: the others are
        # left on and not shown
        flags = [f for f in flags if f != '-w'] + GAME_ERRORS + [INSTRUMENT]
    flags = flags + mine
    cmd = ['gcc'] + flags + ['-MD', '-MF', obj[:-2] + '.d', '-x', 'c', '-c', '-', '-o', obj]
    proc = subprocess.run(cmd, input=modern.modern_source(path), capture_output=True, text=True,
                          env=environment(), encoding='utf-8', errors='replace')
    if proc.returncode != 0:
        if os.path.exists(obj):
            os.remove(obj)
        return path, None, True, proc.stderr
    if path not in NATURAL_ALIGNMENT:
        cap_alignment(obj)
    return path, obj, True, proc.stderr


def cap_alignment(obj):
    """Gives every variable's section in an object the boundary the
    PlayStation's compiler gives the variable: a word's for an array or a
    structure, and for anything else its own size's, at most a word's
    (WORD, above, says why). A variable is in a section of its own, named
    for its kind and itself; it is an array or a structure if it is
    larger than the boundary this compiler gave it, or was given more than
    a word's."""
    with open(obj, 'rb') as f:
        data = bytearray(f.read())
    if data[:4] != b'\x7fELF' or data[4] not in (1, 2):
        sys.exit('port_build.py: %s is not an object' % obj)
    if data[4] == 2:
        start, = struct.unpack_from('<Q', data, 0x28)
        size, count, names = struct.unpack_from('<HHH', data, 0x3A)
        name_field, bytes_field, boundary_field, form = 0x18, 0x20, 0x30, '<Q'
    else:
        start, = struct.unpack_from('<I', data, 0x20)
        size, count, names = struct.unpack_from('<HHH', data, 0x2E)
        name_field, bytes_field, boundary_field, form = 0x10, 0x14, 0x20, '<I'
    names_at, = struct.unpack_from(form, data, start + names * size + name_field)
    changed = False
    for i in range(count):
        at = start + i * size
        name_at, = struct.unpack_from('<I', data, at)
        length, = struct.unpack_from(form, data, at + bytes_field)
        boundary, = struct.unpack_from(form, data, at + boundary_field)
        end = data.index(b'\0', names_at + name_at)
        name = data[names_at + name_at:end].decode('latin-1')
        if not name.startswith(tuple(kind + '.' for kind in DATA_KINDS)):
            continue
        wanted = WORD if boundary > WORD or length > boundary else boundary
        if wanted != boundary:
            struct.pack_into(form, data, at + boundary_field, wanted)
            changed = True
    if changed:
        with open(obj, 'wb') as f:
            f.write(data)


def playstation_addresses():
    """{name: address} of the PlayStation program."""
    with open(PLAYSTATION_NAMES, encoding='utf-8') as f:
        return {m.group(1): int(m.group(2), 16)
                for m in re.finditer(r'^(\w+) = (0x[0-9A-Fa-f]+);', f.read(), re.M)}


def write_ld_script(objects):
    """The link's script: each kind of variable's sections that are named
    for a variable of the PlayStation program, in its order."""
    playstation = playstation_addresses()
    found = {kind: set() for kind in DATA_KINDS}
    for obj in objects:
        for sec in Elf(obj).secs:
            for kind in DATA_KINDS:
                if sec['name'].startswith(kind + '.') and sec['name'][len(kind) + 1:] in playstation:
                    found[kind].add(sec['name'][len(kind) + 1:])
    lines = {'outside': outside_state_patterns()}
    for kind in DATA_KINDS:
        ordered = sorted(found[kind], key=lambda name: playstation[name])
        lines[kind[1:]] = '\n'.join('        *(%s.%s)' % (kind, name) for name in ordered)
    with open(LD_SCRIPT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(LD_SCRIPT_TEXT % lines)


def check_layout(grown=()):
    """How many neighbours of the PlayStation program are as far apart
    here. Returns (as far apart, pairs, how many of the others have for
    their first a variable of `grown`, which a 64-bit program lays out
    with wider pointers); the others go to LAYOUT."""
    playstation = playstation_addresses()
    elf = Elf(PROGRAM)
    here = {}
    for s in elf.syms:
        if s['name'] in playstation and s['type'] == 1 and s['shndx'] not in (0, 0xFFF1):
            here[s['name']] = (elf.secs[s['shndx']]['name'], s['value'])
    names = sorted(here, key=lambda name: playstation[name])
    same, pairs, wider, moved = 0, 0, 0, []
    for a, b in zip(names, names[1:]):
        if here[a][0] != here[b][0]:
            continue  # the next of another kind: no neighbours
        pairs += 1
        there_gap = playstation[b] - playstation[a]
        here_gap = here[b][1] - here[a][1]
        if there_gap == here_gap:
            same += 1
        else:
            wider += a in grown
            moved.append('%-40s %-40s 0x%X on the PlayStation, 0x%X here%s'
                         % (a, b, there_gap, here_gap & 0xFFFFFFFF, '  (the first has pointers)' if a in grown else ''))
    with open(LAYOUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write("# Neighbours among the game's variables that are not as far apart in the port as on\n"
                '# the PlayStation (tools/port_build.py): the first, the next, and the two distances.\n')
        f.write('\n'.join(moved) + '\n')
    return same, pairs, wider


def place_here(runs, offset, size):
    """Where the `size` bytes at `offset` of a variable as the PlayStation
    lays it out are in this program's (tools/port_layout.py,
    console_runs); None if they are not in one piece here."""
    for there, here, count in runs:
        if there <= offset and offset + size <= there + count:
            return here + offset - there
    return None


def state_lines(symbols, layouts):
    """The `var` lines of a play test's plan for this program: the values of
    tests/play/state.toml at the program's own addresses. A value inside a
    variable that is laid out otherwise here than on the PlayStation, which
    is where the file's offsets are counted, is found by its layout."""
    types = {'u8': (1, 0), 's8': (1, 1), 'u16': (2, 0), 's16': (2, 1), 'u32': (4, 0), 's32': (4, 1)}
    found = {}
    with open(STATE, encoding='utf-8') as f:
        for m in re.finditer(r'^(\w+)\s*=\s*"([^":]+):(\w+)"', f.read(), re.M):
            name, where, kind = m.groups()
            total, offset, variable = 0, 0, None
            for word in where.split('+'):
                word = word.strip()
                if re.fullmatch(r'0[xX][0-9A-Fa-f]+|\d+', word):
                    offset += int(word, 0)
                elif word in symbols:
                    total += symbols[word]
                    variable = word
                else:
                    total = None
                    break
            if total is None or kind not in types:
                continue
            if variable in layouts:
                offset = place_here(layouts[variable][1], offset, types[kind][0])
                if offset is None:
                    sys.exit('port_build.py: %s: `%s = %s` is not one value of `%s` as this program lays it out'
                             % (STATE, name, where, variable))
            found[name] = (total + offset,) + types[kind]
    # in the order of the plans (tools/playtest.py): by name
    return ['var %s %d %d %d' % ((name,) + found[name]) for name in sorted(found)]


def write_names():
    """The names beside the program: the values a run can watch, every
    name with its address and, for a 64-bit program, how each variable
    that grew lies against the PlayStation's (`layout` and `run` lines:
    tools/port_layout.py, console_runs)."""
    elf = Elf(PROGRAM)
    layouts = port_layout.console_runs(PROGRAM) if BITS == 64 else {}
    symbols, rows = {}, []
    for s in elf.syms:
        if not s['name'] or s['shndx'] == 0 or s['type'] not in (0, 1, 2):
            continue
        if s['bind'] in (1, 2) or s['name'] not in symbols:
            symbols[s['name']] = s['value']
        rows.append((s['value'], 'f' if s['type'] == 2 else 'o', s['size'], s['name']))
    with open(NAMES, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# The names of %s, for the program itself: written by tools/port_build.py.\n' % PROGRAM)
        for line in state_lines(symbols, layouts):
            f.write(line + '\n')
        for name in sorted(layouts):
            f.write('layout %s %d\n' % (name, layouts[name][0]))
            for run in layouts[name][1]:
                f.write('run %d %d %d\n' % run)
        for value, kind, size, name in sorted(set(rows)):
            f.write('sym %08x %s %d %s\n' % (value, kind, size, name))
    return len(rows), set(layouts)


def main():
    os.chdir(ROOT)
    args = sys.argv[1:]
    bits = bits_of(args)
    if bits is None:
        return 1
    set_bits(bits)
    if '--list' in args:
        for path, own, mine in sources():
            print(path)
        return 0
    problem = check_host()
    if problem:
        print('port: %s' % problem)
        print('port: it is an x86 Linux program with no C library: it needs a gcc that takes -m%d '
              '(x86-64 Linux, or WSL) and nothing installed beside it' % BITS)
        return 1
    if '--host' in args:
        print('port: this host can build it')
        return 0
    problems = check_state_groups([path for path, own, mine in sources() if own], 'src/port')
    if problems:
        print('\n'.join('port: ' + problem for problem in problems))
        return 1
    os.makedirs(OBJ_DIR, exist_ok=True)
    host_tests.OUT_DIR = OUT_DIR
    host_tests.write_headers(True)

    flags = (GAME_FLAGS + host_tests.include_flags(True) + modern.DEFS + host_tests.REBUSHIDO_DEFS
             + MOVOVL_INCLUDES)
    flags += ['-DRB_GIT_HASH="%s"' % git_hash()]
    # a change to how it is built builds everything again
    stamp = max(os.path.getmtime(p) for p in (__file__, 'tests/run.py', 'tools/modern/check.py',
                                              'tools/port_gte_macros.py', modern.SHIM))
    hash_file = OUT_DIR + '/git_hash.txt'
    old = open(hash_file).read() if os.path.exists(hash_file) else ''
    if old != git_hash():
        with open(hash_file, 'w', newline='\n') as f:
            f.write(git_hash())
        version = object_of('src/rebushido/version.c')
        if os.path.exists(version):
            os.remove(version)

    jobs = [(path, own, mine, flags, stamp) for path, own, mine in sources()]
    objects, failed, built = [], 0, 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for path, obj, compiled, messages in pool.map(compile_one, jobs):
            built += compiled
            if obj is None:
                failed += 1
                print('port: %s does not compile:' % path)
                print('\n'.join('    ' + line for line in messages.strip().split('\n')[:40]))
            else:
                objects.append(obj)
    if failed:
        print('FAILED: port: %d files do not compile' % failed)
        return 1

    write_ld_script(objects)
    proc = subprocess.run(['gcc'] + LINK_FLAGS + ['-Wl,-T,' + LD_SCRIPT, '-o', PROGRAM] + objects,
                          capture_output=True, text=True, env=environment(), errors='replace')
    if proc.returncode != 0:
        missing = sorted(set(re.findall(r"undefined reference to `(\w+)'", proc.stderr)))
        other = [line for line in proc.stderr.split('\n') if line and 'undefined reference' not in line
                 and ': in function' not in line.lower()]
        if missing:
            print('port: %d names the program uses are defined nowhere:' % len(missing))
            print('    ' + ' '.join(missing))
        print('\n'.join('    ' + line for line in other[:30]))
        print('FAILED: port: it does not link')
        return 1
    count, grown = write_names()
    same, pairs, wider = check_layout(grown)
    print("port: %d of the game's %d pairs of neighbouring variables are as far apart as on the PlayStation "
          '(the others: %s%s)' % (same, pairs, LAYOUT,
                                 '; %d of them follow a variable whose pointers are wider here' % wider
                                 if BITS == 64 else ''))
    print('OK: port: %s links, a %d-bit program: %d files (%d compiled now), %d names, %d bytes'
          % (PROGRAM, BITS, len(objects), built, count, os.path.getsize(PROGRAM)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
