#!/usr/bin/env python3
"""The model pipeline from the player's disc to a checked mod, in one
command (tools/port_models/README.md):

    python tools/port_models/build.py --fighter M101 [--image "Bushido Blade 2 (USA).cue"]
            [--steps reference,motions,prepare,parts,assemble,check,renders,port]

reference  the game's own model (tools/fighter_export.py) and the hd mod's
           at two levels of subdivision (tools/fighter_hd.py), into
           tmp/models/ref/ of the main checkout
motions    the game's motions of the fighter (motions.py, run in WSL)
prepare    the pieces to remake, in their bones' frames (prepare.py)
parts      the new pieces, modelled and baked in Blender (fighter_parts.py)
assemble   the file, its levels of detail and its maps (assemble.py), into
           the `remade` mod, tmp/mods/remade/ of the main checkout
check      the checks (check.py); the build fails with them
renders    the game's, the hd mod's and the new model side by side, in
           Blender, through the motions (tools/blender/fighter_scene.py),
           into tmp/models/renders/
port       the same fight in the port's own renderer (headless, in WSL)
           with each of the three, side by side, into
           tmp/models/renders/NAME/port/

Blender is the portable 4.5 in the main checkout's tmp/tools/
(tools/blender/README.md), or $BLENDER. Nothing it writes is committed:
all of it is made from the player's disc.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from recipe import main_checkout  # noqa: E402

STEPS = 'reference,motions,prepare,parts,assemble,check,renders,port'
# fighter_export's model ids and --fight's names (enum RosterId)
ROSTER = {'M100': (3, 'kannuki'), 'M101': (4, 'tatsumi'), 'M102': (5, 'mikado'),
          'M103': (6, 'utsusemi'), 'M104': (7, 'redshadow'), 'M105': (8, 'tsubame'),
          'M106': (9, 'sazanka'), 'M107': (10, 'suminagashi'), 'M108': (11, 'nightstalker'),
          'M109': (12, 'matsumushi'), 'M200': (18, 'gengoro'), 'M201': (19, 'kaun'), 'M202': (20, 'jo'),
          'M203': (21, 'hongou'), 'M204': (22, 'katze'), 'M206': (24, 'chihiro'),
          'M207': (25, 'utamaru'), 'M208': (26, 'tonyumeda')}
OPPONENTS = ('M103', 'M200')
MOD = 'name = Remade fighters\nversion = 0.1\norder = 30\nauthor = ReBushido (tools/port_models/)\n'


def blender():
    path = os.environ.get('BLENDER') or os.path.join(main_checkout(), 'tmp', 'tools',
                                                     'blender-4.5.16-windows-x64', 'blender.exe')
    if not os.path.exists(path):
        sys.exit('build: no Blender at %s (tools/blender/README.md)' % path)
    return path


def run(cmd, **kw):
    print('build: ' + ' '.join(cmd if isinstance(cmd, list) else [cmd]), flush=True)
    done = subprocess.run(cmd, cwd=REPO, **kw)
    if done.returncode != 0:
        sys.exit('build: that step failed (%d)' % done.returncode)


def run_blender(script, args):
    """Blender's output is long: only its lines of our own, and the
    error's, are shown."""
    cmd = [blender(), '--background', '--factory-startup', '--python', script, '--'] + args
    print('build: blender %s %s' % (script, ' '.join(args)), flush=True)
    done = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, errors='replace')
    for line in done.stdout.splitlines() + done.stderr.splitlines():
        if line.startswith(('fighter_', 'Error', 'Traceback', '  File', '    ')) or 'Error' in line:
            print('  ' + line)
    if done.returncode != 0 or 'Traceback' in done.stdout + done.stderr:
        sys.exit('build: Blender failed on %s' % script)


def hd_level(name, levels, out, image):
    recipe = os.path.join(out, 'recipe.json')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(REPO, 'tools', 'fighter_hd.json'), encoding='utf-8') as f:
        data = json.load(f)
    data['models'] = {name: {'levels': levels}}
    with open(recipe, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f)
    run([sys.executable, 'tools/fighter_hd.py', '--image', image, '--models', name, '--out', out,
         '--recipe', recipe])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--fighter', required=True, help='the model file, M101 for Tatsumi')
    ap.add_argument('--image', default=os.path.join(main_checkout(), 'Bushido Blade 2 (USA).cue'))
    ap.add_argument('--steps', default=STEPS)
    ap.add_argument('--frames', default='0:1800:150', help='the renders\' frames of a motion')
    args = ap.parse_args()
    name = args.fighter
    steps = args.steps.split(',')
    main_dir = main_checkout()
    ref = os.path.join(main_dir, 'tmp', 'models', 'ref')
    work = os.path.join(main_dir, 'tmp', 'models', 'work')
    mod = os.path.join(main_dir, 'tmp', 'mods', 'remade')
    out = os.path.join(mod, 'models')
    renders = os.path.join(main_dir, 'tmp', 'models', 'renders', name)
    motions = os.path.join(ref, 'motions')
    if 'reference' in steps:
        ids = [ROSTER[name][0]] + [ROSTER[o][0] for o in OPPONENTS if o != name]
        run([sys.executable, 'tools/fighter_export.py', '--image', args.image, '--models',
             ','.join(str(i) for i in ids), '--out', ref])
        hd_level(name, 2, os.path.join(ref, 'hd2'), args.image)
        hd_level(name, 1, os.path.join(ref, 'hd1'), args.image)
    if 'motions' in steps:
        # WSL's path of the main checkout's folders
        to_wsl = lambda p: subprocess.run(['wsl', 'wslpath', '-a', p.replace('\\', '/')],  # noqa: E731
                                          capture_output=True, text=True).stdout.strip()
        against = ','.join(ROSTER[o][1] for o in OPPONENTS if o != name)
        run(['wsl', 'python3', 'tools/port_models/motions.py', '--fighter', ROSTER[name][1],
             '--against', against, '--image', to_wsl(args.image), '--models', to_wsl(ref),
             '--out', to_wsl(motions)])
    if 'prepare' in steps:
        run([sys.executable, os.path.join(HERE, 'prepare.py'), '--name', name, '--ref', ref,
             '--hd', os.path.join(ref, 'hd2', 'models'), '--work', work])
    if 'parts' in steps:
        run_blender('tools/blender/fighter_parts.py', [os.path.join(work, name), '--look'])
    if 'assemble' in steps:
        run([sys.executable, os.path.join(HERE, 'assemble.py'), '--name', name, '--work', work, '--ref', ref,
             '--hd', os.path.join(ref, 'hd2', 'models'),
             '--lods', os.path.join(ref, 'hd1', 'models') + ',' + ref, '--out', out])
        with open(os.path.join(mod, 'mod.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(MOD)
    if 'check' in steps:
        lod = os.path.join(mod, 'lod')
        run([sys.executable, os.path.join(HERE, 'check.py'), os.path.join(out, name + '.glb'),
             '--reference', os.path.join(ref, name + '.glb'), '--motions', motions,
             '--lod', os.path.join(lod, name + '.lod1.glb'), '--lod', os.path.join(lod, name + '.lod2.glb'),
             '--report', os.path.join(work, name, 'check')])
    if 'renders' in steps:
        # the fight in which he is the first pad's: its first frame is his
        # stance as the fight begins
        clip = None
        runs = sorted(os.listdir(motions)) if os.path.isdir(motions) else []
        runs.sort(key=lambda r: not r.startswith(ROSTER[name][1] + '-'))
        for run_dir in runs:
            for f in sorted(os.listdir(os.path.join(motions, run_dir))):
                if f.startswith(name + '.') and clip is None:
                    clip = os.path.join(motions, run_dir, f)
        models = [os.path.join(ref, name + '.glb'), os.path.join(ref, 'hd2', 'models', name + '.glb'),
                  os.path.join(out, name + '.glb')]
        run_blender('tools/blender/fighter_scene.py',
                    sum((['--model', m] for m in models), []) +
                    (['--motion', clip, '--frames', '0:1:1'] if clip else []) +
                    ['--out', os.path.join(renders, 'stand'), '--views', 'front,side,back,three,face,face_three',
                     '--sheet', 'sheet.png', '--size', '900'])
        if clip:
            run_blender('tools/blender/fighter_scene.py',
                        sum((['--model', m] for m in models), []) +
                        ['--motion', clip, '--frames', args.frames, '--out', os.path.join(renders, 'motion'),
                         '--views', 'three', '--sheet', 'sheet.png', '--size', '700'])
        print('build: the renders are in %s' % renders)
    if 'port' in steps:
        port_pictures(name, ref, out, renders, args.image)


def port_pictures(name, ref, out, renders, image):
    """The same fight in the port (the headless program, in WSL, on
    llvmpipe) with the game's own model, the hd mod's and the new one, at
    the same blanks, and the three side by side."""
    from PIL import Image
    to_wsl = lambda p: subprocess.run(['wsl', 'wslpath', '-a', p.replace('\\', '/')],  # noqa: E731
                                      capture_output=True, text=True).stdout.strip()
    folders = {}
    for label, models in (('game', 'off'), ('hd', os.path.join(ref, 'hd2', 'models')), ('remade', out)):
        shots = os.path.join(renders, 'port', label)
        os.makedirs(shots, exist_ok=True)
        cmd = ['wsl', 'build/port-gl/rebushido-gl', '--image', to_wsl(image), '--fight', 'tatara-shrine',
               ROSTER[name][1], 'utsusemi', '--frames', '1300', '--no-cards', '--graphics', 'remastered',
               '--gl-scale', '3', '--fighter-models', models if models == 'off' else to_wsl(models),
               '--gl-shots', to_wsl(shots), '--gl-every', '150']
        with open(os.path.join(shots, 'log.txt'), 'w', encoding='utf-8', errors='replace') as log:
            run(cmd, stdout=log, stderr=subprocess.STDOUT,
                env=dict(os.environ, WSLENV='SDL_VIDEODRIVER:SDL_AUDIODRIVER:LIBGL_ALWAYS_SOFTWARE',
                         SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy', LIBGL_ALWAYS_SOFTWARE='1'))
        folders[label] = shots
    for f in sorted(os.listdir(folders['game'])):
        if not f.endswith('.png'):
            continue
        pics = [Image.open(os.path.join(folders[k], f)) for k in ('game', 'hd', 'remade')]
        w, h = pics[0].size
        sheet = Image.new('RGB', (w * 3, h))
        for k, p in enumerate(pics):
            sheet.paste(p.convert('RGB'), (k * w, 0))
        sheet.save(os.path.join(renders, 'port', 'side_by_side_' + f))
    print('build: the port\'s pictures, the game\'s, the hd mod\'s and the new model side by side, are in %s'
          % os.path.join(renders, 'port'))


if __name__ == '__main__':
    main()
