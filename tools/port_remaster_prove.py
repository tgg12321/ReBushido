#!/usr/bin/env python3
"""Proves what "Graphics: Remastered" adds to the port's picture, stage by
stage, with nothing shown (docs/PORT.md, "Remastered").

    python3 tools/port_remaster_prove.py --image "Bushido Blade 2 (USA).cue"
            [--stages 0-31] [--views original,far,max] [--scale 3]
            [--antialiasing 1,2] [--aspect 4:3,16:9] [--out build/remaster2]
            [--shots] [--frames N]

For each stage and each setting it runs build/port-gl/rebushido-gl into a
versus fight on that stage, runs the first fighter across it, and reports
what the frame cost: the primitives a frame drew, the room left in every
store of a fixed size the drawing path fills, and the time. With --shots
it writes the hardware's picture and the software GPU's at the same blanks
as PNGs, so that the two can be laid side by side.

The stage is asked for by a `poke` line of the plan (src/port/plan.c): the
menus would take a long script to reach each of the 32, and a build of its
own for each (`make quickboot`) would take an hour. **A poke changes what
the game computes**, so these runs are not the proof that play is
untouched: tools/port_gl_prove.py is, with the three play tests and no
poke.

It needs the prover (tools/port_gl_build.py) and your own image of the
disc. No check runs it: CI has no graphics card.
"""
import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

PROGRAM = 'build/port-gl/rebushido-gl'
# The stages a fight can be set on (include/stage.h): 0 to 31. The six
# after them are a mode's own -- the two team battle courts, chanbara's
# hall, the two practice floors, and stage 37, link mode's castle rooftop
# (select.c, SELECT_STAGE_LINK_TO) -- and are not a versus fight's: the
# versus menus cannot choose them,
# and `poke`ing one into a versus fight is what reaches them here
# (tools/port_stage_prove.py).
FIGHT_STAGES = 32
# Every stage the game can load, a file each (NDATA_STAGES, S00 to S37).
ALL_STAGES = 38
STAGE_NAMES = [
    'main_tatara_shrine', 'meikyokan_dojo', 'shainto_hq', 'tatara_shrine',
    'bus_stop', 'lost_bridge', 'tatara_shrine_path', 'bamboo_thicket',
    'outside_meikyokan', 'future_dump_site', 'snake_cliff', 'kimigaura_coast',
    'orange_tree_fields', 'castle_rooftop', 'under_lost_bridge',
    'terraced_fields', 'under_railway', 'water_tower_dam', 'lumberyard',
    'lighthouse', 'kabuki_stage', 'abandoned_ship', 'ferry_dock',
    'shainto_gate', 'purple_cavern', 'abandoned_station', 'railway',
    'abandoned_tunnel', 'abandoned_shipyard', 'building_rooftop',
    'building_parking_lot', 'shainto_cemetery',
    # the modes' own, which no fight can be set on from the menus
    'team_1', 'team_2', 'chanbara', 'practice_1', 'practice_2', 'stage_37']

# The test this tour is made of: the presses of port_game as far as the
# fight, and then the first fighter running instead of fighting, so that
# the camera travels over the stage and any geometry that comes in late
# shows. No `expect`: the poke has changed which stage it is.
TOUR_TOML = '''\
about = "A tour of one stage for the remastered renderer's prover (tools/port_remaster_prove.py). Not a test: a poke of the plan chooses the stage, which changes what the game computes."
discs = ["port"]
cards = "none"
limit = %(limit)d
seconds = 600

input = [
    "page+30 START",
    "movie+300 START", "movie+600 CROSS", "movie+900 START",
    "menu+30 DOWN", "menu+60 DOWN", "menu+90 DOWN", "menu+150 CROSS",
    "select+280 CROSS", "select+320 P2 CROSS", "select+580 CROSS", "select+620 P2 CROSS",
    "select+1180 CROSS",
    # The first fighter runs: forward, then a quarter turn and forward
    # again, four times over, which takes it across the stage and back.
    "fight+20f UP hold 150f",
    "fight+175f RIGHT hold 60f", "fight+175f UP hold 230f",
    "fight+420f LEFT hold 60f", "fight+420f UP hold 230f",
    "fight+670f RIGHT hold 60f", "fight+670f UP hold 230f",
    "fight+920f LEFT hold 60f", "fight+920f UP hold 230f",
]

compare = ["screen", "stage", "loaded_stage", "p1_x", "p1_y", "p1_z"]

[[checkpoint]]
name = "page"
when = "rb_page == RB_PAGE_UP"
within = 2000
shot = false

[[checkpoint]]
name = "movie"
when = "screen == SCREEN_MOVIE"
within = 1000
shot = false

[[checkpoint]]
name = "menu"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_TOP && screen_frames >= 120"
within = 3000
shot = false

[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 6000
shot = false

# Every two seconds of the fight, while the fighter runs.
[[checkpoint]]
name = "f"
frames = 60
repeat = %(laps)d
shot = false
'''


def write_tour(path, limit, laps):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(TOUR_TOML % {'limit': limit, 'laps': laps})
    return path


def plan_for(toml_path, out, stage, limit):
    """The plan's lines for one stage: the tour, with `poke` lines that
    hold the fight's stage at that one from the `select` checkpoint to
    the end of the run.

    Both the stage the menus chose (`g_fight_config.stage`) and the
    fight's own (`g_fight_stage`) are held: the select screen writes the
    first when the player picks, and the fight's loading step compares
    the second with what is in memory every frame (fight.c), so holding
    both makes the fight load the stage asked for however the menus got
    there."""
    import playtest

    playtest.PORT_PROGRAM = os.path.abspath(PROGRAM)
    playtest.PORT_SYMBOLS = playtest.PORT_PROGRAM + '.sym'
    test = playtest.Test(toml_path)
    order, lines = test.plan(playtest.Disc('port'), playtest.header_names(), 1)
    # `var NAME ADDR SIZE SIGNED` (tests/play/state.toml)
    where = {}
    for line in lines:
        words = line.split()
        if words and words[0] == 'var':
            where[words[1]] = (int(words[2]), int(words[3]))
    for name in ('config_stage', 'stage'):
        if name not in where:
            sys.exit('port_remaster_prove: the plan has no `%s` value' % name)
    # the checkpoints, in the order their `cp` lines come: `select` is
    # the one the pokes are timed from
    cps = [line.split()[1] for line in lines if line.startswith('cp ')]
    select = cps.index('select') + 1
    for name in ('config_stage', 'stage'):
        address, size = where[name]
        lines.append('poke %d 0 %d %d %d %d'
                     % (select, limit, address, size, stage))
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return out


LIMITS = re.compile(
    r'gl run limits frames (\d+) prim_bytes (-?\d+) prim_free (-?\d+) '
    r'queue_size (-?\d+) queue_free (-?\d+) ot_farthest (\d+) tiles (\d+) '
    r'tiles_least (\d+) nodes (\d+) nodes_least (\d+) side_full (\d+)')
FRAME = re.compile(
    r'gl blank (\d+) picture (\d+)x(\d+) scale (\d+) vertices (\d+) '
    r'draws (\d+) uploaded (\d+)')
PER_FRAME = re.compile(r'gl run per_frame_us (\d+)')
KIND = re.compile(r'gl run kind (\S+) seen (\d+) drawn (\d+)')
STAGE_TRACE = re.compile(r'\bloaded_stage=(-?\d+)')


def numbers_of(log, err):
    """What a run measured."""
    out = {'frames': 0, 'prim_bytes': 0, 'prim_free': 0, 'queue_size': 0,
           'queue_free': 0, 'ot_farthest': 0, 'tiles': 0, 'tiles_least': 0,
           'nodes': 0, 'nodes_least': 0,
           'side_full': 0, 'per_frame_us': 0, 'vertices': [], 'prims': 0,
           'stages': set()}
    with open(err, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = LIMITS.search(line)
            if m:
                for i, name in enumerate(['frames', 'prim_bytes', 'prim_free',
                                          'queue_size', 'queue_free',
                                          'ot_farthest', 'tiles',
                                          'tiles_least', 'nodes',
                                          'nodes_least', 'side_full']):
                    out[name] = int(m.group(i + 1))
            m = PER_FRAME.search(line)
            if m:
                out['per_frame_us'] = int(m.group(1))
            m = FRAME.search(line)
            if m:
                out['vertices'].append(int(m.group(5)))
            m = KIND.search(line)
            if m and m.group(1) not in ('fill', 'copy', 'load', 'store', 'setting'):
                out['prims'] += int(m.group(2))
    if os.path.exists(log):
        with open(log, encoding='utf-8', errors='replace') as f:
            for line in f:
                for value in STAGE_TRACE.findall(line):
                    if int(value) >= 0:
                        out['stages'].add(int(value))
    out['vertices_worst'] = max(out['vertices']) if out['vertices'] else 0
    out['vertices_mean'] = (sum(out['vertices']) // len(out['vertices'])
                            if out['vertices'] else 0)
    return out


def run(args, log, err):
    started = time.time()
    with open(log, 'w', encoding='utf-8', errors='replace', newline='\n') as f:
        with open(err, 'w', encoding='utf-8', errors='replace', newline='\n') as g:
            # the GPU's environment where a prover chose one
            # (port_gl_prove.py, use_gpu)
            import port_gl_prove as gl

            subprocess.run(args, stdout=f, stderr=g, cwd=ROOT, env=gl.ENVIRONMENT)
    return time.time() - started


def main():
    os.chdir(ROOT)
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', default='Bushido Blade 2 (USA).cue')
    ap.add_argument('--stages', default='0-31',
                    help='a stage, a range, or several apart by commas')
    ap.add_argument('--views', default='original,max')
    ap.add_argument('--antialiasing', default='1')
    ap.add_argument('--aspect', default='4:3')
    ap.add_argument('--scale', type=int, default=3)
    ap.add_argument('--every', type=int, default=240)
    ap.add_argument('--frames', type=int, default=4200)
    ap.add_argument('--out', default='build/remaster2')
    ap.add_argument('--step', default='step3')
    ap.add_argument('--shots', action='store_true')
    args = ap.parse_args()
    if not os.path.isfile(PROGRAM):
        print('port_remaster_prove: no %s (python3 tools/port_gl_build.py)' % PROGRAM)
        return 1
    if not os.path.isfile(args.image):
        print('port_remaster_prove: no %s: --image names your own image of the disc' % args.image)
        return 1
    stages = []
    for piece in args.stages.split(','):
        if '-' in piece:
            first, last = piece.split('-')
            stages += list(range(int(first), int(last) + 1))
        else:
            stages.append(int(piece))
    stages = [s for s in stages if 0 <= s < FIGHT_STAGES]
    out = os.path.join(args.out, args.step)
    os.makedirs(out, exist_ok=True)
    toml_path = write_tour(os.path.join(out, 'tour.toml'), args.frames, 20)
    settings = [(view, int(aa), aspect)
                for view in args.views.split(',')
                for aa in args.antialiasing.split(',')
                for aspect in args.aspect.split(',')]
    rows = []
    for stage in stages:
        folder = os.path.join(out, '%02d-%s' % (stage, STAGE_NAMES[stage]))
        os.makedirs(folder, exist_ok=True)
        plan = plan_for(
            toml_path, os.path.join(folder, 'plan.txt'), stage, args.frames)
        for view, aa, aspect in settings:
            name = '%s-aa%d-%s' % (view, aa, aspect.replace(':', 'x'))
            shots = os.path.join(folder, name)
            common = [os.path.join('.', PROGRAM), '--image', args.image,
                      '--plan', plan, '--out', shots,
                      '--graphics', 'original' if view == 'off' else 'remastered',
                      '--draw-distance', 'original' if view == 'off' else view,
                      '--antialiasing', str(aa), '--aspect', aspect,
                      '--gl-scale', str(args.scale), '--trace']
            if args.shots:
                os.makedirs(shots, exist_ok=True)
                common += ['--gl-shots', shots, '--gl-every', str(args.every),
                           '--gl-original']
            # the system now and then gives the program no memory at the
            # console's addresses, with nothing of the run's own wrong:
            # such a run is tried again
            for attempt in range(3):
                seconds = run(common, os.path.join(folder, name + '.log'),
                              os.path.join(folder, name + '.err'))
                got = numbers_of(os.path.join(folder, name + '.log'),
                                 os.path.join(folder, name + '.err'))
                if got['frames'] > 0:
                    break
                print('    (no frames; trying again)')
            got['seconds'] = seconds
            rows.append((stage, name, got))
            print('%-24s %-22s prims %7d vertices mean %6d worst %7d  '
                  'tiles %4d to %4d  prim_free %7d of %7d  queue_free %4d  '
                  'ot %4d  side_full %5d  %5.1f s'
                  % (STAGE_NAMES[stage], name, got['prims'],
                     got['vertices_mean'], got['vertices_worst'],
                     got['tiles_least'], got['tiles'], got['prim_free'],
                     got['prim_bytes'], got['queue_free'],
                     got['ot_farthest'], got['side_full'], seconds))
            sys.stdout.flush()
    table = os.path.join(out, 'measured.tsv')
    with open(table, 'w', encoding='utf-8', newline='\n') as f:
        f.write('stage\tname\tsetting\tprims\tvertices_mean\tvertices_worst\t'
                'tiles\ttiles_least\tnodes\tprim_free\tprim_bytes\t'
                'queue_free\tot_farthest\t'
                'side_full\tper_frame_us\tseconds\tloaded_stages\n')
        for stage, name, got in rows:
            f.write('%d\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%s\n'
                    % (stage, STAGE_NAMES[stage], name, got['prims'],
                       got['vertices_mean'], got['vertices_worst'], got['tiles'],
                       got['tiles_least'],
                       got['nodes'], got['prim_free'], got['prim_bytes'],
                       got['queue_free'], got['ot_farthest'], got['side_full'],
                       got['per_frame_us'], got['seconds'],
                       ' '.join(str(s) for s in sorted(got['stages']))))
    print()
    print('port_remaster_prove: %d runs; %s' % (len(rows), table))
    return 0


if __name__ == '__main__':
    sys.exit(main())
