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
    python3 tools/port_sdl_build.py --padded [--windows] and beside the program two whose window
                                                differs (rebushido-pad-small, -pad-large), for
                                                tools/port_replay.py --window

`make port-sdl` and `make port-sdl-windows` run it (`wsl bash tools/build.sh
port-sdl`). It is the same program as `make port` builds (tools/
port_build.py), from the same files compiled the same way, and then:

  - the four files of src/port/window/ that are plain arithmetic (where the
    picture goes, the sound's queue, the clock, the keys), compiled as the
    port's own files are;
  - the files that talk to SDL 2 and to a C library (src/port/window/
    sdl_main.c, sdl_window.c, and host_posix.c or src/port/host_windows.c),
    compiled as any program is;
  - SDL 2 and a C library, which `make port` has neither of;
  - the port's typeface, Shippori Mincho in three weights
    (src/port/fonts/), as C arrays written into the build's folder, and
    the English top menu
    (src/port/mods/english/), copied into mods/ beside the program
    (docs/MODS.md, "The English menu").

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
import hashlib
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
WINDOW_PLAIN = ['scale.c', 'ring.c', 'pace.c', 'keymap.c', 'logname.c', 'fps.c', 'settext.c', 'choices.c', 'cpus.c']
# What reads the game's variables out for the session's log: the port's own
# as well, and not in that test, which has no game.
WINDOW_GAME = ['gamestate.c']
# What talks to SDL and to the C library: compiled as any program is.
WINDOW_HOSTED = ['sdl_main.c', 'sdl_window.c', 'sdl_log.c', 'sdl_menu.c', 'sdl_net.c', 'sdl_report.c', 'menu.c', 'font.c',
                 'stage_page.c', 'match_page.c', 'training_page.c', 'top_page.c', 'page_look.c', 'menu_online.c',
                 'menu_replays.c', 'menu_stats.c', 'sdl_replay.c', 'sdl_export.c', 'film.c', 'film_jpeg.c', 'film_mf.c',
                 'film_gif.c', 'sdl_theatre.c',
                 'sampler.c', 'warm.c', 'portmap.c', 'cpu_prefer.c',
                 'prepare.c', 'update.c', 'update_core.c', 'update_apply.c', 'online_pages.c']
# (Monocypher, the updater's Ed25519, is in every program: tools/
# port_build.py's VENDOR_FILES)
# The release key the updater holds every release's manifest to (docs/
# PORT.md, "Updates"): its public half, the hex of 32 bytes, or `none`
RELEASE_KEY = 'config/release_public_key.txt'
# The port's typeface, each weight put into the program as a C array of its
# bytes (src/port/window/font.c; docs/PORT.md, "The port's lettering"), and
# the mods the program ships, copied into mods/ beside it.
FONTS = [('src/port/fonts/ShipporiMincho-Regular.ttf', 'g_port_font_text'),
         ('src/port/fonts/ShipporiMincho-Bold.ttf', 'g_port_font_bold'),
         ('src/port/fonts/ShipporiMincho-ExtraBold.ttf', 'g_port_font_title')]
SHIPPED_MODS = 'src/port/mods'
# the sample arenas' sources (tools/arena_compile.py)
SHIPPED_ARENAS = 'tools/arenas'

# The remastered renderer's backend, which talks to OpenGL and to a C
# library and so is compiled the same way (docs/PORT.md, "Remastered in
# the window"). It calls GL only through pointers the window looks up with
# SDL_GL_GetProcAddress, so nothing of GL is linked to.
GL_DIR = 'src/port/gl/'
GL_HOSTED = ['gl_bench.c', 'gl_cloth.c', 'gl_draw.c', 'gl_dressing.c', 'gl_fighter.c', 'gl_fighter_card.c', 'gl_fighter_file.c', 'gl_joints.c', 'gl_light.c', 'gl_light_list.c', 'gl_lights.c', 'gl_load.c', 'gl_movie.c', 'gl_movie_pack.c', 'gl_pause.c', 'gl_post.c', 'gl_record.c', 'gl_replace.c', 'gl_scene.c',
             'gl_see.c', 'gl_shadow.c', 'gl_sky.c', 'gl_stage.c', 'gl_sun.c', 'gl_time.c', 'gl_tween.c']
# The hosts of the build without a window: not this one's on Linux, where
# the C library's is host_posix.c; on Windows the one file is both builds'.
OTHER_HOSTS = ('src/port/host_linux.c', 'src/port/host_windows.c')
# (a path cut short by snprintf is what snprintf is used for)
HOSTED_FLAGS = ['-O2', '-g', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-Wno-format-truncation', '-D_FILE_OFFSET_BITS=64',
                '-Iinclude', '-I' + WINDOW_DIR.rstrip('/'), '-I' + GL_DIR.rstrip('/')]
# The players' program is optimised further than the headless one the
# checks and the play tests run (tools/port_build.py, -O1): the game and
# the port's own files at -O2, which was measured faster with the same
# traces (docs/PORT.md, "Where the time goes", "-O2"). PORT_OPT=N says
# otherwise, into a folder of its own.
WINDOW_OPT = '2'
# ... and the files with SDL and the C library of the 64-bit programs at
# -O3 for an x86-64 of 2013 on (Haswell: AVX2), linked whole (LTO): the
# pictures between frames, the shadows and the cloth were a fifth to a
# third quicker on the card's bench (docs/PORT.md, "Where the time goes").
# No multiply-add is fused, so their arithmetic is the same to the bit as
# without (-ffp-contract=off). None of it is the game's state.
HOSTED_FAST = ['-O3', '-march=x86-64-v3', '-ffp-contract=off', '-flto=auto']
# Where the program is linked: where the build without a window is
# (tools/port_build.py, PROGRAM_BASE), so that the game's state's two runs
# of variables have low 24 bits that no other place a primitive may be has
# (port_build.check_link_space; docs/PORT.md, "Where the program's
# variables are"). What is not the state (the window's, SDL's, the
# remastered side channel) may lie anywhere below 4 GB.
PROGRAM_BASE = port_build.PROGRAM_BASE
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
# order, kind by kind, and marks at each kind's two ends. Each kind's
# first part, between g_port_core_<kind> and g_port_core_<kind>_end, is
# the core: everything but the window's own files and the version's
# (APART), whose sections follow it. The code is one section, so that no
# part of the core (the game's `main` in .text.startup, cold code in
# .text.unlikely) is put by the program's link after the window's code
# and SDL's: a build whose window differs has its core where this one has
# it, and its sum (code_sum) is this one's (docs/PORT.md, "Replays":
# "Held to the program").
BLOB_LD_TEXT = '''/* Written by tools/port_sdl_build.py: the game and the port as one object.
 * The game's variables are in the order of the PlayStation program. */
%(format)sSECTIONS
{
    .text : {
        %(under)sg_port_core_text = .;
        EXCLUDE_FILE(%(apart)s) *(.text .text.* .text$*)
        %(under)sg_port_core_text_end = .;
        *(.text .text.* .text$*)
    }
    %(rodata_name)s : {
        %(under)sg_port_blob_rodata = .;
        %(under)sg_port_core_rodata = .;
%(rodata)s
        EXCLUDE_FILE(%(apart)s) *(%(rodata_name)s %(rodata_name)s%(dot)s*)
        %(under)sg_port_core_rodata_end = .;
        *(%(rodata_name)s %(rodata_name)s%(dot)s*)
        %(under)sg_port_blob_rodata_end = .;
    }
    .data : {
        %(under)sg_port_blob_data = .;
        %(under)sg_port_core_data = .;
        %(under)sg_port_game_data = .;
%(data)s
        %(under)sg_port_game_data_end = .;
        EXCLUDE_FILE(%(outside)s %(run)s) *(.data .data%(dot)s*)
        %(under)sg_port_run_data = .;
%(run_data)s
        %(under)sg_port_state_data_end = .;
        EXCLUDE_FILE(%(apart)s) *(.data .data%(dot)s*)
        %(under)sg_port_core_data_end = .;
        *(.data .data%(dot)s*)
        %(under)sg_port_blob_data_end = .;
    }
    .bss : {
        %(under)sg_port_blob_bss = .;
        %(under)sg_port_core_bss = .;
        %(under)sg_port_game_bss = .;
%(bss)s
        %(under)sg_port_game_bss_end = .;
        EXCLUDE_FILE(%(outside)s %(run)s) *(.bss .bss%(dot)s*) *(COMMON)
        %(under)sg_port_run_bss = .;
%(run_bss)s
        %(under)sg_port_state_bss_end = .;
        EXCLUDE_FILE(%(apart)s) *(.bss .bss%(dot)s*) EXCLUDE_FILE(%(apart)s) *(COMMON)
        %(under)sg_port_core_bss_end = .;
        *(.bss .bss%(dot)s*) *(COMMON)
        %(under)sg_port_blob_bss_end = .;
    }
}
'''
# The files of the game's object that are not its core: the window's
# arithmetic (WINDOW_PLAIN, WINDOW_GAME), and the version, whose text holds
# the commit.
APART = ['*src_port_window_*.o', '*src_rebushido_version.o']
# The hosted files' functions all in .text, after the game's object's
# (Linux: a C compiler puts `main` and cold code in sections that the link
# puts before every .text, the game's object's too).
HOSTED_IN_TEXT = ['-fno-reorder-functions', '-fno-reorder-blocks-and-partition']
# --padded: the programs whose window differs from this one's, beside it
# (rebushido-pad-small and rebushido-pad-large), and their room before the
# window's files: code, variables and constants, in bytes. A little moves
# the window's code and nothing of the game's object; much moves the
# game's object's variables and constants too.
PADDED = '%s-pad-%s'
PADS = [('small', (96, 0, 0)), ('large', (0x30000, 0x30000, 0x30000))]


class Target:
    """What differs between the programs: the system, and how wide a
    pointer is. Each has a folder of its own, and the size that is not
    the default has its number after the system's name.
    """

    def __init__(self, windows, bits):
        self.windows = windows
        self.bits = bits
        self.size_flag = '-m%d' % bits
        # PORT_OPT=N: the game optimised otherwise than the players' build
        # is (WINDOW_OPT), in a folder of its own; RB_WINDOW_FLAGS: flags
        # more for every file, to be measured, into the folder
        # RB_WINDOW_NAME names
        self.name = (('windows' if windows else 'linux')
                     + ('' if bits == port_build.DEFAULT_BITS else str(bits))
                     + ('-O' + os.environ['PORT_OPT'] if os.environ.get('PORT_OPT') else '')
                     + os.environ.get('RB_WINDOW_NAME', ''))
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
            # (-lole32 and -lmfuuid: a replay's film, by Media Foundation,
            # whose own two libraries are looked up as it starts: film_mf.c)
            return (['-L%s/lib' % self.sdl, '-lSDL2', '-lcomdlg32', '-lws2_32', '-lwinhttp', '-lole32', '-lmfuuid',
                     '-static-libgcc']
                    + self.windows_link_flags())
        # (-lm: the menu's font is drawn with the C library's arithmetic)
        return [self.size_flag, '-no-pie', '-lSDL2', '-lm', '-Wl,-Ttext-segment=0x%X' % PROGRAM_BASE]


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
        if kind == 'port' and path in port_build.VENDOR_FILES:
            pass  # as it is shipped, its warnings off (port_build.VENDOR_FLAGS)
        elif kind == 'port':
            flags = [f for f in flags if f != '-w'] + port_build.PORT_WARNINGS + ['-I' + WINDOW_DIR.rstrip('/')]
        else:
            flags = flags + [port_build.INSTRUMENT]
        cmd = [target.cc] + flags + target.game_flags + ['-MD', '-MF', obj[:-2] + '.d', '-x', 'c']
        if target.windows and path not in port_build.NATURAL_ALIGNMENT:
            proc = compile_for_windows(target, cmd, modern.modern_source(path), obj)
        else:
            proc = subprocess.run(cmd + ['-c', '-', '-o', obj], input=modern.modern_source(path), capture_output=True,
                                  text=True, env=port_build.environment(), encoding='utf-8', errors='replace')
            if proc.returncode == 0 and not target.windows and path not in port_build.NATURAL_ALIGNMENT:
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
    # the window's own files are not the game's state either
    # (config/port_state.txt)
    outside = port_build.outside_state_patterns()
    words = {'under': target.under, 'dot': target.dot, 'rodata_name': target.kinds['.rodata'], 'outside': outside,
             'apart': ' '.join(APART),
             'run': port_build.run_state_patterns(),
             'run_data': port_build.run_state_sections(['.data'], target.dot),
             'run_bss': port_build.run_state_sections(['.bss'], target.dot),
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
                '--keep-global-symbol=%sg_port_*' % target.under,
                # Monocypher's, which the updater calls (update_core.c)
                '--keep-global-symbol=%scrypto_*' % target.under, whole, blob])
    if proc.returncode != 0:
        print('\n'.join('    ' + line for line in proc.stderr.split('\n')[:30]))
        return None
    # what it still asks for: the host's functions (port_host.h), the two
    # ends of its own variables (given at the link, below), the code's sum
    # (code_sum), on Windows what the compiler and the linker themselves
    # call, and in a 32-bit program libgcc's division of 64-bit numbers
    allowed = re.compile(r'^_{0,3}(port_Host\w+|g_port_data_(start|end)|g_port_code_sum|chkstk_ms|main|alloca'
                         r'|pei386_runtime_relocator|u?divdi3|u?moddi3)$')
    strangers = [name for name in undefined_of(target, blob) if not allowed.match(name)]
    if strangers:
        print("port-sdl: the game's object asks for names that are not the host's: %s" % ' '.join(strangers))
        return None
    return blob


def code_sum(target, blob):
    """The sum of the game's object's core (BLOB_LD_TEXT): its code, its
    constants and its variables' first values, byte for byte, how large
    its zeroed variables are, and every place they name another (each
    relocation, with the name it reaches). It is taken from the object
    before the program's link, so it says nothing of where the window's
    code or SDL's lie. 64 bits of it, or None."""
    marks = {}
    for address, letter, size, name in symbols_of(target, blob):
        if name.startswith('g_port_core_'):
            marks[name[len('g_port_core_'):]] = address
    sections = {'text': '.text', 'rodata': target.kinds['.rodata'], 'data': '.data'}
    if any(kind not in marks or kind + '_end' not in marks for kind in list(sections) + ['bss']):
        print("port-sdl: the game's object has no marks of its core (g_port_core_...)")
        return None
    digest = hashlib.sha1()
    for kind, section in sections.items():
        raw = target.dir + '/core_%s.bin' % kind
        proc = run([target.objcopy, '-O', 'binary', '--only-section=' + section, blob, raw])
        if proc.returncode != 0:
            print('\n'.join('    ' + line for line in proc.stderr.split('\n')[:10]))
            return None
        with open(raw, 'rb') as f:
            data = f.read()
        os.remove(raw)
        digest.update(('%s %d\n' % (kind, marks[kind + '_end'] - marks[kind])).encode())
        digest.update(data[marks[kind]:marks[kind + '_end']])
    digest.update(('bss %d\n' % (marks['bss_end'] - marks['bss'])).encode())
    proc = run([target.objdump, '-r', blob])
    if proc.returncode != 0:
        return None
    section = None
    for line in proc.stdout.split('\n'):
        m = re.match(r'^RELOCATION RECORDS FOR \[(.*)\]:', line)
        if m:
            section = next((kind for kind, name in sections.items() if name == m.group(1)), None)
            continue
        m = re.match(r'^([0-9a-f]+)\s+(\S+)\s+(\S+)', line)
        if section and m and marks[section] <= int(m.group(1), 16) < marks[section + '_end']:
            digest.update(('%s %s\n' % (section, line.strip())).encode())
    return int.from_bytes(digest.digest()[:8], 'little')


def compile_code_sum(target, value):
    """The code's sum as a C file of the build's own, compiled outside the
    game's object (it is not part of what it sums): its object, or None."""
    path = target.dir + '/code_sum.c'
    obj = target.dir + '/code_sum.o'
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Written by tools/port_sdl_build.py: the sum of the game\'s object\'s\n'
                ' * core (port_StateBuildSum, src/port/state.c). */\n'
                'extern const unsigned long long g_port_code_sum;\n'
                'const unsigned long long g_port_code_sum = 0x%016XULL;\n' % value)
    proc = run([target.cc] + ([] if target.windows else [target.size_flag]) + ['-O2', '-c', path, '-o', obj])
    if proc.returncode != 0:
        print('\n'.join('    ' + line for line in proc.stderr.split('\n')[:10]))
        return None
    return obj


def write_pad(target, name, text, data, rodata):
    """An object of nothing but room (`text`, `data` and `rodata` bytes),
    linked before the window's files in a program made to test that a
    build whose window differs plays this one's replays (tools/
    port_replay.py, --window): its path."""
    # C, not assembly, so that the object is marked as the compiler marks
    # every other (on Linux its note of the processor's features, without
    # which the link lays out the program's calls to libraries otherwise)
    path = '%s/pad_%s.c' % (target.dir, name)
    obj = path[:-2] + '.o'
    constants = '.section .rdata,\\"dr\\"' if target.windows else '.section .rodata'
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Written by tools/port_sdl_build.py --padded */\n'
                '__asm__(".text\\n.fill %d, 1, 0xcc\\n.data\\n.fill %d, 1, 0\\n%s\\n.fill %d, 1, 0\\n.text\\n");\n'
                % (text, data, constants, rodata))
    proc = run([target.cc] + ([] if target.windows else [target.size_flag]) + ['-O2', '-c', path, '-o', obj])
    if proc.returncode != 0:
        print('\n'.join('    ' + line for line in proc.stderr.split('\n')[:10]))
        return None
    return obj


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
    if first is None or last is None or not PROGRAM_BASE <= first < last < 1 << 32:
        print("FAILED: port-sdl: %s: the game's variables are at 0x%X to 0x%X, and have to be past 0x%X and below "
              '4 GB, where the game\'s 32-bit addresses reach' % (program, first or 0, last or 0, PROGRAM_BASE))
        return False
    problems, room = port_build.check_link_space(symbols)
    if problems:
        print('\n'.join('port-sdl: %s: %s' % (program, problem) for problem in problems))
        print("FAILED: port-sdl: %s: its variables do not fit the ordering tables' link space" % program)
        return False
    print('port-sdl: %s: %s' % (program, room))
    same, pairs, wider = check_layout(target, program, grown)
    print("port-sdl: %s: %d names; its variables at 0x%X to 0x%X; %d of the game's %d pairs of neighbouring "
          'variables as far apart as on the PlayStation (the others: %s/layout.txt%s)'
          % (program, count, first, last, same, pairs, target.dir,
             '; %d of them follow a variable whose pointers are wider here' % wider if target.bits == 64 else ''))
    return True


def write_font(target):
    """The typeface's weights as a C file of the build's own, written again
    only when a weight or this script is newer: its path."""
    path = target.dir + '/menu_font.c'
    newest = max([os.path.getmtime(font) for font, _ in FONTS] + [os.path.getmtime(__file__)])
    if os.path.exists(path) and os.path.getmtime(path) >= newest:
        return path
    lines = ['/* Written by tools/port_sdl_build.py from src/port/fonts/ (SIL Open Font',
             ' * Licence, src/port/fonts/OFL-ShipporiMincho.txt): the port\'s typeface. */']
    for font, name in FONTS:
        data = open(font, 'rb').read()
        lines += ['/* %s */' % font,
                  'extern const unsigned char %s[];' % name,
                  'const unsigned char %s[%d] = {' % (name, len(data))]
        for at in range(0, len(data), 16):
            lines.append('    ' + ' '.join('%d,' % byte for byte in data[at:at + 16]))
        lines.append('};')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return path


def release_key():
    """The release key's public half as config/release_public_key.txt has
    it: 64 hex digits, or '' where the file says `none` (no key yet: the
    program then never updates itself). Ends the build on anything else."""
    words = [line.strip() for line in open(RELEASE_KEY, encoding='utf-8')
             if line.strip() and not line.strip().startswith('#')]
    if words == ['none']:
        return ''
    if len(words) != 1 or not re.fullmatch(r'[0-9a-f]{64}', words[0]):
        sys.exit('port-sdl: %s is neither `none` nor the 64 hex digits of an Ed25519 public key' % RELEASE_KEY)
    return words[0]


def write_update_key(target):
    """The release key as a C file of the build's own, written again only
    when it changed: its path."""
    path = target.dir + '/update_key.c'
    key = release_key()
    text = ('/* Written by tools/port_sdl_build.py from %s: the key every\n'
            ' * release\'s manifest is held to (src/port/window/update.c). */\n'
            'extern const char g_port_update_key[];\n'
            'const char g_port_update_key[] = "%s";\n' % (RELEASE_KEY, key))
    if not os.path.exists(path) or open(path, encoding='utf-8').read() != text:
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    if not key:
        print('port-sdl: %s says none: this program never updates itself (BUILD.md, "Publishing a release")'
              % RELEASE_KEY)
    return path


def arena_mod_text(name):
    """The mod.txt of the sample arena `name`'s folder of mods/ (tools/
    port_release.py holds a package's copy to it)."""
    return ('# made by tools/port_sdl_build.py from %s/%s\nname = arena %s\nauthor = ReBushido\n'
            % (SHIPPED_ARENAS, name, name))


def copy_mods(target):
    """The mods the program ships into mods/ beside it, each folder as it
    is in the repository and nothing else, and the sample arenas compiled:
    the names of the folders."""
    names = []
    for name in sorted(os.listdir(SHIPPED_MODS)):
        source = os.path.join(SHIPPED_MODS, name)
        if not os.path.isdir(source):
            continue
        dest = os.path.join(target.dir, 'mods', name)
        if os.path.isdir(dest):
            shutil.rmtree(dest)
        shutil.copytree(source, dest)
        names.append(name)
    # the sample arenas, compiled from their sources (docs/MODS.md,
    # "Custom arenas"): mods/arena-NAME/arenas/NAME.arena, played only
    # where the run says --arena NAME
    import arena_compile
    for name in sorted(os.listdir(SHIPPED_ARENAS)):
        source = os.path.join(SHIPPED_ARENAS, name)
        if not os.path.isfile(os.path.join(source, 'arena.txt')):
            continue
        dest = os.path.join(target.dir, 'mods', 'arena-' + name)
        if os.path.isdir(dest):
            shutil.rmtree(dest)
        os.makedirs(os.path.join(dest, 'arenas'))
        with open(os.path.join(dest, 'mod.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(arena_mod_text(name))
        with open(os.path.join(dest, 'arenas', name + '.arena'), 'wb') as f:
            f.write(arena_compile.compile_arena(source, report=lambda text: None))
        names.append('arena-' + name)
    return names


def main():
    os.chdir(ROOT)
    args = sys.argv[1:]
    bits = port_build.bits_of(args)
    if bits is None:
        return 1
    port_build.set_bits(bits)
    optimise = '-O' + (os.environ.get('PORT_OPT') or WINDOW_OPT)
    more = os.environ.get('RB_WINDOW_FLAGS', '').split()
    port_build.GAME_FLAGS = [optimise if re.match(r'-O\d$', f) else f for f in port_build.GAME_FLAGS] + more
    # The files with SDL and the C library (the remastered renderer, the
    # HD cloth, the window) of the 64-bit programs: HOSTED_FAST, given to
    # the link too (-flto). RB_HOSTED_FLAGS says otherwise, for a
    # measurement.
    if 'RB_HOSTED_FLAGS' in os.environ:
        hosted_more = os.environ['RB_HOSTED_FLAGS'].split()
    else:
        hosted_more = HOSTED_FAST if bits == 64 else []
    target = Target('--windows' in args, bits)
    if not target.windows:
        hosted_more = hosted_more + HOSTED_IN_TEXT
    HOSTED_FLAGS.extend(more + hosted_more)
    problem = check_host(target)
    if problem:
        print('port-sdl: %s' % problem)
        return 1
    if '--host' in args:
        print('port-sdl: this host can build it (%s%s)' % (target.cc, ', SDL 2 in ' + target.sdl if target.sdl else ''))
        return 0
    problems = port_build.check_state_groups(
        [path for path, own, mine in port_build.sources() if own and path not in OTHER_HOSTS], 'src/port')
    problems += port_build.check_state_groups([WINDOW_DIR + f for f in WINDOW_PLAIN + WINDOW_GAME], WINDOW_DIR)
    # the hosts not this build's are named in the groups for the other builds
    problems = [problem for problem in problems if not any(host in problem for host in OTHER_HOSTS)]
    if problems:
        print('\n'.join('port-sdl: ' + problem for problem in problems))
        return 1
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
    jobs += [(target, path, 'hosted', flags, stamp)
             for path in target.hosted + [write_font(target), write_update_key(target)]]
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
    value = code_sum(target, blob)
    summed = None if value is None else compile_code_sum(target, value)
    if summed is None:
        print("FAILED: port-sdl: the sum of the game's object's core was not made")
        return 1
    if not link_program(target, target.program, [blob, summed] + hosted, target.link_flags() + hosted_more):
        return 1
    made = [target.program]
    if '--padded' in args:
        # the same program with room between the game's object and the
        # window's files: a build whose window differs, a little (its
        # game's object where this one's is) and much (the game's
        # variables moved, which no replay of this one's plays on)
        for name, sizes in PADS:
            pad = write_pad(target, name, *sizes)
            program = PADDED % (target.program[:-4] if target.windows else target.program, name)
            program += '.exe' if target.windows else ''
            if pad is None or not link_program(target, program, [blob, summed, pad] + hosted,
                                               target.link_flags() + hosted_more):
                return 1
            made.append(program)
    made += ['%s/mods/%s/' % (target.dir, name) for name in copy_mods(target)]
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
        if not link_program(target, headless, [blob, summed, plain],
                            ['-nostdlib', '-Wl,-e,' + target.under + 'port_WindowsStart', '-lkernel32', '-lws2_32', '-lgcc']
                            + target.windows_link_flags()):
            return 1
        made.append(headless)
    print("OK: port-sdl: %s: %d files (%d compiled now); the core's sum %016X"
          % (', '.join(made), len(game) + len(hosted), built, value))
    return 0


if __name__ == '__main__':
    sys.exit(main())
