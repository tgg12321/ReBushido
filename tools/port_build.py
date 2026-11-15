#!/usr/bin/env python3
"""Build the native port (docs/PORT.md): build/port/rebushido.

    python3 tools/port_build.py            build what changed, link, write the names
    python3 tools/port_build.py --list     the files it is made of
    python3 tools/port_build.py --host     only say whether this host can build it

`make port` and `wsl bash tools/build.sh port` run it. The program is the
game's own C with the port's switch on (RB_PORT, include/rebushido.h):

  - every file of src/main/ and src/rebushido/ (quick boot's aside, which is
    its own build), compiled as the host tests compile them;
  - Sony's library code only where it is plain arithmetic on memory
    (SONY_FOLDERS and SONY_FILES below; the rule is in docs/PORT.md,
    "Sony's libraries in the port");
  - src/port/: the GTE, the platform layer (every other function of Sony's
    and of the BIOS the game calls), memory, the disc image's reader.

It is a 32-bit x86 Linux program linked with no C library, as the host tests
are (tests/README.md): the game keeps addresses in 32-bit integers, and a
32-bit C library is not always installed. So it needs the host's gcc with
-m32 and python3, and nothing else; where gcc cannot make such a program
this says so and exits 1.

The game's files are compiled with their warnings off (`make
rebushido-modern` is where those are read). The port's own files are
compiled with that check's warnings as errors.

The game's variables are linked in the order the PlayStation program has
them (config/symbol_addrs.txt), each kind among its own (set, not set,
constant): the game takes some variables for neighbours, reading two or
three of them as one array, and they have to stay neighbours. Each is
compiled into a section of its own, and the link is by a script this
writes, build/port/rebushido.ld. After the link it counts the pairs of
neighbours that are as far apart in the port as on the PlayStation, and
writes the ones that are not to build/port/layout.txt.

Beside the program it writes build/port/rebushido.sym: every name of the
program with its address, and the values of tests/play/state.toml as the
play tests' plans name them. The program reads it for --trace and --plan,
and to say where it was if it crashes.
"""
import concurrent.futures
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'modern'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import check as modern  # noqa: E402  (tools/modern/check.py)
import run as host_tests  # noqa: E402  (tests/run.py)
from elf32 import Elf  # noqa: E402

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
# The kinds of variable, by the section the compiler puts each in.
DATA_KINDS = ('.rodata', '.data', '.bss')

# The program is linked 4 MB up from where Linux usually has one, and says
# where its variables begin and end: an ordering table's link is the low 24
# bits of an address, and those of the program's variables must not be
# those of the PlayStation's RAM, which is the 2 MB from 0x80000000
# (src/port/gpu.c, port_AddLinkRegion).
LD_SCRIPT_TEXT = '''/* Written by tools/port_build.py: the native port's link. The game's
 * variables are in the order of the PlayStation program, kind by kind. */
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
%(data)s
        *(.data .data.*)
    }
    .bss : {
%(bss)s
        *(.bss .bss.*) *(COMMON)
    }
    g_port_data_end = .;
    /DISCARD/ : { *(.note.GNU-stack) *(.comment) *(.eh_frame) *(.note.gnu.property) }
}
'''

HOST_PROBE = 'void _start(void) { for (;;) { } }\n'


def sources():
    """(path, the port's own) for each file of the program."""
    out = [('src/main/' + f, False) for f in sorted(os.listdir('src/main')) if f.endswith('.c')]
    out += [('src/rebushido/' + f, False) for f in sorted(os.listdir('src/rebushido'))
            if f.endswith('.c') and f not in NOT_THE_PORTS]
    for folder in sorted(SONY_FOLDERS):
        out += [(SONY + folder + '/' + f, False) for f in sorted(os.listdir(SONY + folder))
                if f.endswith('.c') and f not in SONY_FOLDERS[folder]]
    out += [(SONY + f, False) for f in SONY_FILES]
    out += [('src/port/' + f, True) for f in sorted(os.listdir('src/port')) if f.endswith('.c')]
    return out


def environment():
    # the compiler's temporary files beside the objects: /tmp is emptied
    # under a WSL that has just started
    return dict(os.environ, TMPDIR=os.path.abspath(OUT_DIR), LC_ALL='C')


def check_host():
    """None, or why this host cannot build the port."""
    os.makedirs(OUT_DIR, exist_ok=True)
    try:
        proc = subprocess.run(['gcc', '-m32', '-ffreestanding', '-nostdinc', '-nostdlib', '-static', '-no-pie',
                               '-x', 'c', '-', '-o', OUT_DIR + '/host_probe'],
                              input=HOST_PROBE, capture_output=True, text=True, env=environment())
    except OSError as error:
        return 'no gcc here (%s)' % error
    if proc.returncode != 0:
        return ('this gcc cannot make a 32-bit x86 program (gcc -m32 -nostdlib): %s'
                % (proc.stderr.strip().split('\n') or ['?'])[-1])
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
    path, own, flags, stamp = job
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
        flags = flags + [INSTRUMENT]
    cmd = ['gcc'] + flags + ['-MD', '-MF', obj[:-2] + '.d', '-x', 'c', '-c', '-', '-o', obj]
    proc = subprocess.run(cmd, input=modern.modern_source(path), capture_output=True, text=True,
                          env=environment(), encoding='utf-8', errors='replace')
    if proc.returncode != 0:
        if os.path.exists(obj):
            os.remove(obj)
        return path, None, True, proc.stderr
    return path, obj, True, proc.stderr


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
    lines = {}
    for kind in DATA_KINDS:
        ordered = sorted(found[kind], key=lambda name: playstation[name])
        lines[kind[1:]] = '\n'.join('        *(%s.%s)' % (kind, name) for name in ordered)
    with open(LD_SCRIPT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(LD_SCRIPT_TEXT % lines)


def check_layout():
    """How many neighbours of the PlayStation program are as far apart
    here. Returns (as far apart, pairs); the others go to LAYOUT."""
    playstation = playstation_addresses()
    elf = Elf(PROGRAM)
    here = {}
    for s in elf.syms:
        if s['name'] in playstation and s['type'] == 1 and s['shndx'] not in (0, 0xFFF1):
            here[s['name']] = (elf.secs[s['shndx']]['name'], s['value'])
    names = sorted(here, key=lambda name: playstation[name])
    same, pairs, moved = 0, 0, []
    for a, b in zip(names, names[1:]):
        if here[a][0] != here[b][0]:
            continue  # the next of another kind: no neighbours
        pairs += 1
        there_gap = playstation[b] - playstation[a]
        here_gap = here[b][1] - here[a][1]
        if there_gap == here_gap:
            same += 1
        else:
            moved.append('%-40s %-40s 0x%X on the PlayStation, 0x%X here' % (a, b, there_gap, here_gap & 0xFFFFFFFF))
    with open(LAYOUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write("# Neighbours among the game's variables that are not as far apart in the port as on\n"
                '# the PlayStation (tools/port_build.py): the first, the next, and the two distances.\n')
        f.write('\n'.join(moved) + '\n')
    return same, pairs


def state_lines(symbols):
    """The `var` lines of a play test's plan for this program: the values of
    tests/play/state.toml at the program's own addresses."""
    types = {'u8': (1, 0), 's8': (1, 1), 'u16': (2, 0), 's16': (2, 1), 'u32': (4, 0), 's32': (4, 1)}
    found = {}
    with open(STATE, encoding='utf-8') as f:
        for m in re.finditer(r'^(\w+)\s*=\s*"([^":]+):(\w+)"', f.read(), re.M):
            name, where, kind = m.groups()
            total = 0
            for word in where.split('+'):
                word = word.strip()
                if re.fullmatch(r'0[xX][0-9A-Fa-f]+|\d+', word):
                    total += int(word, 0)
                elif word in symbols:
                    total += symbols[word]
                else:
                    total = None
                    break
            if total is not None and kind in types:
                found[name] = (total,) + types[kind]
    # in the order of the plans (tools/playtest.py): by name
    return ['var %s %d %d %d' % ((name,) + found[name]) for name in sorted(found)]


def write_names():
    elf = Elf(PROGRAM)
    symbols, rows = {}, []
    for s in elf.syms:
        if not s['name'] or s['shndx'] == 0 or s['type'] not in (0, 1, 2):
            continue
        if s['bind'] in (1, 2) or s['name'] not in symbols:
            symbols[s['name']] = s['value']
        rows.append((s['value'], 'f' if s['type'] == 2 else 'o', s['size'], s['name']))
    with open(NAMES, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# The names of %s, for the program itself: written by tools/port_build.py.\n' % PROGRAM)
        for line in state_lines(symbols):
            f.write(line + '\n')
        for value, kind, size, name in sorted(set(rows)):
            f.write('sym %08x %s %d %s\n' % (value, kind, size, name))
    return len(rows)


def main():
    os.chdir(ROOT)
    args = sys.argv[1:]
    if '--list' in args:
        for path, own in sources():
            print(path)
        return 0
    problem = check_host()
    if problem:
        print('port: %s' % problem)
        print('port: it is a 32-bit x86 Linux program with no C library: it needs a gcc that takes -m32 '
              '(x86-64 Linux, or WSL) and nothing installed beside it')
        return 1
    if '--host' in args:
        print('port: this host can build it')
        return 0
    os.makedirs(OBJ_DIR, exist_ok=True)
    host_tests.OUT_DIR = OUT_DIR
    host_tests.write_headers(True)

    flags = GAME_FLAGS + host_tests.include_flags(True) + modern.DEFS + host_tests.REBUSHIDO_DEFS
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

    jobs = [(path, own, flags, stamp) for path, own in sources()]
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
    count = write_names()
    same, pairs = check_layout()
    print("port: %d of the game's %d pairs of neighbouring variables are as far apart as on the PlayStation "
          '(the others: %s)' % (same, pairs, LAYOUT))
    print('OK: port: %s links: %d files (%d compiled now), %d names, %d bytes'
          % (PROGRAM, len(objects), built, count, os.path.getsize(PROGRAM)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
