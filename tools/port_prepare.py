#!/usr/bin/env python3
"""The mods made from the player's own disc, made by the PC program itself
the first time it runs (docs/MODS.md, "Made from your disc"): the art mod
(tools/port_art.py), the stages' pages made larger (tools/port_stage_hd.py)
and the fighters with their geometry fixed (tools/port_models/geometry.py),
each the same bytes the developer tools make from the same disc.

    python tools/port_prepare.py --image CUE --program rebushido.exe
            --mods FOLDER [--work FOLDER] [--plans FOLDER] [--key KEY]
            [--only art,stages-hd,fighters] [--jobs N]
    python3 tools/port_prepare.py --write-plans FOLDER --symbols EXE.sym

The package for players (tools/port_release.py) ships this file with the
developer tools it runs, a Python of its own with numpy and Pillow
(tools/port_prepare_bundle.py) and the plans the program plays, in
prepare/ beside rebushido.exe; the program runs it when a mod is missing
or was made from another disc or by another version
(src/port/window/prepare.c), shows how far it has come, and lets the
player skip it: what was done is kept in the work folder, and the next
start goes on from there.

The textures are dumped by the program itself, as the developer tools
dump them on the GL prover: run with no window shown, no sound, no mod
and `--dump-textures`, through the art tour's plans (port_art.plan_text)
and the stages' (port_remaster_prove.plan_for, as port_stage_hd dumps
them). Then the same functions as the developer tools' make them larger
(port_art.build, port_stage_hd.build), and fighter_export.py and
geometry.py make the fighter, so what is written is what those tools
write.

It prints a line for the program at every step:

    step I N NAME          the Ith mod of N is begun
    progress PERMILLE TEXT how far all of it has come
    made NAME / failed NAME WHY

and ends 0 when every mod asked for was made. Each mod is written into
the work folder and moved into the mods folder whole, with
made-from-disc.txt beside its mod.txt saying the key it was made for
(the program's version and the disc's): a folder of the mods folder
without that file is the player's, and is never written over.

--write-plans writes the plans for a program from its .sym (what the
packager does, with the repository's tools/playtest.py).
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import port_art  # noqa: E402
import port_stage_hd  # noqa: E402

MODS = ['art', 'stages-hd', 'fighters']
TITLES = {'art': 'Remastered art', 'stages-hd': 'Stages HD', 'fighters': 'Fighters (geometry fixed)'}
MARKER = 'made-from-disc.txt'
MARKER_TEXT = '''# Made from your own disc by rebushido.exe, the first time it ran
# (tools/port_prepare.py; docs/MODS.md, "Made from your disc"). Never
# shipped. Delete this folder and it is made again at the next start.
key = %s
'''
# what the art and the stages' dumps play to (port_art.plan_text's limit;
# port_stage_hd.DUMP_FRAMES)
ART_LIMIT = 9000
# the fighters made: (name, fighter_export's model id, the joints cut);
# tools/port_models/README.md, "The geometry-only pass"
FIGHTERS = [('M101', 4, '5-6,9-10')]
FIGHTER_PASSES = 1
FIGHTERS_MANIFEST = '''# The fighters' own models with their geometry fixed (clipping, flicker,
# joints) and the game's own texture, from your own disc, by
# tools/port_models/geometry.py. Never committed.
name = Fighters (geometry fixed)
version = 0.1
order = 30
author = ReBushido (tools/port_models/)
'''
# How long each piece takes, in seconds on the owner's PC (docs/MODS.md,
# "Made from your disc"), for the share of the whole the bar shows
WEIGHTS = {'art-dump': 50, 'art-build': 12, 'stage-dump': 75, 'stage-build': 64, 'fighters': 3}
RETRIES = 1
RENAME_TRIES = 20
# The settings the program dumps with, whatever the player's rebushido.cfg
# says: the GL prover's own, on which the developer tools dump (gl_main.c's
# defaults), so that the tours reach the same screens -- the stages the
# original picks, its rule for a fight's music, whose loading takes its
# own time -- and the game's sprites are drawn as there, at 4:3.
CHILD_SETTINGS = '''# tools/port_prepare.py: the settings its runs of the program play with
stage_select_everywhere = off
fight_music = original
draw_distance = original
antialiasing = off
aspect = 4:3
stage_mesh = off
fighter_models = off
shadows = original
smooth_light = off
lights = off
reflections = off
ambient_occlusion = off
bloom = off
tonemap = off
effects = original
frame_rate = original
sky = original
see_through = off
stage_lights = off
effect_lights = off
'''
CREATE_NO_WINDOW = 0x08000000


def say(*words):
    print(' '.join(str(w) for w in words), flush=True)


class Progress:
    """The share of the whole that is done, for `progress` lines."""

    def __init__(self, only):
        self.total = 0
        for name in only:
            self.total += {'art': WEIGHTS['art-dump'] + WEIGHTS['art-build'],
                           'stages-hd': WEIGHTS['stage-dump'] + WEIGHTS['stage-build'],
                           'fighters': WEIGHTS['fighters']}[name]
        self.done = 0.0

    def add(self, weight, text):
        self.done += weight
        self.show(text)

    def show(self, text, within=0.0):
        share = (self.done + within) / max(self.total, 1)
        say('progress', min(1000, int(share * 1000)), text)


# ---- the plans ----


def plans(symbols, scratch):
    """{name: text} of the plans of the art's tour and of every stage's,
    for the program whose names are in `symbols` (its .sym); `scratch` a
    folder to work in. Needs the repository (tools/playtest.py)."""
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'tools'))
    import playtest
    import port_remaster_prove as remaster

    program = symbols[:-len('.sym')] if symbols.endswith('.sym') else symbols
    playtest.PORT_PROGRAM = os.path.abspath(program)
    playtest.PORT_SYMBOLS = os.path.abspath(symbols)
    os.makedirs(scratch, exist_ok=True)
    names = playtest.header_names()
    toml = os.path.join(scratch, 'tour.toml')
    out = {}
    for mode in port_art.MODES:
        with open(toml, 'w', encoding='utf-8', newline='\n') as f:
            f.write(port_art.plan_text(mode, ART_LIMIT))
        _, lines = playtest.Test(toml).plan(playtest.Disc('port'), names, 1)
        out['art-%s.plan' % mode] = '\n'.join(lines) + '\n'
    remaster.PROGRAM = os.path.relpath(playtest.PORT_PROGRAM, remaster.ROOT)
    remaster.write_tour(toml, port_stage_hd.DUMP_FRAMES, 1)
    plan = os.path.join(scratch, 'stage.plan')
    for stage in range(port_stage_hd.ALL_STAGES):
        remaster.plan_for(toml, plan, stage, port_stage_hd.DUMP_FRAMES)
        with open(plan, encoding='utf-8') as f:
            out['stage-%02d.plan' % stage] = f.read()
    os.remove(toml)
    os.remove(plan)
    return out


def write_plans(folder, symbols):
    """The plans, written into `folder`."""
    for name, text in plans(symbols, folder).items():
        with open(os.path.join(folder, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    return 0


# ---- the program, playing a plan ----


def child_env():
    """The program's children draw on a window never shown, on the PC's own
    graphics card (the remastered renderer is what dumps the textures,
    and SDL's dummy driver has no OpenGL); --hidden keeps them silent."""
    env = dict(os.environ)
    if os.name == 'nt':
        for name in ('SDL_VIDEODRIVER', 'SDL_AUDIODRIVER'):
            env.pop(name, None)
    return env


def start_run(args, plan, out, dump):
    """The program, playing `plan` with no window shown and no mod,
    writing every texture it draws into `dump`."""
    os.makedirs(out, exist_ok=True)
    log = os.path.join(out, 'log.txt')
    cmd = [args.program, '--hidden', '--no-sound', '--fast', '--config', args.config, '--log', log,
           '--image', args.image, '--plan', plan, '--out', out, '--graphics', 'remastered',
           '--resolution', '1', '--mods', args.empty, '--dump-textures', dump]
    flags = CREATE_NO_WINDOW if os.name == 'nt' else 0
    return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=child_env(),
                            creationflags=flags), log


def run_all(args, runs, progress, weight, what, check=None):
    """Plays the runs, `args.jobs` at once: [(name, plan, out, dump)].
    A run already done (its `.done` beside it) is not played again. A run
    that `check` says stopped short is played once more."""
    waiting = list(runs)
    running = []
    tries = {}
    each = weight / max(len(runs), 1)
    kept = sum(1 for run in runs if os.path.exists(run[2] + '.done'))
    if kept:
        say('note', '%d runs of %s done before' % (kept, what))
    while waiting or running:
        while waiting and len(running) < args.jobs:
            name, plan, out, dump = waiting.pop(0)
            if os.path.exists(out + '.done'):
                progress.add(each, '%s: %s (done before)' % (what, name))
                continue
            if os.path.isdir(out):
                # a run stopped part way (the program skipped, the PC
                # turned off): what it wrote is not kept
                shutil.rmtree(out)
            # each run dumps into a folder of its own, brought into the
            # mod's dump only when it has ended: the program writes a
            # texture once, where its file is not there yet, so a file
            # cut short by a run that was stopped must never be among them
            process, log = start_run(args, plan, out, os.path.join(out, 'dump'))
            running.append((name, plan, out, dump, process, log))
        if not running:
            break
        time.sleep(0.1)
        for item in list(running):
            name, plan, out, dump, process, log = item
            if process.poll() is None:
                continue
            running.remove(item)
            text = ''
            if os.path.exists(log):
                with open(log, encoding='utf-8', errors='replace') as f:
                    text = f.read()
            if check is not None and not check(text):
                tries[name] = tries.get(name, 0) + 1
                if tries[name] <= RETRIES:
                    waiting.append((name, plan, out, dump))
                    continue
                say('note', '%s: %s stopped short' % (what, name))
            bring_in(os.path.join(out, 'dump'), dump)
            with open(out + '.done', 'w') as f:
                f.write('%d\n' % process.returncode)
            shutil.rmtree(out, ignore_errors=True)
            progress.add(each, '%s: %s' % (what, name))


def bring_in(source, dump):
    """A run's dump moved into the mod's, file by file, where the file is
    not there already: as the developer tools' runs write into one folder,
    the first to write a texture's file keeps it."""
    for folder, dirs, names in os.walk(source):
        dirs.sort()
        target = os.path.join(dump, os.path.relpath(folder, source))
        os.makedirs(target, exist_ok=True)
        for name in sorted(names):
            if not os.path.exists(os.path.join(target, name)):
                os.replace(os.path.join(folder, name), os.path.join(target, name))


# ---- the mods ----


def make_art(args, progress, staging):
    dump = os.path.join(args.work, 'art-dump')
    os.makedirs(dump, exist_ok=True)
    runs = [(mode, os.path.join(args.plans, 'art-%s.plan' % mode), os.path.join(args.work, 'art-runs', mode), dump)
            for mode in port_art.MODES]
    # every mode at once, as port_art.dump plays them
    jobs, args.jobs = args.jobs, max(args.jobs, len(runs))
    run_all(args, runs, progress, WEIGHTS['art-dump'], 'the screens played')
    args.jobs = jobs
    progress.show('the art: made larger')
    build = argparse.Namespace(dump=[dump], out=staging, scale=6, keys=None)
    if port_art.build(build):
        return 'the art could not be made larger'
    progress.add(WEIGHTS['art-build'], 'the art: made larger')
    return None


def make_stages(args, progress, staging):
    dump = os.path.join(args.work, 'stage-dump')
    os.makedirs(dump, exist_ok=True)
    runs = [('stage %02d' % s, os.path.join(args.plans, 'stage-%02d.plan' % s),
             os.path.join(args.work, 'stage-runs', 'stage%02d' % s), dump)
            for s in range(port_stage_hd.ALL_STAGES)]
    run_all(args, runs, progress, WEIGHTS['stage-dump'], 'the stages loaded',
            check=lambda text: "the stage's pages" in text)
    progress.show("the stages' pages: made larger")
    build = argparse.Namespace(dump=dump, out=staging, scale=4, keys=None, jobs=args.jobs)
    if port_stage_hd.build(build):
        return "the stages' pages could not be made larger"
    progress.add(WEIGHTS['stage-build'], "the stages' pages: made larger")
    return None


def make_fighters(args, progress, staging):
    ref = os.path.join(args.work, 'fighters-ref')
    models = os.path.join(staging, 'models')
    os.makedirs(models, exist_ok=True)
    flags = CREATE_NO_WINDOW if os.name == 'nt' else 0
    for name, model, joints in FIGHTERS:
        steps = [[sys.executable, os.path.join(HERE, 'fighter_export.py'), '--image', args.image, '--models',
                  str(model), '--out', ref],
                 [sys.executable, os.path.join(HERE, 'port_models', 'geometry.py'), os.path.join(ref, name + '.glb'),
                  os.path.join(models, name + '.glb'), '--passes', str(FIGHTER_PASSES), '--joints', joints]]
        for cmd in steps:
            proc = subprocess.run(cmd, capture_output=True, text=True, creationflags=flags)
            if proc.returncode != 0:
                return '%s: %s' % (os.path.basename(cmd[1]), (proc.stdout + proc.stderr).strip()[-300:])
        progress.add(WEIGHTS['fighters'] / len(FIGHTERS), 'the fighters: %s' % name)
    with open(os.path.join(staging, 'mod.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(FIGHTERS_MANIFEST)
    return None


MAKERS = {'art': make_art, 'stages-hd': make_stages, 'fighters': make_fighters}


def is_players(folder):
    """A folder of the mods folder that this tool did not make: anything
    in it and no made-from-disc.txt."""
    return os.path.isdir(folder) and not os.path.exists(os.path.join(folder, MARKER)) and any(os.scandir(folder))


def put_in_place(staging, target):
    """The mod made, moved into the mods folder whole: the old one (made
    for another key) out of the way first."""
    if os.path.isdir(target):
        old = target + '.old'
        if os.path.isdir(old):
            shutil.rmtree(old)
        renamed(target, old)
        shutil.rmtree(old)
    renamed(staging, target)


def renamed(source, target):
    """A folder renamed. Windows refuses for a moment while something
    still reads a file just written in it (a virus scanner): it is asked
    again, and copied at last."""
    for _ in range(RENAME_TRIES):
        try:
            os.rename(source, target)
            return
        except PermissionError:
            time.sleep(0.5)
    shutil.copytree(source, target)
    shutil.rmtree(source, ignore_errors=True)


def prepare(args):
    only = [m for m in MODS if m in args.only.split(',')]
    progress = Progress(only)
    os.makedirs(args.work, exist_ok=True)
    # a work folder of another key is of no use: begun again
    key_file = os.path.join(args.work, 'key.txt')
    if os.path.exists(key_file):
        with open(key_file, encoding='utf-8') as f:
            if f.read().strip() != args.key:
                for entry in os.listdir(args.work):
                    path = os.path.join(args.work, entry)
                    shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)
    with open(key_file, 'w', encoding='utf-8', newline='\n') as f:
        f.write(args.key + '\n')
    args.empty = os.path.join(args.work, 'no-mods')
    os.makedirs(args.empty, exist_ok=True)
    args.config = os.path.join(args.work, 'empty.cfg')
    with open(args.config, 'w', encoding='utf-8', newline='\n') as f:
        f.write(CHILD_SETTINGS)
    if args.plans is None:
        args.plans = os.path.join(args.work, 'plans')
        write_plans(args.plans, args.program + '.sym')
    failed = 0
    progress.show('begun')
    for i, name in enumerate(only):
        say('step', i + 1, len(only), TITLES[name])
        target = os.path.join(args.mods, name)
        if is_players(target):
            say('failed', name, 'mods/%s is your own (no %s): left as it is' % (name, MARKER))
            failed += 1
            continue
        staging = os.path.join(args.work, name)
        if os.path.isdir(staging):
            shutil.rmtree(staging)
        os.makedirs(staging)
        why = MAKERS[name](args, progress, staging)
        if why:
            say('failed', name, why)
            failed += 1
            continue
        with open(os.path.join(staging, MARKER), 'w', encoding='utf-8', newline='\n') as f:
            f.write(MARKER_TEXT % args.key)
        os.makedirs(args.mods, exist_ok=True)
        put_in_place(staging, target)
        say('made', name)
    if not failed and not args.keep_work:
        shutil.rmtree(args.work)
    progress.show('done')
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', help='your own image of the disc')
    ap.add_argument('--program', help='rebushido.exe, which dumps the textures')
    ap.add_argument('--mods', help='the mods folder the mods are made in')
    ap.add_argument('--work', help='where the dumps are kept until all is made (beside the mods folder)')
    ap.add_argument('--plans', help='the plans the program plays (made from its .sym if not said)')
    ap.add_argument('--key', default='', help='what made-from-disc.txt says: the program\'s version and the disc\'s')
    ap.add_argument('--only', default=','.join(MODS))
    ap.add_argument('--jobs', type=int, default=max(1, min(6, (os.cpu_count() or 2) - 1)))
    ap.add_argument('--keep-work', action='store_true', help='keep the dumps (for a test)')
    ap.add_argument('--write-plans', metavar='FOLDER')
    ap.add_argument('--symbols', help='the program\'s .sym, for --write-plans')
    args = ap.parse_args()
    if args.write_plans:
        return write_plans(args.write_plans, args.symbols)
    for need in ('image', 'program', 'mods'):
        if not getattr(args, need):
            ap.error('--%s is needed' % need)
    args.image = os.path.abspath(args.image)
    args.program = os.path.abspath(args.program)
    args.mods = os.path.abspath(args.mods)
    args.work = os.path.abspath(args.work or os.path.join(os.path.dirname(args.mods), 'prepare-work'))
    if args.plans:
        args.plans = os.path.abspath(args.plans)
    return prepare(args)


if __name__ == '__main__':
    sys.exit(main())
