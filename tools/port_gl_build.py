#!/usr/bin/env python3
"""Builds the prover of the port's remastered renderer: the port with an
OpenGL 3.3 core context made through EGL, with no window, no display and
no surface (docs/PORT.md, "Remastered").

    python3 tools/port_gl_build.py            -> build/port-gl/rebushido-gl
    python3 tools/port_gl_build.py --bits 32  -> of 32-bit pointers
    python3 tools/port_gl_build.py --host     -> whether this host can
    python3 tools/port_gl_build.py --windows  -> build/port-gl/windows/
                                                 rebushido-gl.exe, SDL2.dll
                                                 beside it: the same prover
                                                 on Windows' own graphics
                                                 card (MinGW, as the window
                                                 build; `make port-gl-windows`)

It is the build with a window (tools/port_sdl_build.py) with EGL in
place of SDL and src/port/gl/ in place of src/port/window/'s two files:
everything of the game and the port is linked into one object whose
names are its own, so that the C library's `open`, `read`, `main` and
`printf` are not given the game's.

It is built with 64-bit pointers, as the port is (docs/PORT.md, "64
bits"), or with 32-bit ones (`--bits 32`, `PORT_BITS`).

What it needs on Linux: gcc, a C library and EGL of the size asked for
(Ubuntu, 64 bits: libegl-dev, which is there for any desktop; 32 bits:
lib32gcc-13-dev libegl-dev:i386, which come with the window's own
needs). Mesa's software driver (mesa-libgallium) is the GL. Nothing of
this is needed by any check: no check has a graphics card.

For Windows (`--windows`) it needs what the window's Windows build needs
(tools/port_sdl_build.py: MinGW and SDL 2's MinGW package), and SDL makes
the context, on a window that is never shown (`PORT_GL_SDL` in
src/port/gl/gl_main.c): the provers run it on the player's own card
(`--gpu card`; docs/PORT.md, "The provers on a graphics card").
"""
import concurrent.futures
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_build  # noqa: E402
import port_sdl_build as sdl  # noqa: E402
from port_build import host_tests, modern  # noqa: E402

OUT_DIR = 'build/port-gl'
GL_DIR = 'src/port/gl/'
# What talks to EGL and to the C library.
GL_HOSTED = ['gl_bench.c', 'gl_cloth.c', 'gl_draw.c', 'gl_fighter.c', 'gl_fighter_card.c', 'gl_fighter_file.c', 'gl_light.c', 'gl_lights.c', 'gl_load.c', 'gl_replace.c', 'gl_scene.c', 'gl_see.c',
             'gl_shadow.c', 'gl_sky.c', 'gl_stage.c', 'gl_sun.c', 'gl_tween.c', 'gl_main.c']
# The window's own arithmetic, which this program uses to make the
# software GPU's picture for the comparison; and its host.
WINDOW_PLAIN = ['scale.c', 'ring.c', 'pace.c', 'keymap.c']
WINDOW_HOST = 'src/port/window/host_posix.c'
EGL_PROBE = ('#include <EGL/egl.h>\nint main(void) '
             '{ return eglGetDisplay(EGL_DEFAULT_DISPLAY) == 0; }\n')


class Target(sdl.Target):
    """The one program: Linux and EGL, or Windows and SDL; pointers of the
    size asked for."""

    def __init__(self, bits, windows=False):
        sdl.OUT_DIR = OUT_DIR
        super().__init__(windows, bits)
        if windows:
            # SDL's context on a hidden window, and the window build's host
            self.program = self.dir + '/rebushido-gl.exe'
            self.hosted = [GL_DIR + f for f in GL_HOSTED] + ['src/port/host_windows.c']
            return
        self.program = OUT_DIR + '/rebushido-gl'
        self.sdl = None
        self.sdl_flags = []
        self.hosted = [GL_DIR + f for f in GL_HOSTED] + [WINDOW_HOST]

    def link_flags(self):
        if self.windows:
            return super().link_flags()
        return [self.size_flag, '-no-pie', '-lEGL', '-Wl,-Ttext-segment=0x%X' % sdl.PROGRAM_BASE]


def check_host(target):
    """None, or why this host cannot build it."""
    os.makedirs(target.dir, exist_ok=True)
    probe = target.dir + '/egl_probe'
    proc = sdl.run([target.cc, target.size_flag] + ['-x', 'c', '-', '-o', probe, '-lEGL'], input=EGL_PROBE)
    if proc.returncode != 0:
        return ('%s cannot make a %d-bit program with EGL: %s\n'
                'port-gl: on Ubuntu: sudo apt install libegl-dev%s'
                % (target.cc, target.bits, (proc.stderr.strip().split('\n') or ['?'])[-1],
                   '' if target.bits == 64 else
                   ':i386 lib32gcc-13-dev   (not gcc-multilib: it removes the MIPS compiler)'))
    os.remove(probe)
    return None


def main():
    os.chdir(ROOT)
    args = sys.argv[1:]
    bits = port_build.bits_of(args)
    if bits is None:
        return 1
    # the game's files are compiled for this size: -m64 and no vector
    # instructions, or -m32 (tools/port_build.py, set_bits)
    port_build.set_bits(bits)
    windows = '--windows' in args
    target = Target(bits, windows)
    # the files that talk to EGL see src/port/gl/ and the window's header
    sdl.HOSTED_FLAGS = sdl.HOSTED_FLAGS + ['-I' + GL_DIR.rstrip('/'), '-Isrc/port/window']
    if windows:
        sdl.HOSTED_FLAGS.append('-DPORT_GL_SDL')
    problem = sdl.check_host(target) if windows else check_host(target)
    if problem:
        print('port-gl: %s' % problem)
        return 1
    if '--host' in args:
        print('port-gl: this host can build it (%s, %d-bit)' % (target.cc, bits))
        return 0
    os.makedirs(target.obj_dir, exist_ok=True)
    host_tests.OUT_DIR = OUT_DIR
    os.makedirs(port_build.OUT_DIR, exist_ok=True)
    host_tests.write_headers(True)

    flags = port_build.GAME_FLAGS + host_tests.include_flags(True) + modern.DEFS + host_tests.REBUSHIDO_DEFS
    flags += port_build.MOVOVL_INCLUDES
    flags += ['-DRB_GIT_HASH="%s"' % port_build.git_hash()]
    stamp = max(os.path.getmtime(p) for p in (__file__, sdl.__file__, port_build.__file__, 'tests/run.py',
                                              'tools/modern/check.py', 'tools/port_gte_macros.py', modern.SHIM))
    jobs = [(target, path, 'port' if own else 'game', flags + mine, stamp)
            for path, own, mine in port_build.sources() if path not in sdl.OTHER_HOSTS]
    jobs += [(target, 'src/port/window/' + f, 'port', flags, stamp) for f in WINDOW_PLAIN]
    jobs += [(target, path, 'hosted', flags, stamp) for path in target.hosted]
    game, hosted, failed, built = [], [], 0, 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for job, (path, obj, compiled, messages) in zip(jobs, pool.map(sdl.compile_one, jobs)):
            built += compiled
            if obj is None:
                failed += 1
                print('port-gl: %s does not compile:' % path)
                print('\n'.join('    ' + line for line in messages.strip().split('\n')[:40]))
            elif job[2] == 'hosted':
                hosted.append(obj)
            else:
                game.append(obj)
    if failed:
        print('FAILED: port-gl: %d files do not compile' % failed)
        return 1
    blob = sdl.link_blob(target, game)
    if blob is None:
        print("FAILED: port-gl: the game's object does not link")
        return 1
    if not sdl.link_program(target, target.program, [blob] + hosted, target.link_flags()):
        return 1
    if windows:
        shutil.copyfile(target.sdl + '/bin/SDL2.dll', target.dir + '/SDL2.dll')
    print('OK: port-gl: %s: %d-bit, %d files (%d compiled now)'
          % (target.program, bits, len(game) + len(hosted), built))
    return 0


if __name__ == '__main__':
    sys.exit(main())
