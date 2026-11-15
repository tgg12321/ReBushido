#!/usr/bin/env python3
"""Build the native port with a window (docs/PORT.md, "The window").

    python3 tools/port_sdl_build.py             for this host: build/port-sdl/rebushido
    python3 tools/port_sdl_build.py --windows   for Windows, with MinGW: build/port-sdl/rebushido.exe,
                                                SDL2.dll beside it, and rebushido-headless.exe
    python3 tools/port_sdl_build.py --host [--windows]   only say whether this host can build it

`make port-sdl` and `make port-sdl-windows` run it (`wsl bash tools/build.sh
port-sdl`). It is the same program as `make port` builds (tools/
port_build.py), from the same files compiled the same way, and then:

  - the four files of src/port/window/ that are plain arithmetic (where the
    picture goes, the sound's queue, the clock, the keys), compiled as the
    port's own files are;
  - the files that talk to SDL 2 and to a C library (src/port/window/
    sdl_main.c, sdl_window.c, and host_posix.c or src/port/host_windows.c),
    compiled as any program is;
  - SDL 2 and a C library, which `make port` has neither of.

It is local: no check runs it, and CI needs nothing it needs.

The game defines names a C library has too (`main`, `printf`, `memcpy`,
`open`, `read`, `rand`: Sony's library, and the BIOS's card files). So
everything of the game and the port is first linked into one object,
build/port-sdl/<target>/game.o, in which every name is made that object's
own except the port's (`port_...`, `g_port_...`): the C library and SDL
then never see the game's `open`, and the game never calls theirs. What
that object still asks for is listed and must be the host's functions
(include/port_host.h) and nothing else. The game's variables keep the
PlayStation's order inside it, as in `make port`.

What it needs:

  Linux    gcc with -m32, a 32-bit C library and 32-bit SDL 2. On Ubuntu:
           sudo dpkg --add-architecture i386 && sudo apt update
           sudo apt install lib32gcc-13-dev libsdl2-dev:i386 libsdl2-dev
           (NOT gcc-multilib: it takes the MIPS cross compiler away.)
  Windows  i686-w64-mingw32-gcc (apt install gcc-mingw-w64-i686) and SDL 2's
           development package for MinGW, unpacked: the folder that holds
           i686-w64-mingw32/ is named by SDL2_MINGW, or is
           tools/sdl2-mingw/SDL2-*/ or build/sdl2-mingw/SDL2-*/ (BUILD.md).
"""
import concurrent.futures
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_build  # noqa: E402  (tools/port_build.py)
from port_build import host_tests, modern  # noqa: E402

OUT_DIR = 'build/port-sdl'
WINDOW_DIR = 'src/port/window/'
# The window's arithmetic: compiled as the port's own files, and tested on
# every push (tests/port/window.c).
WINDOW_PLAIN = ['scale.c', 'ring.c', 'pace.c', 'keymap.c']
# What talks to SDL and to the C library: compiled as any program is.
WINDOW_HOSTED = ['sdl_main.c', 'sdl_window.c']
# The hosts of the build without a window: not this one's on Linux, where
# the C library's is host_posix.c; on Windows the one file is both builds'.
OTHER_HOSTS = ('src/port/host_linux.c', 'src/port/host_windows.c')
HOSTED_FLAGS = ['-O2', '-g', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-D_FILE_OFFSET_BITS=64',
                '-Iinclude', '-I' + WINDOW_DIR.rstrip('/')]
# Where the program is linked: 4 MB up, as `make port` has it, so that the
# low 24 bits of its variables' addresses are no other memory's
# (src/port/gpu.c, port_AddLinkRegion). They have to end below this.
PROGRAM_BASE = 0x08400000
PROGRAM_END = 0x08800000
SDL_MINGW_PLACES = ['tools/sdl2-mingw/SDL2-*', 'build/sdl2-mingw/SDL2-*']
SDL_PROBE = '#include <SDL.h>\nint main(void) { return SDL_Init(0); }\n'

# The object of the game and the port: the variables in the PlayStation's
# order, kind by kind, and marks at each kind's two ends.
BLOB_LD_TEXT = '''/* Written by tools/port_sdl_build.py: the game and the port as one object.
 * The game's variables are in the order of the PlayStation program. */
%(format)sSECTIONS
{
    %(rodata_name)s : {
        %(under)sg_port_blob_rodata = .;
%(rodata)s
        *(%(rodata_name)s %(rodata_name)s%(dot)s*)
        %(under)sg_port_blob_rodata_end = .;
    }
    .data : {
        %(under)sg_port_blob_data = .;
%(data)s
        *(.data .data%(dot)s*)
        %(under)sg_port_blob_data_end = .;
    }
    .bss : {
        %(under)sg_port_blob_bss = .;
%(bss)s
        *(.bss .bss%(dot)s*) *(COMMON)
        %(under)sg_port_blob_bss_end = .;
    }
}
'''


class Target:
    """What differs between the two programs."""

    def __init__(self, windows):
        self.windows = windows
        self.name = 'windows' if windows else 'linux'
        self.dir = '%s/%s' % (OUT_DIR, self.name)
        self.obj_dir = self.dir + '/obj'
        if windows:
            prefix = 'i686-w64-mingw32-'
            # the one that needs no thread library beside the program
            self.cc = shutil.which(prefix + 'gcc-win32') and prefix + 'gcc-win32' or prefix + 'gcc'
            self.objcopy, self.nm, self.objdump = prefix + 'objcopy', prefix + 'nm', prefix + 'objdump'
            self.program = OUT_DIR + '/rebushido.exe'
            self.under = '_'  # a C name has one before it in a Windows object
            # a variable's section is `.data$name`; constants are `.rdata`
            self.kinds = {'.rodata': '.rdata', '.data': '.data', '.bss': '.bss'}
            self.dot = '$'
            # the game's bitfields as on every other host, not Microsoft's way
            self.game_flags = ['-mno-ms-bitfields']
            self.sdl = self.find_sdl()
            self.sdl_flags = ['-I%s/include/SDL2' % self.sdl] if self.sdl else []
            self.hosted = [WINDOW_DIR + f for f in WINDOW_HOSTED] + ['src/port/host_windows.c']
        else:
            self.cc, self.objcopy, self.nm, self.objdump = 'gcc', 'objcopy', 'nm', 'objdump'
            self.program = OUT_DIR + '/rebushido'
            self.under = ''
            self.kinds = {'.rodata': '.rodata', '.data': '.data', '.bss': '.bss'}
            self.dot = '.'
            self.game_flags = []
            self.sdl = None
            self.sdl_flags = ['-I/usr/include/SDL2', '-D_REENTRANT']
            self.hosted = [WINDOW_DIR + f for f in WINDOW_HOSTED] + [WINDOW_DIR + 'host_posix.c']

    @staticmethod
    def find_sdl():
        places = [os.environ['SDL2_MINGW']] if os.environ.get('SDL2_MINGW') else []
        for pattern in SDL_MINGW_PLACES:
            places += sorted(glob.glob(pattern), reverse=True)
        for place in places:
            if os.path.isfile(place + '/i686-w64-mingw32/include/SDL2/SDL.h'):
                return place.rstrip('/') + '/i686-w64-mingw32'
        return None

    def object_of(self, path):
        return '%s/%s.o' % (self.obj_dir, path[:-2].replace('/', '_'))

    def link_flags(self):
        if self.windows:
            return ['-L%s/lib' % self.sdl, '-lSDL2', '-lcomdlg32', '-static-libgcc',
                    '-Wl,--image-base=0x%X' % PROGRAM_BASE, '-Wl,--large-address-aware',
                    '-Wl,--disable-dynamicbase']
        return ['-m32', '-no-pie', '-lSDL2', '-Wl,-Ttext-segment=0x%X' % PROGRAM_BASE]


def run(cmd, **more):
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=port_build.environment(), **more)


def check_host(target):
    """None, or why this host cannot build the program."""
    os.makedirs(target.dir, exist_ok=True)
    if not shutil.which(target.cc):
        return 'no %s here%s' % (target.cc, ' (apt install gcc-mingw-w64-i686)' if target.windows else '')
    if target.windows and not target.sdl:
        return ("SDL 2's development package for MinGW is not here: unpack SDL2-devel-2.x.y-mingw.tar.gz "
                '(github.com/libsdl-org/SDL/releases) and name its SDL2-2.x.y folder with SDL2_MINGW, or put it '
                'in tools/sdl2-mingw/ (BUILD.md)')
    probe = target.dir + '/sdl_probe' + ('.exe' if target.windows else '')
    proc = run([target.cc] + (['-DSDL_MAIN_HANDLED'] if target.windows else ['-m32']) + target.sdl_flags
               + ['-x', 'c', '-', '-o', probe] + [f for f in target.link_flags() if not f.startswith('-Wl,-T')],
               input=SDL_PROBE)
    if proc.returncode != 0:
        return ('%s cannot make a 32-bit program with SDL 2: %s%s'
                % (target.cc, (proc.stderr.strip().split('\n') or ['?'])[-1],
                   '' if target.windows else
                   '\nport-sdl: on Ubuntu: sudo dpkg --add-architecture i386 && sudo apt update && sudo apt install '
                   'lib32gcc-13-dev libsdl2-dev:i386 libsdl2-dev   (not gcc-multilib: it removes the MIPS compiler)'))
    os.remove(probe)
    return None


def compile_one(job):
    """As tools/port_build.py compiles a file, with this target's compiler
    and into its folder; `kind` is game, port (the port's own: its warnings
    are errors) or hosted (with SDL and a C library)."""
    target, path, kind, flags, stamp = job
    obj = target.object_of(path)
    if os.path.exists(obj):
        newest = port_build.newest_input(obj)
        made = os.path.getmtime(obj)
        if newest is not None and newest < made and os.path.getmtime(path) < made and stamp < made:
            return path, obj, False, ''
    if kind == 'hosted':
        cmd = [target.cc] + ([] if target.windows else ['-m32']) + HOSTED_FLAGS + target.sdl_flags
        cmd += ['-MD', '-MF', obj[:-2] + '.d', '-c', path, '-o', obj]
        proc = run(cmd)
    else:
        if kind == 'port':
            flags = [f for f in flags if f != '-w'] + port_build.PORT_WARNINGS + ['-I' + WINDOW_DIR.rstrip('/')]
        else:
            flags = flags + [port_build.INSTRUMENT]
        cmd = [target.cc] + flags + target.game_flags + ['-MD', '-MF', obj[:-2] + '.d', '-x', 'c', '-c', '-', '-o', obj]
        proc = subprocess.run(cmd, input=modern.modern_source(path), capture_output=True, text=True,
                              env=port_build.environment(), encoding='utf-8', errors='replace')
    if proc.returncode != 0:
        if os.path.exists(obj):
            os.remove(obj)
        return path, None, True, proc.stderr
    return path, obj, True, proc.stderr


def section_names(target, obj):
    """The names of an object's sections."""
    proc = run([target.objdump, '-h', obj])
    return re.findall(r'^\s*\d+\s+(\S+)', proc.stdout, re.M)


def write_blob_script(target, objects, path):
    """The script that links the game and the port into one object, each
    kind of variable's sections that are named for a variable of the
    PlayStation program in its order (tools/port_build.py does the same for
    the whole program)."""
    playstation = port_build.playstation_addresses()
    found = {kind: set() for kind in target.kinds}
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for names in pool.map(lambda obj: section_names(target, obj), objects):
            for name in names:
                for kind, here in target.kinds.items():
                    variable = name[len(here) + 1:]
                    if name.startswith(here + target.dot) and variable in playstation:
                        found[kind].add(variable)
    words = {'under': target.under, 'dot': target.dot, 'rodata_name': target.kinds['.rodata'],
             # an object, not a program: MinGW's linker takes a script of its
             # own for the second unless it is told
             'format': 'OUTPUT_FORMAT(pe-i386)\n' if target.windows else ''}
    for kind, here in target.kinds.items():
        ordered = sorted(found[kind], key=lambda name: playstation[name])
        words[kind[1:]] = '\n'.join('        *(%s%s%s)' % (here, target.dot, name) for name in ordered)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(BLOB_LD_TEXT % words)


def symbols_of(target, path):
    """[(address, letter, size, name)] of a program or an object, by nm: a
    C name without the underscore Windows puts before it. A Windows
    program's names have no sizes: each is given the room to the next."""
    proc = run([target.nm, '-n', '-S', path])
    rows = []
    for line in proc.stdout.split('\n'):
        m = re.match(r'^([0-9a-f]+) (?:([0-9a-f]+) )?(\w) (\S+)$', line)
        if not m:
            continue
        name = m.group(4)
        if target.under and name.startswith(target.under):
            name = name[len(target.under):]
        rows.append([int(m.group(1), 16), m.group(3), int(m.group(2), 16) if m.group(2) else None, name])
    for row, after in zip(rows, rows[1:] + [None]):
        if row[2] is None:
            row[2] = after[0] - row[0] if after and after[1].lower() == row[1].lower() else 0
    return [tuple(row) for row in rows]


def undefined_of(target, obj):
    proc = run([target.nm, '-u', obj])
    return sorted(set(re.findall(r'\bU (\S+)', proc.stdout)))


def link_blob(target, objects):
    """Everything of the game and the port as one object whose names are
    its own but the port's. Returns its path, or None."""
    script = target.dir + '/game.ld'
    whole = target.dir + '/game_whole.o'
    blob = target.dir + '/game.o'
    write_blob_script(target, objects, script)
    listing = target.dir + '/objects.txt'
    with open(listing, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(objects) + '\n')
    proc = run([target.cc] + ([] if target.windows else ['-m32']) + ['-nostdlib', '-r', '-Wl,-T,' + script,
                                                                    '-o', whole, '@' + listing])
    if proc.returncode != 0:
        print('\n'.join('    ' + line for line in proc.stderr.split('\n')[:30]))
        return None
    proc = run([target.objcopy, '-w', '--keep-global-symbol=%sport_*' % target.under,
                '--keep-global-symbol=%sg_port_*' % target.under, whole, blob])
    if proc.returncode != 0:
        print('\n'.join('    ' + line for line in proc.stderr.split('\n')[:30]))
        return None
    # what it still asks for: the host's functions, the two ends of its own
    # variables (given at the link, below), and on Windows what the compiler
    # and the linker themselves call
    allowed = re.compile(r'^_?(port_Host\w+|g_port_data_(start|end)|_chkstk_ms|__main|_alloca|_pei386_runtime_relocator)$')
    strangers = [name for name in undefined_of(target, blob) if not allowed.match(name)]
    if strangers:
        print("port-sdl: the game's object asks for names that are not the host's: %s" % ' '.join(strangers))
        return None
    return blob


def write_names(target, program, names_path):
    """The program's names, as tools/port_build.py writes them."""
    rows = symbols_of(target, program)
    symbols = {}
    for address, letter, size, name in rows:
        if letter.isupper() or name not in symbols:
            symbols[name] = address
    with open(names_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# The names of %s, for the program itself: written by tools/port_sdl_build.py.\n' % program)
        for line in port_build.state_lines(symbols):
            f.write(line + '\n')
        for address, letter, size, name in sorted(set(rows)):
            if letter.lower() in 'tdbr' and re.fullmatch(r'\w+', name):
                f.write('sym %08x %s %d %s\n' % (address, 'f' if letter.lower() == 't' else 'o', size, name))
    return symbols, len(rows)


def check_layout(target, program):
    """As tools/port_build.py counts them: the PlayStation program's
    neighbours that are as far apart here."""
    playstation = port_build.playstation_addresses()
    here = {}
    for address, letter, size, name in symbols_of(target, program):
        if name in playstation and letter.lower() in 'dbr':
            here[name] = (letter.lower(), address)
    names = sorted(here, key=lambda name: playstation[name])
    same, pairs, moved = 0, 0, []
    for a, b in zip(names, names[1:]):
        if here[a][0] != here[b][0]:
            continue
        pairs += 1
        if playstation[b] - playstation[a] == here[b][1] - here[a][1]:
            same += 1
        else:
            moved.append('%-40s %-40s 0x%X on the PlayStation, 0x%X here'
                         % (a, b, playstation[b] - playstation[a], (here[b][1] - here[a][1]) & 0xFFFFFFFF))
    with open(target.dir + '/layout.txt', 'w', encoding='utf-8', newline='\n') as f:
        f.write("# Neighbours among the game's variables that are not as far apart in this program as on\n"
                '# the PlayStation (tools/port_sdl_build.py): the first, the next, and the two distances.\n')
        f.write('\n'.join(moved) + '\n')
    return same, pairs


def link_program(target, program, objects, more):
    """Links, and checks where the game's variables came to lie."""
    under = target.under
    marks = ['g_port_blob_rodata', 'g_port_blob_data', 'g_port_blob_bss']
    start = 'MIN(MIN(%s%s, %s%s), %s%s)' % (under, marks[0], under, marks[1], under, marks[2])
    end = 'MAX(MAX(%s%s_end, %s%s_end), %s%s_end)' % (under, marks[0], under, marks[1], under, marks[2])
    # the two ends of the game's variables, for the port (port_platform.h):
    # a script given beside the objects adds to the linker's own
    ends = target.dir + '/ends.ld'
    with open(ends, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Written by tools/port_sdl_build.py. */\n%sg_port_data_start = %s;\n%sg_port_data_end = %s;\n'
                % (under, start, under, end))
    proc = run([target.cc, '-o', program] + objects + [ends] + more)
    if proc.returncode != 0:
        print('\n'.join('    ' + line for line in proc.stderr.split('\n')[:40]))
        print('FAILED: port-sdl: %s does not link' % program)
        return False
    symbols, count = write_names(target, program, program + '.sym')
    first, last = symbols.get('g_port_data_start'), symbols.get('g_port_data_end')
    if first is None or last is None or not PROGRAM_BASE <= first < last <= PROGRAM_END:
        print("FAILED: port-sdl: %s: the game's variables are at 0x%X to 0x%X, and have to be inside 0x%X to 0x%X "
              '(their low 24 bits must be no other memory\'s)' % (program, first or 0, last or 0, PROGRAM_BASE, PROGRAM_END))
        return False
    same, pairs = check_layout(target, program)
    print("port-sdl: %s: %d names; its variables at 0x%X to 0x%X; %d of the game's %d pairs of neighbouring "
          'variables as far apart as on the PlayStation (the others: %s/layout.txt)'
          % (program, count, first, last, same, pairs, target.dir))
    return True


def main():
    os.chdir(ROOT)
    args = sys.argv[1:]
    target = Target('--windows' in args)
    problem = check_host(target)
    if problem:
        print('port-sdl: %s' % problem)
        return 1
    if '--host' in args:
        print('port-sdl: this host can build it (%s%s)' % (target.cc, ', SDL 2 in ' + target.sdl if target.sdl else ''))
        return 0
    os.makedirs(target.obj_dir, exist_ok=True)
    # the headers the build writes go into this build's own folder; the
    # compiler's temporary files where tools/port_build.py has them
    host_tests.OUT_DIR = OUT_DIR
    os.makedirs(port_build.OUT_DIR, exist_ok=True)
    host_tests.write_headers(True)

    flags = port_build.GAME_FLAGS + host_tests.include_flags(True) + modern.DEFS + host_tests.REBUSHIDO_DEFS
    flags += ['-DRB_GIT_HASH="%s"' % port_build.git_hash()]
    stamp = max(os.path.getmtime(p) for p in (__file__, port_build.__file__, 'tests/run.py', 'tools/modern/check.py',
                                              'tools/port_gte_macros.py', modern.SHIM))
    hash_file = target.dir + '/git_hash.txt'
    old = open(hash_file).read() if os.path.exists(hash_file) else ''
    if old != port_build.git_hash():
        with open(hash_file, 'w', newline='\n') as f:
            f.write(port_build.git_hash())
        version = target.object_of('src/rebushido/version.c')
        if os.path.exists(version):
            os.remove(version)

    jobs = [(target, path, 'port' if own else 'game', flags, stamp)
            for path, own in port_build.sources() if path not in OTHER_HOSTS]
    jobs += [(target, WINDOW_DIR + f, 'port', flags, stamp) for f in WINDOW_PLAIN]
    jobs += [(target, path, 'hosted', flags, stamp) for path in target.hosted]
    game, hosted, failed, built = [], [], 0, 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for job, (path, obj, compiled, messages) in zip(jobs, pool.map(compile_one, jobs)):
            built += compiled
            if obj is None:
                failed += 1
                print('port-sdl: %s does not compile:' % path)
                print('\n'.join('    ' + line for line in messages.strip().split('\n')[:40]))
            elif job[2] == 'hosted':
                hosted.append(obj)
            else:
                game.append(obj)
    if failed:
        print('FAILED: port-sdl: %d files do not compile' % failed)
        return 1

    blob = link_blob(target, game)
    if blob is None:
        print("FAILED: port-sdl: the game's object does not link")
        return 1
    if not link_program(target, target.program, [blob] + hosted, target.link_flags()):
        return 1
    made = [target.program]
    if target.windows:
        shutil.copyfile(target.sdl + '/bin/SDL2.dll', OUT_DIR + '/SDL2.dll')
        made.append(OUT_DIR + '/SDL2.dll')
        # the program without a window, a C library or SDL: kernel32 alone,
        # started at the host file's own entry
        plain = target.dir + '/host_windows_plain.o'
        proc = run([target.cc] + [f for f in HOSTED_FLAGS if not f.startswith('-D_FILE')]
                   + ['-DPORT_NO_C_RUNTIME', '-c', 'src/port/host_windows.c', '-o', plain])
        if proc.returncode != 0:
            print('\n'.join('    ' + line for line in proc.stderr.split('\n')[:40]))
            print('FAILED: port-sdl: src/port/host_windows.c does not compile for the program without a C library')
            return 1
        headless = OUT_DIR + '/rebushido-headless.exe'
        if not link_program(target, headless, [blob, plain],
                            ['-nostdlib', '-Wl,-e,_port_WindowsStart', '-lkernel32', '-lgcc',
                             '-Wl,--image-base=0x%X' % PROGRAM_BASE, '-Wl,--large-address-aware',
                             '-Wl,--disable-dynamicbase']):
            return 1
        made.append(headless)
    print('OK: port-sdl: %s: %d files (%d compiled now)' % (', '.join(made), len(game) + len(hosted), built))
    return 0


if __name__ == '__main__':
    sys.exit(main())
