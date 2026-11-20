#!/usr/bin/env python3
"""Proves that the PC package makes the mods of the player's disc itself,
the same bytes as the developer tools (docs/MODS.md, "Made from your
disc").

    python tools/port_prepare_prove.py [--zip build/release/ReBushido-PC-....zip]
            [--image CUE] [--out build/prepare-prove] [--keep]

On Windows, with Windows' own Python (the developer tools' `build` steps
run on it) and WSL (their dumps run on the GL prover there, which
`python3 tools/port_gl_build.py` builds from the same commit as the
package's program):

 1. The package's zip (the newest of build/release/ unless --zip says) is
    unpacked into a folder of its own, with no tmp/mods/ and nothing else
    beside it, and its rebushido.exe is run as `--prepare-only --hidden
    --no-sound`, with SDL's dummy drivers and with no Python on PATH: the
    package's own path, its own Python, to the end. Its runs that dump
    draw on a window never shown, on the PC's graphics card, as the
    provers' `--gpu card` do (tools/port_prepare.py, child_env).
 2. The developer tools make the same three mods: tools/port_art.py and
    tools/port_stage_hd.py dump on the GL prover and build here;
    tools/fighter_export.py and tools/port_models/geometry.py make the
    fighter, as port_prepare.py's FIGHTERS says.
 3. Every file of the two is compared by SHA-256 (made-from-disc.txt
    aside), and the fighter's model with the owner's own in tmp/mods/,
    where there is one (read, never written).

It prints the time the package took and ends 0 if every file is the same.
build/prepare-prove/ is removed after unless --keep.
"""
import argparse
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_prepare  # noqa: E402

DEFAULT_OUT = os.path.join(ROOT, 'build', 'prepare-prove')
DEFAULT_IMAGE = os.path.join(ROOT, 'Bushido Blade 2 (USA).cue')


def main_checkout():
    """The checkout whose tmp/mods/ is the owner's (a worktree's main one)."""
    proc = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                          capture_output=True, text=True)
    common = proc.stdout.strip()
    return os.path.dirname(common) if proc.returncode == 0 and common else ROOT


def sums(top):
    out = {}
    for folder, dirs, names in os.walk(top):
        dirs.sort()
        for name in sorted(names):
            if name == port_prepare.MARKER:
                continue
            path = os.path.join(folder, name)
            with open(path, 'rb') as f:
                out[os.path.relpath(path, top).replace(os.sep, '/')] = hashlib.sha256(f.read()).hexdigest()
    return out


def wsl_path(path):
    return '/mnt/%s/%s' % (path[0].lower(), path[3:].replace('\\', '/'))


def no_python_path():
    """PATH without a folder that has a python.exe, nor Windows' own
    stand-ins for one."""
    keep = []
    for folder in os.environ.get('PATH', '').split(os.pathsep):
        if not folder or os.path.isfile(os.path.join(folder, 'python.exe')) \
                or os.path.isfile(os.path.join(folder, 'python3.exe')):
            continue
        keep.append(folder)
    return os.pathsep.join(keep)


def still_running(top):
    """How many programs of the package's folder are running."""
    script = "@(Get-Process | Where-Object { $_.Path -like '%s\\*' }).Count" % top.replace("'", "''")
    proc = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True, text=True)
    return int(proc.stdout.strip() or 0)


def run_package(zip_path, image, out, interrupt):
    """The package unpacked and run as a player's first start would make
    the mods; first stopped after `interrupt` seconds, as a player's PC
    turned off, and started again to go on: (seconds in all, its mods
    folder)."""
    folder = os.path.join(out, 'package')
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(folder)
    top = os.path.join(folder, os.listdir(folder)[0])
    env = dict(os.environ)
    env['PATH'] = no_python_path()
    for name in ('PYTHONHOME', 'PYTHONPATH', 'PYTHONSTARTUP', 'VIRTUAL_ENV'):
        env.pop(name, None)
    env['SDL_VIDEODRIVER'] = 'dummy'
    env['SDL_AUDIODRIVER'] = 'dummy'
    shots = os.path.join(out, 'shots')
    os.makedirs(shots)
    seconds = 0.0
    for run in ('first', 'again') if interrupt else ('first',):
        log = os.path.join(out, 'package-%s.log' % run)
        cmd = [os.path.join(top, 'rebushido.exe'), '--prepare-only', '--hidden', '--no-sound', '--log', log,
               '--tap-window', shots, '--image', image]
        began = time.time()
        proc = subprocess.Popen(cmd, env=env, cwd=top, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if run == 'first' and interrupt:
            try:
                proc.wait(interrupt)
                sys.exit('port_prepare_prove: the package ended before %d seconds: no stop to go on from'
                         % interrupt)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            seconds += time.time() - began
            time.sleep(2)
            left = still_running(top)
            print('%s stopped after %d seconds: %d of its programs left running' % (
                'ok:' if not left else 'FAILED:', interrupt, left))
            if left:
                sys.exit(1)
            continue
        proc.wait()
        seconds += time.time() - began
        if proc.returncode != 0:
            sys.exit('port_prepare_prove: the package ended %d (its log: %s)' % (proc.returncode, log))
        if interrupt:
            with open(log, encoding='utf-8', errors='replace') as f:
                text = f.read()
            kept = sum(int(n) for n in re.findall(r'note (\d+) runs? of .* done before', text))
            asked = re.search(r'prepare: making (\S+) from your disc', text)
            print('%s started again, it went on: %d runs of the program kept from before, %s still to make, '
                  '%d seconds' % ('ok:' if kept else 'FAILED:', kept, asked.group(1) if asked else '?',
                                  time.time() - began))
            if not kept:
                sys.exit(1)
    return seconds, os.path.join(top, 'mods')


def run_developer(image, out):
    """The three mods by the developer tools, into out/dev/mods."""
    dev = os.path.join(out, 'dev')
    mods = os.path.join(dev, 'mods')
    script = os.path.join(dev, 'dump.sh')
    os.makedirs(dev, exist_ok=True)
    with open(script, 'w', newline='\n') as f:
        f.write('cd "%s" || exit 1\nsource .venv/bin/activate 2>/dev/null || true\n' % wsl_path(ROOT))
        f.write('python3 tools/port_art.py dump --image "%s" --dump "%s" || exit 1\n'
                % (wsl_path(image), wsl_path(os.path.join(dev, 'art-dump'))))
        f.write('python3 tools/port_stage_hd.py dump --image "%s" --dump "%s" || exit 1\n'
                % (wsl_path(image), wsl_path(os.path.join(dev, 'stage-dump'))))
    proc = subprocess.run(['wsl', 'bash', wsl_path(script)], capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stdout[-2000:] + proc.stderr[-2000:])
        sys.exit('port_prepare_prove: the GL prover\'s dumps failed (python3 tools/port_gl_build.py in WSL first)')
    steps = [[sys.executable, 'tools/port_art.py', 'build', '--dump', os.path.join(dev, 'art-dump'), '--out',
              os.path.join(mods, 'art')],
             [sys.executable, 'tools/port_stage_hd.py', 'build', '--dump', os.path.join(dev, 'stage-dump'), '--out',
              os.path.join(mods, 'stages-hd')]]
    ref = os.path.join(dev, 'fighters-ref')
    for name, model, joints in port_prepare.FIGHTERS:
        steps.append([sys.executable, 'tools/fighter_export.py', '--image', image, '--models', str(model), '--out',
                      ref])
        steps.append([sys.executable, 'tools/port_models/geometry.py', os.path.join(ref, name + '.glb'),
                      os.path.join(mods, 'fighters', 'models', name + '.glb'), '--passes',
                      str(port_prepare.FIGHTER_PASSES), '--joints', joints])
    for cmd in steps:
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if proc.returncode != 0:
            print(proc.stdout[-2000:] + proc.stderr[-2000:])
            sys.exit('port_prepare_prove: %s failed' % ' '.join(cmd[1:3]))
    with open(os.path.join(mods, 'fighters', 'mod.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(port_prepare.FIGHTERS_MANIFEST)
    return mods


def compare(name, made, want):
    same = sorted(k for k in want if made.get(k) == want[k])
    differ = sorted(k for k in want if k in made and made[k] != want[k])
    missing = sorted(set(want) - set(made))
    more = sorted(set(made) - set(want))
    ok = not differ and not missing and not more and want
    print('%s %s: %d files, %d the same, %d differ, %d missing, %d more' % (
        'ok:' if ok else 'FAILED:', name, len(want), len(same), len(differ), len(missing), len(more)))
    for k in (differ + missing + more)[:5]:
        print('    ' + k)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--zip', help='the package (the newest of build/release/ if not said)')
    ap.add_argument('--image', default=DEFAULT_IMAGE if os.path.isfile(DEFAULT_IMAGE)
                    else os.path.join(main_checkout(), os.path.basename(DEFAULT_IMAGE)))
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--keep', action='store_true', help='keep build/prepare-prove/')
    ap.add_argument('--interrupt', type=int, default=100,
                    help='stop the first run after this many seconds and start it again (0: never)')
    args = ap.parse_args()
    if os.name != 'nt':
        sys.exit('port_prepare_prove: on Windows (the package is a Windows program)')
    zips = [args.zip] if args.zip else sorted(glob.glob(os.path.join(ROOT, 'build', 'release', 'ReBushido-PC-*.zip')),
                                              key=os.path.getmtime)
    if not zips or not os.path.isfile(zips[-1]):
        sys.exit('port_prepare_prove: no package (wsl bash tools/build.sh port-release)')
    image = os.path.abspath(args.image)
    if not os.path.isfile(image):
        sys.exit('port_prepare_prove: no disc image %s (--image)' % image)
    out = os.path.abspath(args.out)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    print('port_prepare_prove: %s' % os.path.basename(zips[-1]))
    seconds, made = run_package(zips[-1], image, out, args.interrupt)
    print('port_prepare_prove: the package made its mods in %d seconds in all' % seconds)
    want = run_developer(image, out)
    ok = True
    for name in port_prepare.MODS:
        if not os.path.isfile(os.path.join(made, name, port_prepare.MARKER)):
            print('FAILED: %s has no %s' % (name, port_prepare.MARKER))
            ok = False
        ok = compare(name, sums(os.path.join(made, name)), sums(os.path.join(want, name))) and ok
    owner = os.path.join(main_checkout(), 'tmp', 'mods')
    for name, _, _ in port_prepare.FIGHTERS:
        theirs = os.path.join(owner, 'fighters', 'models', name + '.glb')
        if os.path.isfile(theirs):
            mine = sums(os.path.join(made, 'fighters', 'models')).get(name + '.glb')
            with open(theirs, 'rb') as f:
                same = hashlib.sha256(f.read()).hexdigest() == mine
            print('%s the owner\'s tmp/mods/fighters/models/%s.glb is %s' % (
                'ok:' if same else 'FAILED:', name, 'the same' if same else 'another'))
            ok = ok and same
    if not args.keep:
        shutil.rmtree(out)
    print('port_prepare_prove: %s' % ('OK' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
