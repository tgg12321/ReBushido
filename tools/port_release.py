#!/usr/bin/env python3
"""The PC port for Windows as a package for players (BUILD.md, "The
port's package").

    python3 tools/port_release.py [--release] [--no-build]
    python3 tools/port_release.py --self-test

`make port-release` runs it (`wsl bash tools/build.sh port-release
[RB_RELEASE=1]`). It builds the window's program for Windows
(tools/port_sdl_build.py --windows), then puts into build/release/, which
is not in git:

    ReBushido-PC-<version>-<commit>.zip          the package
    ReBushido-PC-<version>-<commit>.zip.sha256   its SHA-256, as sha256sum writes it

<version> is RB_VERSION_NUMBER of include/rebushido.h, <commit> the build's
own (build/port-sdl/windows/git_hash.txt). The zip holds one folder of that
name with rebushido.exe, SDL2.dll, rebushido.exe.sym, mods/ (the mods the
build ships), README.txt (tools/port_release_readme.txt), LICENSE.txt,
NOTICE.txt and licenses/ (SDL 2's, the fonts', stb's).

Nothing of the game goes in, and it refuses a package that has any:

  - every file of mods/ must be byte for byte what the repository makes
    it from: a folder of src/port/mods/ (tracked by git, where there is a
    git to ask) or a sample arena compiled from tools/arenas/<name> as
    tools/port_sdl_build.py compiles it. A mod folder of anything else
    (art, stages-hd, hd, fighters, whatever tmp/mods/ holds) is refused,
    and so is a file more or less in one of its own;
  - no disc image, card, setting, log or screenshot anywhere (.bin .cue
    .iso .chd .img .mcd .cfg .log, a .png outside mods/, logs/,
    screenshots/);
  - no file it did not put there itself: the folder is checked against
    the list of what it wrote, by SHA-256, before it is zipped.

The program must be the commit's: git_hash.txt is HEAD's, and the program
says that commit in its version. --release is for the real thing: a tree
with nothing uncommitted, and the version built without -dev (as `make
rebushido-patch RB_RELEASE=1`). Without it the package is a trial.
--no-build packs what was built last, held to the same checks.

--self-test makes a package of made-up programs in a folder of its own,
plants in copies of it each thing that must be refused, and checks that
each is (tests/rebushido/port_release.py runs it).
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_sdl_build  # noqa: E402  (tools/port_sdl_build.py)

BUILT = 'build/port-sdl/windows'
RELEASE = 'build/release'
HEADER = 'include/rebushido.h'
README = 'tools/port_release_readme.txt'
FONTS = 'src/port/fonts'
STB = ['src/port/vendor/stb_vorbis.c', 'src/port/window/stb_truetype.h']
PROGRAM = ['rebushido.exe', 'SDL2.dll', 'rebushido.exe.sym']
TOP_FILES = PROGRAM + ['README.txt', 'LICENSE.txt', 'NOTICE.txt']
TOP_FOLDERS = ['mods', 'licenses']
# What a player's own folder holds and a package never may: the disc, the
# cards, the settings, the logs and the pictures.
REFUSED_ENDINGS = ('.bin', '.cue', '.iso', '.chd', '.img', '.mcd', '.cfg', '.log')
REFUSED_FOLDERS = ('logs', 'screenshots')
# The DLLs a Windows program may ask for and every Windows has; and SDL2.dll
# beside it. Anything else (libwinpthread, libstdc++) the player would not have.
SYSTEM_DLLS = {'kernel32.dll', 'user32.dll', 'gdi32.dll', 'comdlg32.dll', 'ws2_32.dll', 'msvcrt.dll', 'shell32.dll',
               'advapi32.dll', 'ole32.dll', 'oleaut32.dll', 'winmm.dll', 'imm32.dll', 'version.dll', 'setupapi.dll',
               'iphlpapi.dll', 'bcrypt.dll', 'opengl32.dll', 'dbghelp.dll', 'sdl2.dll'}
SYSTEM_DLL_PREFIXES = ('api-ms-win-',)

NOTICE_PORT = """
The PC program
--------------
rebushido.exe is built from this repository's source (the game's code,
src/port/ and src/rebushido/) and is under the same licence (LICENSE.txt).
It is linked with the start-up code of the mingw-w64 runtime and of GCC,
whose own terms allow that. It has in it, besides:

- SDL 2 (https://www.libsdl.org/), in SDL2.dll beside it: the zlib licence,
  licenses/SDL2-LICENSE.txt.
- stb_vorbis and stb_truetype by Sean Barrett (https://github.com/nothings/stb):
  public domain or MIT, as you prefer, licenses/stb-LICENSE.txt.
- The fonts Marcellus, Archivo and Kaushan Script: the SIL Open Font
  License 1.1, licenses/OFL-*.txt.
- Sony's libsnd and libspu, reconstructed with the rest of PsyQ (above).

Nothing of Bushido Blade 2's data is in this package: the program reads it
from the player's own image of the disc.
"""


def fail(message):
    sys.exit('port_release.py: ' + message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def crlf(text):
    """Text for Windows' own editors."""
    return text.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8')


def version_number():
    match = re.search(r'^#define RB_VERSION_NUMBER "([^"]+)"', read(HEADER).decode('utf-8'), re.M)
    if not match:
        fail('%s has no RB_VERSION_NUMBER' % HEADER)
    return match.group(1)


def git(*args):
    """git's output, or None where there is no git to ask (the build's
    copy of the checkout, tools/mirror.sh)."""
    if os.environ.get('RB_GIT_HASH'):
        return None
    try:
        proc = subprocess.run(['git'] + list(args), capture_output=True, text=True, cwd=ROOT)
    except OSError:
        return None
    return proc.stdout if proc.returncode == 0 else None


# ---- what the mods must be ----


def expected_mods():
    """{path under mods/: bytes} of every file the build ships, made again
    from the repository's sources; and a note if git could not say which
    of src/port/mods/ it tracks."""
    tracked = git('ls-files', '-z', '--', port_sdl_build.SHIPPED_MODS)
    tracked = None if tracked is None else set(p for p in tracked.split('\0') if p)
    files, note = {}, None
    if tracked is None:
        note = 'no git here: the files of %s are not held to what git tracks' % port_sdl_build.SHIPPED_MODS
    for name in sorted(os.listdir(port_sdl_build.SHIPPED_MODS)):
        source = os.path.join(port_sdl_build.SHIPPED_MODS, name)
        if not os.path.isdir(source):
            continue
        for folder, dirs, names in os.walk(source):
            dirs.sort()
            for file in sorted(names):
                path = os.path.join(folder, file)
                rel = os.path.relpath(path, port_sdl_build.SHIPPED_MODS).replace(os.sep, '/')
                if tracked is not None and port_sdl_build.SHIPPED_MODS + '/' + rel not in tracked:
                    fail('%s is not tracked by git: a shipped mod is only what the repository has' % path)
                files[rel] = read(path)
    import arena_compile
    for name in sorted(os.listdir(port_sdl_build.SHIPPED_ARENAS)):
        source = os.path.join(port_sdl_build.SHIPPED_ARENAS, name)
        if not os.path.isfile(os.path.join(source, 'arena.txt')):
            continue
        files['arena-%s/mod.txt' % name] = port_sdl_build.arena_mod_text(name).encode('utf-8')
        files['arena-%s/arenas/%s.arena' % (name, name)] = arena_compile.compile_arena(source,
                                                                                       report=lambda text: None)
    return files, note


# ---- the folder ----


def licence_of_stb(path):
    """The licence at the end of an stb file, from its first line on."""
    text = read(path).decode('utf-8')
    at = text.rfind('This software is available under 2 licenses')
    if at < 0:
        fail('%s has no licence at its end' % path)
    body = text[at:].rstrip().rstrip('*/').rstrip()
    return '%s (%s)\n%s\n%s\n' % (os.path.basename(path), text.split('\n', 1)[0].lstrip('/ ').strip(),
                                  '-' * 60, body)


def contents(built, sdl_licence, mods, version, commit):
    """{path in the package: bytes}, all of it, from the build's folder
    `built` and the repository."""
    out = {}
    for name in PROGRAM:
        path = os.path.join(built, name)
        if not os.path.isfile(path):
            fail('%s is not built: make port-sdl-windows' % path)
        out[name] = read(path)
    # the build's mods/, every file of it, held to `mods` by check()
    mods_dir = os.path.join(built, 'mods')
    if not os.path.isdir(mods_dir):
        fail('%s is not there: make port-sdl-windows' % mods_dir)
    for folder, dirs, names in os.walk(mods_dir):
        dirs.sort()
        for file in sorted(names):
            path = os.path.join(folder, file)
            out['mods/' + os.path.relpath(path, mods_dir).replace(os.sep, '/')] = read(path)
    readme = read(README).decode('utf-8').replace('{version}', version).replace('{commit}', commit)
    out['README.txt'] = crlf(readme)
    out['LICENSE.txt'] = crlf(read('LICENSE').decode('utf-8'))
    out['NOTICE.txt'] = crlf(read('NOTICE').decode('utf-8').rstrip('\n') + '\n' + NOTICE_PORT)
    out['licenses/SDL2-LICENSE.txt'] = crlf(read(sdl_licence).decode('utf-8'))
    for name in sorted(os.listdir(FONTS)):
        if name.startswith('OFL') and name.endswith('.txt'):
            out['licenses/' + name] = crlf(read(os.path.join(FONTS, name)).decode('utf-8'))
    out['licenses/stb-LICENSE.txt'] = crlf('\n\n'.join(licence_of_stb(p) for p in STB))
    return out


def write_folder(folder, files):
    for rel, data in files.items():
        path = os.path.join(folder, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)


def check(folder, written, mods):
    """What is wrong with the package's folder: a list, empty if nothing.
    `written` is {path: sha256} of what was put there; `mods` {path under
    mods/: bytes} of what the mods must be."""
    problems = []
    found = set()
    mod_folders = set(rel.split('/', 1)[0] for rel in mods)
    for here, dirs, names in os.walk(folder):
        dirs.sort()
        for entry in sorted(dirs + names):
            path = os.path.join(here, entry)
            rel = os.path.relpath(path, folder).replace(os.sep, '/')
            parts = rel.split('/')
            lower = rel.lower()
            if os.path.islink(path):
                problems.append('%s is a link' % rel)
                continue
            if entry in dirs:
                if len(parts) == 1 and entry not in TOP_FOLDERS:
                    problems.append('%s/ is no folder of the package' % rel)
                elif parts[0] == 'mods' and len(parts) == 2 and entry not in mod_folders:
                    problems.append('mods/%s/ is no mod the repository makes (made from a disc, or not ours)'
                                    % entry)
                if entry.lower() in REFUSED_FOLDERS:
                    problems.append('%s/ is a player\'s folder' % rel)
                continue
            found.add(rel)
            if lower.endswith(REFUSED_ENDINGS):
                problems.append('%s: a disc image, card, setting or log is never packed' % rel)
            if lower.endswith('.png') and parts[0] != 'mods':
                problems.append('%s: a picture outside mods/ (a screenshot?)' % rel)
            if len(parts) == 1 and entry not in TOP_FILES:
                problems.append('%s is no file of the package' % rel)
            data = read(path)
            if parts[0] == 'mods':
                want = mods.get('/'.join(parts[1:]))
                if want is None:
                    problems.append('%s is no file of the shipped mods' % rel)
                elif want != data:
                    problems.append('%s is not its source\'s bytes' % rel)
            if rel not in written:
                problems.append('%s was not put there by port_release.py' % rel)
            elif written[rel] != sha256(data):
                problems.append('%s changed after port_release.py wrote it' % rel)
    for rel in sorted(set(written) - found):
        problems.append('%s is missing' % rel)
    for rel in sorted(mods):
        if 'mods/' + rel not in found:
            problems.append('mods/%s is missing' % rel)
    for name in TOP_FILES:
        if name not in found:
            problems.append('%s is missing' % name)
    return problems


def check_imports(program):
    """The DLLs `program` asks for that a player's Windows may not have."""
    objdump = shutil.which('x86_64-w64-mingw32-objdump')
    if not objdump:
        return None
    proc = subprocess.run([objdump, '-p', program], capture_output=True, text=True)
    names = re.findall(r'^\s*DLL Name: (\S+)', proc.stdout, re.M)
    if proc.returncode != 0 or not names:
        return ['%s: objdump read no imports' % program]
    return ['%s asks for %s, which a player\'s Windows has not' % (program, name) for name in names
            if name.lower() not in SYSTEM_DLLS and not name.lower().startswith(SYSTEM_DLL_PREFIXES)]


def make_zip(folder, name, written, out_dir):
    """The zip of `folder` as `name`/, its files in the order of `written`;
    and its .sha256. Returns the zip's path."""
    path = os.path.join(out_dir, name + '.zip')
    temporary = path + '.part'
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel in sorted(written):
            info = zipfile.ZipInfo('%s/%s' % (name, rel), date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, read(os.path.join(folder, *rel.split('/'))))
    os.replace(temporary, path)
    with open(path + '.sha256', 'w', newline='\n') as f:
        f.write('%s  %s\n' % (sha256(read(path)), os.path.basename(path)))
    return path


def remove_folder(folder, inside):
    """`folder`, which this tool made, removed: only if it is a folder of
    `inside`."""
    folder, inside = os.path.abspath(folder), os.path.abspath(inside)
    if os.path.dirname(folder) != inside or not os.path.isdir(folder):
        fail('will not remove %s: not a folder of %s' % (folder, inside))
    shutil.rmtree(folder)


# ---- the package ----


def build(release):
    """tools/port_sdl_build.py --windows, with the version a release's or a
    trial's: version.c is compiled again each time, so the program never
    keeps the other's."""
    target = port_sdl_build.Target(True, 64)
    version = target.object_of('src/rebushido/version.c')
    if os.path.exists(version):
        os.remove(version)
    env = dict(os.environ)
    if release:
        env['RB_WINDOW_FLAGS'] = (env.get('RB_WINDOW_FLAGS', '') + ' -DRB_VERSION_SUFFIX=""').strip()
    proc = subprocess.run([sys.executable, 'tools/port_sdl_build.py', '--windows'], env=env)
    if release and os.path.exists(version):
        # the next trial's build has its -dev again
        os.remove(version)
    if proc.returncode != 0:
        fail('the Windows build failed')


def package(release, no_build):
    if release:
        if git('rev-parse', 'HEAD') is None:
            fail('--release needs git: run it in the checkout itself (make port-release RB_RELEASE=1)')
        if git('status', '--porcelain'):
            fail('no release from a tree with changes or files that are not committed')
    if not no_build:
        build(release)
    head = port_sdl_build.port_build.git_hash()
    hash_file = os.path.join(BUILT, 'git_hash.txt')
    commit = read(hash_file).decode('utf-8').strip() if os.path.isfile(hash_file) else ''
    if commit != head:
        fail('the build is of %s, not of HEAD (%s): stale; build it again (make port-sdl-windows)'
             % (commit or 'nothing', head))
    if commit == 'unknown':
        fail('git could not say the commit (RB_GIT_HASH=... names it where git cannot be asked)')
    if release and commit.endswith('-dirty'):
        fail('no release of a build with changes not committed')
    version = version_number()
    full = 'ReBushido %s%s (%s)' % (version, '' if release else '-dev', commit)
    if full.encode('utf-8') not in read(os.path.join(BUILT, 'rebushido.exe')):
        fail('rebushido.exe does not say "%s": not this commit\'s %s build' % (full, 'release' if release else 'trial'))
    target = port_sdl_build.Target(True, 64)
    if not target.sdl:
        fail("no SDL 2 package for MinGW (BUILD.md, \"The port with a window\")")
    sdl_root = os.path.dirname(target.sdl)
    sdl_licence = os.path.join(sdl_root, 'LICENSE.txt')
    if not os.path.isfile(sdl_licence):
        fail('%s is not there' % sdl_licence)
    if read(os.path.join(BUILT, 'SDL2.dll')) != read(os.path.join(target.sdl, 'bin', 'SDL2.dll')):
        fail('%s/SDL2.dll is not the SDL package\'s own' % BUILT)
    problems = check_imports(os.path.join(BUILT, 'rebushido.exe'))
    if problems is None:
        print('port_release: no x86_64-w64-mingw32-objdump: the program\'s DLLs are not looked at')
    elif problems:
        fail('\n  '.join(['the program needs what the package has not:'] + problems))

    mods, note = expected_mods()
    if note:
        if release:
            fail(note)
        print('port_release: ' + note)
    name = 'ReBushido-PC-%s-%s' % (version, commit)
    files = contents(BUILT, sdl_licence, mods, version, commit)
    os.makedirs(RELEASE, exist_ok=True)
    folder = os.path.join(RELEASE, name)
    if os.path.exists(folder):
        remove_folder(folder, RELEASE)
    write_folder(folder, files)
    written = dict((rel, sha256(data)) for rel, data in files.items())
    problems = check(folder, written, mods)
    if problems:
        fail('\n  '.join(['the package is refused (%s is left to look at):' % folder] + problems))
    path = make_zip(folder, name, written, RELEASE)
    remove_folder(folder, RELEASE)
    # the zip read back: what it holds is what was checked
    with zipfile.ZipFile(path) as z:
        held = dict((info.filename[len(name) + 1:], sha256(z.read(info))) for info in z.infolist())
    if held != written:
        fail('%s does not hold what was checked' % path)
    print('OK: %s (%d files, %d bytes; %s)' % (path, len(written), os.path.getsize(path),
                                              'a release' if release else 'a trial'))
    return 0


# ---- the self-test ----


def plants():
    """(what, path, bytes or None to change a file) of each thing the check
    must refuse."""
    return [
        ('a disc image', 'Bushido Blade 2 (USA).bin', b'\0' * 2352),
        ('its cue sheet', 'Bushido Blade 2 (USA).cue', b'FILE "x.bin" BINARY\n'),
        ('an .iso', 'mods/english/disc.iso', b'CD001'),
        ('a .chd', 'game.chd', b'MComprHD'),
        ('an .img', 'licenses/disc.img', b'\0'),
        ('a memory card', 'card1.mcd', b'MC' + b'\0' * 126),
        ('a settings file', 'rebushido.cfg', b'graphics = remastered\n'),
        ('a log', 'logs/rebushido-20261115-203000.log', b'rebushido: started\n'),
        ('a screenshot', 'screenshots/shot-20261115-162200-000.png', b'\x89PNG\r\n\x1a\n'),
        ('a .png at the top', 'shot.png', b'\x89PNG\r\n\x1a\n'),
        ('the art mod', 'mods/art/textures/menu.png', b'\x89PNG\r\n\x1a\n'),
        ('the HD stages', 'mods/stages-hd/mod.txt', b'name = stages hd\n'),
        ('the HD fighters', 'mods/hd/mod.txt', b'name = hd\n'),
        ('fighters', 'mods/fighters/mod.txt', b'name = fighters\n'),
        ('a file more in a shipped mod', 'mods/english/textures/extra.png', b'\x89PNG\r\n\x1a\n'),
        ('a shipped mod\'s file changed', 'mods/english/mod.txt', None),
        ('the arena changed', 'mods/arena-courtyard/arenas/courtyard.arena', None),
        ('a file more at the top', 'extra.txt', b'hello\n'),
        ('a folder more at the top', 'tools/x.py', b'print(1)\n'),
        ('the program changed', 'rebushido.exe', None),
    ]


def self_test(report=print):
    """The failures: an empty list if every plant was refused and the clean
    package passed. Run from the repository's root."""
    failures = []
    mods, _ = expected_mods()
    # (no tempfile: tests/random.py is the random a test imports)
    work = os.path.join(ROOT, 'build', 'tests', 'port_release')
    if os.path.isdir(work):
        remove_folder(work, os.path.dirname(work))
    os.makedirs(work)
    try:
        built = os.path.join(work, 'built')
        for name in PROGRAM:
            write_folder(built, {name: hashlib.sha256(name.encode()).digest() * 64})
        write_folder(os.path.join(built, 'mods'), mods)
        licence = os.path.join(work, 'SDL-LICENSE.txt')
        write_folder(work, {'SDL-LICENSE.txt': b'made up for the test\n'})
        files = contents(built, licence, mods, version_number(), '0000000')
        written = dict((rel, sha256(data)) for rel, data in files.items())
        clean = os.path.join(work, 'clean')
        write_folder(clean, files)
        problems = check(clean, written, mods)
        if problems:
            failures.append('the clean package is refused: ' + '; '.join(problems[:5]))
        else:
            report('ok: the clean package passes (%d files)' % len(written))
        # each plant made in the clean folder, checked, and taken out again
        # (copying the folder each time is slow on the Windows drive)
        for what, rel, data in plants() + [('a shipped mod\'s file missing', 'mods/ui/mod.txt', False)]:
            path = os.path.join(clean, *rel.split('/'))
            made = []
            if data is None or data is False:
                kept = read(path)
                if data is None:
                    write_folder(clean, {rel: bytes([kept[0] ^ 1]) + kept[1:]})
                else:
                    os.remove(path)
            else:
                at = os.path.dirname(path)
                while not os.path.isdir(at):
                    made.append(at)
                    at = os.path.dirname(at)
                write_folder(clean, {rel: data})
            problems = check(clean, written, mods)
            if data is None or data is False:
                write_folder(clean, {rel: kept})
            else:
                os.remove(path)
                for folder in made:
                    os.rmdir(folder)
            if problems:
                report('ok: %s (%s) is refused: %s' % (what, rel, problems[0]))
            else:
                failures.append('%s (%s) is not refused' % (what, rel))
        problems = check(clean, written, mods)
        if problems:
            failures.append('the clean package is refused after the plants: ' + '; '.join(problems[:5]))
        # the zip holds exactly what was checked
        path = make_zip(clean, 'ReBushido-PC-test', written, work)
        with zipfile.ZipFile(path) as z:
            names = sorted(info.filename for info in z.infolist())
        if names != sorted('ReBushido-PC-test/' + rel for rel in written):
            failures.append('the zip does not hold the package\'s files under its folder')
        else:
            report('ok: the zip holds the %d files under ReBushido-PC-test/' % len(names))
    finally:
        remove_folder(work, os.path.dirname(work))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--release', action='store_true', help='a release: a clean tree, no -dev')
    parser.add_argument('--no-build', action='store_true', help='pack what was built last')
    parser.add_argument('--self-test', action='store_true', help='check that what must be refused is')
    args = parser.parse_args()
    os.chdir(ROOT)
    if args.self_test:
        failures = self_test()
        for failure in failures:
            print('FAILED: ' + failure)
        print('port_release self-test: %s' % ('FAILED' if failures else 'OK'))
        return 1 if failures else 0
    return package(args.release, args.no_build)


if __name__ == '__main__':
    sys.exit(main())
