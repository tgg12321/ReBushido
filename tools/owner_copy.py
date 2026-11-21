#!/usr/bin/env python3
"""The owner's copy of the window build, at the repository's root.

    python tools/owner_copy.py

Copies what `wsl bash tools/build.sh port-sdl-windows` made in
build/port-sdl/windows/ -- rebushido.exe, SDL2.dll and the mods the
program ships (every folder of its mods/: english, ui, lights, the sample
arena) -- beside the repository's root, where the owner runs it; and
prepare/ beside it, as the package has it (tools/port_prepare_bundle.py,
with the plans for this rebushido.exe), so that the owner's copy makes the
mods of the disc that are missing at its next start, as a player's does
(docs/MODS.md, "Made from your disc"). The mods made from the owner's own
disc by the developer tools in tmp/mods/ (art, stages-hd, fighters:
tools/port_art.py, tools/port_models/, tools/port_stage_hd.py) go into the
root's mods/ where they are a mod (a mod.txt); a folder there that the
program made itself (made-from-disc.txt) is left alone when tmp/mods/ has
none. None of these is committed: .gitignore and .git/info/exclude keep
them out. rebushido.cfg, logs/, screenshots/ and the memory cards beside
the program are the owner's and are never touched.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
BUILT = os.path.join(ROOT, 'build', 'port-sdl', 'windows')
FILES = ['rebushido.exe', 'SDL2.dll']
DISC_MODS = ['art', 'stages-hd', 'fighters', 'movies-hd']  # fighters: geometry-fixed models, the game's own texture (2026-11-19); movies-hd: tools/port_movies.py, where it was made


def copy_folder(source, target):
    """`target` made again as a copy of `source`."""
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(source, target)


def copy_prepare():
    """prepare/ at the root, made again: the Python, the tools and the
    plans of the rebushido.exe copied. None if it cannot be made here (no
    Windows Python with numpy and Pillow)."""
    import port_prepare
    import port_prepare_bundle

    try:
        files = port_prepare_bundle.files()
    except SystemExit as why:
        return str(why)
    scratch = os.path.join(ROOT, 'build', 'port-prepare', 'plans')
    for name, text in port_prepare.plans(os.path.join(BUILT, 'rebushido.exe.sym'), scratch).items():
        files['plans/' + name] = text.encode('utf-8')
    target = os.path.join(ROOT, 'prepare')
    if os.path.isdir(target):
        shutil.rmtree(target)
    for rel, data in files.items():
        path = os.path.join(target, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
    return None


def main():
    for name in FILES:
        if not os.path.isfile(os.path.join(BUILT, name)):
            print('owner_copy: %s is not built: wsl bash tools/build.sh port-sdl-windows'
                  % os.path.join(BUILT, name))
            return 1
    for name in FILES:
        shutil.copy2(os.path.join(BUILT, name), os.path.join(ROOT, name))
    mods = os.path.join(ROOT, 'mods')
    os.makedirs(mods, exist_ok=True)
    copied = []
    # every mod the build ships, so that a new one is never left behind
    built_mods = os.path.join(BUILT, 'mods')
    for name in sorted(os.listdir(built_mods)) if os.path.isdir(built_mods) else []:
        source = os.path.join(built_mods, name)
        if os.path.isdir(source):
            copy_folder(source, os.path.join(mods, name))
            copied.append(name)
    for name in DISC_MODS:
        source = os.path.join(ROOT, 'tmp', 'mods', name)
        # an empty folder is no mod: the program makes that one itself
        if os.path.isfile(os.path.join(source, 'mod.txt')):
            copy_folder(source, os.path.join(mods, name))
            copied.append(name)
    problem = copy_prepare()
    commit = subprocess.run(['git', '-C', ROOT, 'log', '-1', '--format=%h %s'],
                            capture_output=True, text=True).stdout.strip()
    print('owner_copy: rebushido.exe and SDL2.dll at the root, from %s; mods: %s; prepare/: %s'
          % (commit, ', '.join(copied) or 'none', problem or 'made'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
