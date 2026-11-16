#!/usr/bin/env python3
"""Build the native port with a window (docs/PORT.md, "The window").

    python3 tools/port_sdl_build.py             for this host: build/port-sdl/linux/rebushido
    python3 tools/port_sdl_build.py --windows   for Windows, with MinGW: build/port-sdl/windows/
                                                rebushido.exe, SDL2.dll beside it, and
                                                rebushido-headless.exe
    python3 tools/port_sdl_build.py --bits N    the program of 64-bit pointers, or of 32-bit ones
                                                (PORT_BITS in the environment says the same; 64 if
                                                neither does). The other size is built beside it,
                                                into build/port-sdl/linux32/ or windows32/
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

  Linux    64 bits: gcc and libsdl2-dev. 32 bits: gcc with -m32, a 32-bit C
           library and 32-bit SDL 2 as well. On Ubuntu, for the 32-bit one:
           sudo dpkg --add-architecture i386 && sudo apt update
           sudo apt install lib32gcc-13-dev libsdl2-dev:i386 libsdl2-dev
           (NOT gcc-multilib: it takes the MIPS cross compiler away.)
  Windows  x86_64-w64-mingw32-gcc for 64 bits and i686-w64-mingw32-gcc for 32
           (apt install gcc-mingw-w64-x86-64 gcc-mingw-w64-i686), and SDL 2's
           development package for MinGW, unpacked: the folder that holds
           x86_64-w64-mingw32/ and i686-w64-mingw32/ is named by SDL2_MINGW,
           or is tools/sdl2-mingw/SDL2-*/ or build/sdl2-mingw/SDL2-*/
           (BUILD.md). Each size needs its own half of it.
"""
import concurrent.futures
import glob
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_build  # noqa: E402  (tools/port_build.py)
import port_layout  # noqa: E402
from port_build import host_tests, modern  # noqa: E402

OUT_DIR = 'build/port-sdl'
WINDOW_DIR = 'src/port/window/'
# The window's arithmetic: compiled as the port's own files, and tested on
# every push (tests/port/window.c).
WINDOW_PLAIN = ['scale.c', 'ring.c', 'pace.c', 'keymap.c', 'logname.c', 'fps.c']
# What reads the game's variables out for the session's log: the port's own
# as well, and not in that test, which has no game.
WINDOW_GAME = ['gamestate.c']
# What talks to SDL and to the C library: compiled as any program is.
WINDOW_HOSTED = ['sdl_main.c', 'sdl_window.c', 'sdl_log.c']
# The remastered renderer's backend, which talks to OpenGL and to a C
# library and so is compiled the same way (docs/PORT.md, "Remastered in
# the window"). It calls GL only through pointers the window looks up with
# SDL_GL_GetProcAddress, so nothing of GL is linked to.
GL_DIR = 'src/port/gl/'
GL_HOSTED = ['gl_draw.c', 'gl_shadow.c', 'gl_stage.c', 'gl_tween.c']
# The hosts of the build without a window: not this one's on Linux, where
# the C library's is host_posix.c; on Windows the one file is both builds'.
OTHER_HOSTS = ('src/port/host_linux.c', 'src/port/host_windows.c')
# (a path cut short by snprintf is what snprintf is used for)
HOSTED_FLAGS = ['-O2', '-g', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-Wno-format-truncation', '-D_FILE_OFFSET_BITS=64',
                '-Iinclude', '-I' + WINDOW_DIR.rstrip('/'), '-I' + GL_DIR.rstrip('/')]
# Where the program is linked, so that the low 24 bits of its variables'
# addresses are no other memory's (src/port/gpu.c, port_AddLinkRegion).
# They have to end below PROGRAM_END. The build without a window is
# linked 4 MB up (tools/port_build.py) and has room to spare there; this
# one carries the window, SDL and the remastered renderer's two tables as
# well, so it begins 2 MB lower and has six megabytes. The low 24 bits
# then run from 0x200000, where the console's 2 MB of RAM end, to
# 0x800000, where the scratchpad is: no other region of the port's is in
# between (include/port_platform.h).
PROGRAM_BASE = 0x08200000
PROGRAM_END = 0x08800000
SDL_MINGW_PLACES = ['tools/sdl2-mingw/SDL2-*', 'build/sdl2-mingw/SDL2-*']
# A variable's own section in MinGW's assembly, and the boundary the
# compiler asks for just after one: `.section .data$name,"w"` and
# `.align 16`.
SECTION_LINE = re.compile(r'^(\t\.section\t\.(?:data|rdata|bss)\$\w+,"[a-z]+)"', re.M)
SECTION_ALIGN = re.compile(r'^(\t\.section\t\.(?:data|rdata|bss)\$\w+,"[a-z]+"0\n)[ \t]*\.align[ \t]+\d+\n',
                           re.M)
# A Windows object's section header: its name, how long it is, and the
# flags, which hold the boundary it is to begin on.
COFF_SECTION_BYTES = 40
COFF_NAME_AT, COFF_VIRTUAL_SIZE_AT, COFF_SIZE_AT, COFF_FLAGS_AT = 0, 8, 16, 36
# The flags' bits 20 to 23: 1 for a byte, 2 for two bytes, and so on by
# doubling; 0 for a section that says nothing.
COFF_ALIGN_SHIFT = 20
COFF_ALIGN_MASK = 0xF << COFF_ALIGN_SHIFT
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
    """What differs between the programs: the system, and how wide a
    pointer is. Each has a folder of its own, and the size that is not
    the default has its number after the system's name.
    """

    def __init__(self, windows, bits):
        self.windows = windows
        self.bits = bits
        self.size_flag = '-m%d' % bits
        self.name = (('windows' if windows else 'linux')
                     + ('' if bits == port_build.DEFAULT_BITS else str(bits)))
        self.dir = '%s/%s' % (OUT_DIR, self.name)
        self.obj_dir = self.dir + '/obj'
        if windows:
            prefix = ('x86_64' if bits == 64 else 'i686') + '-w64-mingw32-'
            # the one that needs no thread library beside the program
            self.cc = shutil.which(prefix + 'gcc-win32') and prefix + 'gcc-win32' or prefix + 'gcc'
            self.objcopy, self.nm, self.objdump = prefix + 'objcopy', prefix + 'nm', prefix + 'objdump'
            self.program = self.dir + '/rebushido.exe'
            # a C name has an underscore before it in a 32-bit Windows
            # object and none in a 64-bit one
            self.under = '_' if bits == 32 else ''
            # a variable's section is `.data$name`; constants are `.rdata`
            self.kinds = {'.rodata': '.rdata', '.data': '.data', '.bss': '.bss'}
            self.dot = '$'
            # the game's bitfields as on every other host, not Microsoft's
            # way; and no note of the compiler's own name, which the
            # 64-bit compiler puts in a shared section (.rdata$zzz) that
            # cannot survive the link into one object
            self.game_flags = ['-mno-ms-bitfields', '-fno-ident']
            if bits == 64:
                # The program is one fixed image below 2 GB and imports
                # no variable from a library, so a variable of another
                # file is reached by its address. Without this the 64-bit
                # compiler reaches each through a shared 8-byte slot
                # (.rdata$.refptr.NAME), and those slots cannot survive
                # the link into one object either.
                self.game_flags.append('-mcmodel=small')
            self.sdl = self.find_sdl(bits)
            self.sdl_flags = ['-I%s/include/SDL2' % self.sdl] if self.sdl else []
            self.hosted = ([WINDOW_DIR + f for f in WINDOW_HOSTED]
                           + [GL_DIR + f for f in GL_HOSTED] + ['src/port/host_windows.c'])
        else:
            self.cc, self.objcopy, self.nm, self.objdump = 'gcc', 'objcopy', 'nm', 'objdump'
            self.program = self.dir + '/rebushido'
            self.under = ''
            self.kinds = {'.rodata': '.rodata', '.data': '.data', '.bss': '.bss'}
            self.dot = '.'
            self.game_flags = []
            self.sdl = None
            self.sdl_flags = ['-I/usr/include/SDL2', '-D_REENTRANT']
            self.hosted = ([WINDOW_DIR + f for f in WINDOW_HOSTED]
                           + [GL_DIR + f for f in GL_HOSTED] + [WINDOW_DIR + 'host_posix.c'])

    @staticmethod
    def find_sdl(bits):
        """The half of SDL's MinGW package for this size, or None."""
        half = ('x86_64' if bits == 64 else 'i686') + '-w64-mingw32'
        places = [os.environ['SDL2_MINGW']] if os.environ.get('SDL2_MINGW') else []
        for pattern in SDL_MINGW_PLACES:
            places += sorted(glob.glob(pattern), reverse=True)
        for place in places:
            if os.path.isfile('%s/%s/include/SDL2/SDL.h' % (place, half)):
                return '%s/%s' % (place.rstrip('/'), half)
        return None

    def object_of(self, path):
        return '%s/%s.o' % (self.obj_dir, path[:-2].replace('/', '_'))

    def windows_link_flags(self):
        """Where a Windows program is put, and what it may reach: a
        32-bit one has to be told that it may use addresses from 2 GB up,
        where the PlayStation's RAM is; a 64-bit one always may."""
        flags = ['-Wl,--image-base=0x%X' % PROGRAM_BASE, '-Wl,--disable-dynamicbase']
        if self.bits == 32:
            flags.append('-Wl,--large-address-aware')
        return flags

    def link_flags(self):
        if self.windows:
            return (['-L%s/lib' % self.sdl, '-lSDL2', '-lcomdlg32', '-static-libgcc']
                    + self.windows_link_flags())
        return [self.size_flag, '-no-pie', '-lSDL2', '-Wl,-Ttext-segment=0x%X' % PROGRAM_BASE]


def run(cmd, **more):
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=port_build.environment(), **more)


def check_host(target):
    """None, or why this host cannot build the program."""
    os.makedirs(target.dir, exist_ok=True)
    if not shutil.which(target.cc):
        return 'no %s here%s' % (target.cc, ' (apt install gcc-mingw-w64-i686)' if target.windows else '')
    if target.windows and not target.sdl:
        return ("SDL 2's %s half for MinGW is not here: unpack SDL2-devel-2.x.y-mingw.tar.gz "
                '(github.com/libsdl-org/SDL/releases) and name its SDL2-2.x.y folder with SDL2_MINGW, or put it '
                'in tools/sdl2-mingw/ (BUILD.md)'
                % (('x86_64' if target.bits == 64 else 'i686') + '-w64-mingw32'))
    probe = target.dir + '/sdl_probe' + ('.exe' if target.windows else '')
    proc = run([target.cc] + (['-DSDL_MAIN_HANDLED'] if target.windows else [target.size_flag]) + target.sdl_flags
               + ['-x', 'c', '-', '-o', probe] + [f for f in target.link_flags() if not f.startswith('-Wl,-T')],
               input=SDL_PROBE)
    if proc.returncode != 0:
        return ('%s cannot make a %d-bit program with SDL 2: %s%s'
                % (target.cc, target.bits, (proc.stderr.strip().split('\n') or ['?'])[-1],
                   '' if target.windows or target.bits == 64 else
                   '\nport-sdl: on Ubuntu: sudo dpkg --add-architecture i386 && sudo apt update && sudo apt install '
                   'lib32gcc-13-dev libsdl2-dev:i386 libsdl2-dev   (not gcc-multilib: it removes the MIPS compiler)'))
    os.remove(probe)
    return None


def compile_for_windows(target, cmd, source, obj):
    """A file of the game or the port for Windows, in two steps, with the
    compiler's assembly put right between them. A Windows object's
    sections begin on a multiple of four bytes and are that long, unless
    the section's line says otherwise: so two neighbouring variables of two
    bytes, each in a section of its own, would be four bytes apart, where
    the PlayStation has them two apart and the game reads them as one
    array (g_fight_blow_dir_x and the two after it). A `0` after a
    variable's section's flags starts it at one byte, and the `.align`
    the compiler writes for the variable then makes it what the variable
    needs. The boundary the compiler asked for is taken out with it and
    given again in the object (cap_alignment_coff), by the rule
    tools/port_build.py follows: the assembler would pad the section out
    to the boundary as well, and then two neighbours of two bytes would
    be four apart again."""
    proc = subprocess.run(cmd + ['-S', '-', '-o', '-'], input=source, capture_output=True, text=True,
                          env=port_build.environment(), encoding='utf-8', errors='replace')
    if proc.returncode != 0:
        return proc
    assembly, count = SECTION_LINE.subn(r'\g<1>0"', proc.stdout)
    assembly = SECTION_ALIGN.sub(r'\g<1>', assembly)
    assembled = subprocess.run([target.cc, '-x', 'assembler', '-c', '-', '-o', obj], input=assembly,
                               capture_output=True, text=True, env=port_build.environment(), encoding='utf-8',
                               errors='replace')
    assembled.stderr = proc.stderr + assembled.stderr
    if assembled.returncode == 0:
        cap_alignment_coff(obj)
    return assembled


def cap_alignment_coff(obj):
    """Gives every variable's section of a Windows object the boundary the
    PlayStation's compiler gives the variable, as tools/port_build.py's
    cap_alignment does for an ELF object: a word's for an array or a
    structure, and for anything else its own size's, at most a word's. A
    variable is in a section of its own, named for its kind and itself."""
    with open(obj, 'rb') as f:
        data = bytearray(f.read())
    sections, symbols_at, symbol_count, optional = struct.unpack_from('<H4xIIH', data, 2)
    strings_at = symbols_at + 18 * symbol_count
    first = 20 + optional
    changed = False
    for i in range(sections):
        at = first + i * COFF_SECTION_BYTES
        name = data[at + COFF_NAME_AT:at + COFF_NAME_AT + 8]
        if name[:1] == b'/':
            # a name of more than eight bytes is kept in the table of
            # names, and the field is a `/` and where it is there
            offset = name[1:].split(b'\0')[0].decode('latin-1')
            if not offset.isdigit():
                continue
            end = data.index(b'\0', strings_at + int(offset))
            name = data[strings_at + int(offset):end]
        else:
            name = name.split(b'\0')[0]
        text = name.decode('latin-1')
        if '$' not in text or text.split('$')[0] not in ('.data', '.rdata', '.bss'):
            continue
        length = max(struct.unpack_from('<I', data, at + COFF_SIZE_AT)[0],
                     struct.unpack_from('<I', data, at + COFF_VIRTUAL_SIZE_AT)[0])
        flags, = struct.unpack_from('<I', data, at + COFF_FLAGS_AT)
        bits = (flags & COFF_ALIGN_MASK) >> COFF_ALIGN_SHIFT
        boundary = 1 << (bits - 1) if bits else 1
        wanted = port_build.WORD if boundary > port_build.WORD or length > boundary else boundary
        if wanted != boundary:
            flags = (flags & ~COFF_ALIGN_MASK) | ((wanted.bit_length()) << COFF_ALIGN_SHIFT)
            struct.pack_into('<I', data, at + COFF_FLAGS_AT, flags)
            changed = True
    if changed:
        with open(obj, 'wb') as f:
            f.write(data)


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
        cmd = [target.cc] + ([] if target.windows else [target.size_flag]) + HOSTED_FLAGS + target.sdl_flags
        cmd += ['-MD', '-MF', obj[:-2] + '.d', '-c', path, '-o', obj]
        proc = run(cmd)
    else:
        if kind == 'port':
            flags = [f for f in flags if f != '-w'] + port_build.PORT_WARNINGS + ['-I' + WINDOW_DIR.rstrip('/')]
        else:
            flags = flags + [port_build.INSTRUMENT]
        cmd = [target.cc] + flags + target.game_flags + ['-MD', '-MF', obj[:-2] + '.d', '-x', 'c']
        if target.windows:
            proc = compile_for_windows(target, cmd, modern.modern_source(path), obj)
        else:
            proc = subprocess.run(cmd + ['-c', '-', '-o', obj], input=modern.modern_source(path), capture_output=True,
                                  text=True, env=port_build.environment(), encoding='utf-8', errors='replace')
            if proc.returncode == 0:
                # the boundary the PlayStation's compiler gives each
                # variable, so that the game's neighbours stay neighbours
                port_build.cap_alignment(obj)
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
             'format': ('OUTPUT_FORMAT(%s)\n' % ('pe-x86-64' if target.bits == 64 else 'pe-i386')
                        if target.windows else '')}
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
    proc = run([target.cc] + ([] if target.windows else [target.size_flag])
               + ['-nostdlib', '-r', '-Wl,-T,' + script, '-o', whole, '@' + listing])
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
    allowed = re.compile(r'^_{0,3}(port_Host\w+|g_port_data_(start|end)|chkstk_ms|main|alloca'
                         r'|pei386_runtime_relocator)$')
    strangers = [name for name in undefined_of(target, blob) if not allowed.match(name)]
    if strangers:
        print("port-sdl: the game's object asks for names that are not the host's: %s" % ' '.join(strangers))
        return None
    return blob


def console_layouts(target, program):
    """How each variable the program lays out otherwise than the
    PlayStation lies against the PlayStation's (tools/port_layout.py,
    console_runs); {} for a 32-bit program, which lays them out as the
    PlayStation does. The debug information is read with the target's own
    objdump: a Windows program is no ELF, and readelf reads only those."""
    if target.bits != 64:
        return {}
    import port_layout
    return port_layout.console_runs(program, reader=(target.objdump, '--dwarf=info'))


def write_names(target, program, names_path):
    """The program's names, as tools/port_build.py writes them, and for a
    64-bit program how each variable that grew lies against the
    PlayStation's (port_layout.console_runs)."""
    rows = symbols_of(target, program)
    symbols = {}
    for address, letter, size, name in rows:
        if letter.isupper() or name not in symbols:
            symbols[name] = address
    layouts = console_layouts(target, program)
    with open(names_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# The names of %s, for the program itself: written by tools/port_sdl_build.py.\n' % program)
        for line in port_build.state_lines(symbols, layouts):
            f.write(line + '\n')
        for name in sorted(layouts):
            f.write('layout %s %d\n' % (name, layouts[name][0]))
            for one in layouts[name][1]:
                f.write('run %d %d %d\n' % one)
        for address, letter, size, name in sorted(set(rows)):
            if letter.lower() in 'tdbr' and re.fullmatch(r'\w+', name):
                f.write('sym %08x %s %d %s\n' % (address, 'f' if letter.lower() == 't' else 'o', size, name))
    return symbols, len(rows), set(layouts)


def check_layout(target, program, grown=()):
    """As tools/port_build.py counts them: the PlayStation program's
    neighbours that are as far apart here. `grown` names the variables a
    64-bit program lays out with wider pointers."""
    playstation = port_build.playstation_addresses()
    here = {}
    for address, letter, size, name in symbols_of(target, program):
        if name in playstation and letter.lower() in 'dbr':
            here[name] = (letter.lower(), address)
    names = sorted(here, key=lambda name: playstation[name])
    same, pairs, wider, moved = 0, 0, 0, []
    for a, b in zip(names, names[1:]):
        if here[a][0] != here[b][0]:
            continue
        pairs += 1
        if playstation[b] - playstation[a] == here[b][1] - here[a][1]:
            same += 1
        else:
            wider += a in grown
            moved.append('%-40s %-40s 0x%X on the PlayStation, 0x%X here%s'
                         % (a, b, playstation[b] - playstation[a], (here[b][1] - here[a][1]) & 0xFFFFFFFF,
                            '  (the first has pointers)' if a in grown else ''))
    with open(target.dir + '/layout.txt', 'w', encoding='utf-8', newline='\n') as f:
        f.write("# Neighbours among the game's variables that are not as far apart in this program as on\n"
                '# the PlayStation (tools/port_sdl_build.py): the first, the next, and the two distances.\n')
        f.write('\n'.join(moved) + '\n')
    return same, pairs, wider


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
    symbols, count, grown = write_names(target, program, program + '.sym')
    first, last = symbols.get('g_port_data_start'), symbols.get('g_port_data_end')
    if first is None or last is None or not PROGRAM_BASE <= first < last <= PROGRAM_END:
        print("FAILED: port-sdl: %s: the game's variables are at 0x%X to 0x%X, and have to be inside 0x%X to 0x%X "
              '(their low 24 bits must be no other memory\'s)' % (program, first or 0, last or 0, PROGRAM_BASE, PROGRAM_END))
        return False
    same, pairs, wider = check_layout(target, program, grown)
    print("port-sdl: %s: %d names; its variables at 0x%X to 0x%X; %d of the game's %d pairs of neighbouring "
          'variables as far apart as on the PlayStation (the others: %s/layout.txt%s)'
          % (program, count, first, last, same, pairs, target.dir,
             '; %d of them follow a variable whose pointers are wider here' % wider if target.bits == 64 else ''))
    return True


def main():
    os.chdir(ROOT)
    args = sys.argv[1:]
    bits = port_build.bits_of(args)
    if bits is None:
        return 1
    port_build.set_bits(bits)
    target = Target('--windows' in args, bits)
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
    flags += port_build.MOVOVL_INCLUDES
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

    jobs = [(target, path, 'port' if own else 'game', flags + mine, stamp)
            for path, own, mine in port_build.sources() if path not in OTHER_HOSTS]
    jobs += [(target, WINDOW_DIR + f, 'port', flags, stamp) for f in WINDOW_PLAIN + WINDOW_GAME]
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
        shutil.copyfile(target.sdl + '/bin/SDL2.dll', target.dir + '/SDL2.dll')
        made.append(target.dir + '/SDL2.dll')
        # the program without a window, a C library or SDL: kernel32 alone,
        # started at the host file's own entry
        plain = target.dir + '/host_windows_plain.o'
        proc = run([target.cc] + [f for f in HOSTED_FLAGS if not f.startswith('-D_FILE')]
                   + ['-DPORT_NO_C_RUNTIME', '-c', 'src/port/host_windows.c', '-o', plain])
        if proc.returncode != 0:
            print('\n'.join('    ' + line for line in proc.stderr.split('\n')[:40]))
            print('FAILED: port-sdl: src/port/host_windows.c does not compile for the program without a C library')
            return 1
        headless = target.dir + '/rebushido-headless.exe'
        if not link_program(target, headless, [blob, plain],
                            ['-nostdlib', '-Wl,-e,' + target.under + 'port_WindowsStart', '-lkernel32', '-lgcc']
                            + target.windows_link_flags()):
            return 1
        made.append(headless)
    print('OK: port-sdl: %s: %d files (%d compiled now)' % (', '.join(made), len(game) + len(hosted), built))
    return 0


if __name__ == '__main__':
    sys.exit(main())
