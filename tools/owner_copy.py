#!/usr/bin/env python3
"""The owner's copy of the window build, at the repository's root.

    python tools/owner_copy.py

Copies what `wsl bash tools/build.sh port-sdl-windows` made in
build/port-sdl/windows/ -- rebushido.exe, SDL2.dll and the mods the
program ships (every folder of its mods/: english, ui, lights, the sample
arena) -- beside the repository's root, where the
owner runs it, and the mods made from the owner's own disc in tmp/mods/
(hd, art, stages-hd: tools/fighter_hd.py, tools/port_art.py,
tools/port_stage_hd.py) into the root's mods/ beside it. None of these is
committed: .git/info/exclude keeps them out. rebushido.cfg, logs/,
screenshots/ and the memory cards beside the program are the owner's and
are never touched.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILT = os.path.join(ROOT, 'build', 'port-sdl', 'windows')
FILES = ['rebushido.exe', 'SDL2.dll']
DISC_MODS = ['hd', 'art', 'stages-hd']


def copy_folder(source, target):
    """`target` made again as a copy of `source`."""
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(source, target)


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
        if os.path.isdir(source):
            copy_folder(source, os.path.join(mods, name))
            copied.append(name)
    commit = subprocess.run(['git', '-C', ROOT, 'log', '-1', '--format=%h %s'],
                            capture_output=True, text=True).stdout.strip()
    print('owner_copy: rebushido.exe and SDL2.dll at the root, from %s; mods: %s'
          % (commit, ', '.join(copied) or 'none'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
