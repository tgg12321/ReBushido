#!/usr/bin/env python3
"""Proves the stage's ground drawn as one mesh against the game's own
tiles, stage by stage, with nothing shown (docs/PORT.md, "Remastered",
"The ground as one mesh").

    python3 tools/port_stage_prove.py --image "Bushido Blade 2 (USA).cue"
            [--stages 0-37] [--scale 2] [--frames 4200] [--every 300]
            [--out build/stagemesh] [--pairs 2] [--distance max]
            [--mesh every|on] [--against tiles|nodepth] [--mods FOLDER]
            [--props] [--gpu llvmpipe|card]

Every stage the game can load is measured, 0 to 37: the 32 a fight can
be set on, and the six the modes have of their own, which the menus
cannot choose and a `poke` of the plan puts into a versus fight
(tools/port_remaster_prove.py, `plan_for`).

For each stage it runs build/port-gl/rebushido-gl twice over the same
tour -- the first fighter run across the stage, which swings the camera
right round -- once with the mesh and once without, and reports:

  - that the trace of every watched value is the same both ways, which is
    the whole claim that the game is untouched;
  - how many pixels of the pictures differ, how many by more than a
    twentieth of a channel, and how many are pixels where the tiles drew
    the stage and the mesh drew nothing (a hole, which would be the one
    unforgivable fault) or the other way about (ground the tiles left
    out, which is what the mesh is for);
  - what the frame cost each way;
  - the verdict: whether the stage keeps the mesh by the bar
    (verdict_of), written as a table into `verdict.txt` beside the
    measurements, with the line of `g_stage_proved` it comes to
    (src/port/gl/gl_stage.c).

--mesh says what the run with the mesh is told: `every` draws it on
whatever stage the run reaches, which is what measuring a stage needs,
and `on` is what the player gets, which is how to check the table
itself. --against says what the other run draws: `tiles` is the game's
own ground, and `nodepth` is the mesh with the scene not held against
its depth, which is how to see what that test alone changed.

With --pairs N it writes N pictures a stage of the two side by side
(tiles | mesh) into the stage's folder, and with --mark N it writes N
pictures a stage of the mesh's own with the cells that count reddened,
which is how to see at once whether what differs is a wrongly drawn face
or the speckle of a ground read half a texel along.

With --props it proves the stage's pieces instead -- the railings,
fences, walls and roofs drawn from the ground's mesh as well
(`stage_props_mesh`; docs/PORT.md, "The stage's pieces as meshes"): both
runs draw the ground from the mesh, one the pieces from it too
(`--stage-props every`) and the other from the game's own primitives,
by the same bar, into build/stageprops/ unless --out says, and the
verdict's line is g_stage_props_proved's. Without it both runs leave
the pieces to the game's primitives, so that what is measured is the
ground alone.

It needs the prover (tools/port_gl_build.py) and your own image of the
disc. No check runs it: CI has no graphics card.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_gl_prove as gl
import port_png
import port_remaster_prove as remaster

PROGRAM = remaster.PROGRAM
STAGE_NAMES = remaster.STAGE_NAMES


def read(path):
    width, height, pixels = port_png.read_png(path)
    return width, height, pixels


# A cell of 16 by 16 pixels whose two means are more than 24 apart, in
# an area of four such cells or more. The sizes are not a guess: three
# pairs of pictures were measured at every size from 4 to 16 and every
# step from 16 to 48 -- one pair known wrong (a wall of the terrace
# drawn across the whole picture) and two known right -- and 16 and 24
# is where the wrong one marks 45% of its cells and the right ones mark
# none at all. Below that size the speckle of a ground of noise read
# half a texel along is marked as well, and it runs together into areas
# of any size, which is what makes the number useless for telling a
# wrongly drawn face from a picture that is right.
CELL = 16
STEP = 24
PATCH = 4

# The bar a stage keeps the mesh by (docs/PORT.md, "The ground as one
# mesh"): its trace the same both ways, the areas of plainly other
# colour under a twentieth of the pixels, the largest of them under
# 60,000 pixels, and under a hundredth of the pixels black where the
# other run drew the stage. The measured range is nothing to a few per
# cent and the one picture known to be wrong measured 45%: the bar sits
# between them and not at the edge of what passes.
BAR_PATCH_SHARE = 0.05
BAR_WORST_PATCH = 60000
BAR_HOLE_SHARE = 0.01


def verdict_of(row):
    """Why a stage does not keep the mesh, or None."""
    seen = row['pixels']
    pixels = max(seen['pixels'], 1)
    if seen['shots'] == 0:
        return 'no pictures were taken'
    if not row['same_trace']:
        return 'the traces differ'
    if seen['patch'] > BAR_PATCH_SHARE * pixels:
        return 'areas of plainly other colour: %.2f%% of the pixels' % (
            100.0 * seen['patch'] / pixels)
    if seen['worst_patch'] > BAR_WORST_PATCH:
        return 'one area of %d pixels of plainly other colour' % (
            seen['worst_patch'])
    if seen['holes'] > BAR_HOLE_SHARE * pixels:
        return 'black where the other run drew: %.2f%% of the pixels' % (
            100.0 * seen['holes'] / pixels)
    return None


def write_verdict(path, rows, props=False):
    """The table a reader looks at, and the line of g_stage_proved (or,
    with --props, g_stage_props_proved) it comes to
    (src/port/gl/gl_stage.c)."""
    table = 'g_stage_props_proved' if props else 'g_stage_proved'
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        if props:
            f.write('The stage\'s pieces drawn from the mesh, held against '
                    'the game\'s own primitives\n')
            f.write('(tools/port_stage_prove.py --props; docs/PORT.md, "The '
                    'stage\'s pieces as meshes").\n\n')
        else:
            f.write('The stage\'s ground as one mesh, held against the other '
                    'run\n')
            f.write('(tools/port_stage_prove.py; docs/PORT.md, "The ground as '
                    'one mesh").\n\n')
        f.write('%-3s %-22s %-6s %8s %8s %8s %8s %8s  %s\n'
                % ('n', 'stage', 'trace', 'pictures', 'differ%', 'areas%',
                   'worst', 'black%', 'verdict'))
        for row in rows:
            seen = row['pixels']
            pixels = max(seen['pixels'], 1)
            why = verdict_of(row)
            f.write('%-3d %-22s %-6s %8d %8.2f %8.3f %8d %8.3f  %s\n'
                    % (row['stage'], row['name'],
                       'same' if row['same_trace'] else 'DIFFER',
                       seen['shots'], 100.0 * seen['differ'] / pixels,
                       100.0 * seen['patch'] / pixels, seen['worst_patch'],
                       100.0 * seen['holes'] / pixels,
                       'the mesh' if why is None else 'the tiles: ' + why))
        kept = set(row['stage'] for row in rows if verdict_of(row) is None)
        seen_stages = set(row['stage'] for row in rows)
        f.write('\nstages measured %d, the mesh kept on %d\n'
                % (len(rows), len(kept)))
        f.write('\n%s, a stage a number, for the stages measured '
                'here\n(a stage this run did not reach is left as it was):\n'
                % table)
        line = []
        for stage in sorted(seen_stages):
            line.append('%d' % (1 if stage in kept else 0))
        f.write('  %s\n' % ', '.join(line))
    return path


def cell_means(pixels, w, cell_x, cell_y):
    """The mean of a cell's pixels, one number per channel."""
    total = [0, 0, 0]
    count = 0
    for y in range(cell_y * CELL, cell_y * CELL + CELL):
        at = (y * w + cell_x * CELL) * 3
        for _ in range(CELL):
            total[0] += pixels[at]
            total[1] += pixels[at + 1]
            total[2] += pixels[at + 2]
            at += 3
            count += 1
    return [t // count for t in total]


def patches(cells, cw, ch):
    """The connected areas of the cells that differ, in cells: how many
    cells are in an area of PATCH cells or more, and the largest area.

    A face drawn that should not be, or left out that should not be,
    shows as one area of the picture whose colour is plainly other; a
    ground of noise read half a texel along differs in as many pixels
    but keeps its colour, and a cell's mean is what tells the two
    apart."""
    seen = bytearray(cw * ch)
    big = 0
    worst = 0
    for start in range(cw * ch):
        if not cells[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        found = 0
        while stack:
            at = stack.pop()
            found += 1
            x = at % cw
            y = at // cw
            for to, ok in ((at - 1, x > 0), (at + 1, x + 1 < cw),
                           (at - cw, y > 0), (at + cw, y + 1 < ch)):
                if ok and cells[to] and not seen[to]:
                    seen[to] = 1
                    stack.append(to)
        if found >= PATCH:
            big += found
        if found > worst:
            worst = found
    return big, worst


def mark(path, pixels, w, h, cells, cw):
    """The cells that count, drawn on the mesh's own picture."""
    shown = bytearray(pixels)
    for n, on in enumerate(cells):
        if not on:
            continue
        for dy in range(CELL):
            at = (((n // cw) * CELL + dy) * w + (n % cw) * CELL) * 3
            for _ in range(CELL):
                shown[at] = min(255, shown[at] + 120)
                at += 3
    port_png.write_png(path, w, h, bytes(shown))


def look(a_dir, b_dir, pairs, folder, first_blank, marks=0):
    """The two runs' pictures against each other."""
    got = {'shots': 0, 'pixels': 0, 'differ': 0, 'far': 0, 'worst': 0,
           'holes': 0, 'filled': 0, 'patch': 0, 'worst_patch': 0}
    if not os.path.isdir(a_dir) or not os.path.isdir(b_dir):
        return got
    names = sorted(n for n in os.listdir(a_dir) if n.endswith('.png'))
    made = 0
    for name in names:
        # gl-004200.png: the blank it was taken at. The menus before the
        # fight are the same picture both ways and say nothing about the
        # ground, so only the fight is measured.
        digits = ''.join(c for c in name if c.isdigit())
        if digits and int(digits) < first_blank:
            continue
        b_path = os.path.join(b_dir, name)
        if not os.path.exists(b_path):
            continue
        aw, ah, ap = read(os.path.join(a_dir, name))
        bw, bh, bp = read(b_path)
        if (aw, ah) != (bw, bh):
            continue
        got['shots'] += 1
        got['pixels'] += aw * ah
        cw = (aw + CELL - 1) // CELL
        ch = (ah + CELL - 1) // CELL
        counted = bytearray(cw * ch)
        for i in range(0, len(ap), 3):
            a = ap[i:i + 3]
            b = bp[i:i + 3]
            if a == b:
                continue
            got['differ'] += 1
            d = max(abs(a[j] - b[j]) for j in range(3))
            if d > 32:
                got['far'] += 1
            at = i // 3
            counted[(at // aw // CELL) * cw + (at % aw) // CELL] = 1
            if d > got['worst']:
                got['worst'] = d
            # black is what the game clears its picture to: a pixel the
            # tiles drew and the mesh did not is a hole in the ground
            if b == b'\0\0\0' and a != b'\0\0\0':
                got['holes'] += 1
            elif a == b'\0\0\0':
                got['filled'] += 1
        # a cell the two pictures hold a plainly other colour in
        cells = bytearray(cw * ch)
        for n, touched in enumerate(counted):
            if not touched:
                continue
            x = n % cw
            y = n // cw
            if (x + 1) * CELL > aw or (y + 1) * CELL > ah:
                continue
            one = cell_means(ap, aw, x, y)
            two = cell_means(bp, bw, x, y)
            if max(abs(one[j] - two[j]) for j in range(3)) > STEP:
                cells[n] = 1
        big, worst = patches(cells, cw, ch)
        if made < marks:
            mark(os.path.join(folder, 'mark-' + name), bp, bw, bh, cells, cw)
        got['patch'] += big * CELL * CELL
        got['worst_patch'] = max(got['worst_patch'], worst * CELL * CELL)
        if made < pairs:
            made += 1
            out = os.path.join(folder, 'pair-' + name)
            wide = bytearray(aw * 2 * ah * 3)
            for y in range(ah):
                at = y * aw * 2 * 3
                wide[at:at + aw * 3] = ap[y * aw * 3:(y + 1) * aw * 3]
                wide[at + aw * 3:at + aw * 6] = bp[y * aw * 3:(y + 1) * aw * 3]
            port_png.write_png(out, aw * 2, ah, bytes(wide))
    return got


def same_trace(a, b):
    if not os.path.exists(a) or not os.path.exists(b):
        return False
    with open(a, 'rb') as f:
        one = f.read()
    with open(b, 'rb') as f:
        two = f.read()
    return one == two


def main():
    os.chdir(ROOT)
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', default='Bushido Blade 2 (USA).cue')
    ap.add_argument('--stages', default='0-31')
    ap.add_argument('--scale', type=int, default=2)
    ap.add_argument('--tall', type=int, default=0)
    ap.add_argument('--frames', type=int, default=4200)
    ap.add_argument('--every', type=int, default=300)
    ap.add_argument('--distance', default='max')
    ap.add_argument('--aspect', default='4:3')
    ap.add_argument('--pairs', type=int, default=0)
    ap.add_argument('--from-blank', type=int, default=2900)
    ap.add_argument('--mark', type=int, default=0)
    # What the run with the mesh on is told. `every` draws the mesh on
    # whatever stage the run reaches, which is what measuring a stage
    # needs: `on` would leave out any stage g_stage_proved does not hold
    # yet and measure nothing at all. `on` is what the player gets, and
    # is how to check that the table itself says what it should.
    ap.add_argument('--mesh', default='every', choices=('on', 'every'))
    # What the other run draws with. `tiles` is the game's own
    # ground, which is the proof that the mesh draws the stage;
    # `nodepth` is the mesh with the scene not held against its
    # depth, which is how to see what that test alone changed --
    # the ground that veils a fighter, and nothing of his feet or
    # his shadow eaten by the ground they stand on.
    ap.add_argument('--against', default='tiles',
                    choices=('tiles', 'nodepth'))
    ap.add_argument('--out', default=None)
    # the stage's pieces, and not its ground (the module's words)
    ap.add_argument('--props', action='store_true')
    # the mods both runs draw with (docs/MODS.md): a mod of the stage's
    # pages is to hold the verdicts as the game's own textures do
    ap.add_argument('--mods', default=None)
    gl.add_gpu_option(ap)
    args = ap.parse_args()
    if args.out is None:
        args.out = 'build/stageprops' if args.props else 'build/stagemesh'
    global PROGRAM
    PROGRAM = remaster.PROGRAM = gl.use_gpu(args.gpu)
    problem = gl.missing_program(args.gpu)
    if problem:
        print('port_stage_prove: %s' % problem)
        return 1
    if not os.path.isfile(args.image):
        print('port_stage_prove: no %s: --image names your own image of the '
              'disc' % args.image)
        return 1
    stages = []
    for piece in args.stages.split(','):
        if '-' in piece:
            first, last = piece.split('-')
            stages += list(range(int(first), int(last) + 1))
        else:
            stages.append(int(piece))
    stages = [s for s in stages if 0 <= s < remaster.ALL_STAGES]
    os.makedirs(args.out, exist_ok=True)
    toml_path = remaster.write_tour(
        os.path.join(args.out, 'tour.toml'), args.frames, 20)
    rows = []
    for stage in stages:
        folder = os.path.join(args.out, '%02d-%s' % (stage, STAGE_NAMES[stage]))
        os.makedirs(folder, exist_ok=True)
        plan = remaster.plan_for(
            toml_path, os.path.join(folder, 'plan.txt'), stage, args.frames)
        runs = {}
        for which in ('off', 'on'):
            shots = os.path.join(folder, which)
            os.makedirs(shots, exist_ok=True)
            for old in os.listdir(shots):
                if old.endswith('.png'):
                    os.remove(os.path.join(shots, old))
            common = [gl.command(PROGRAM), '--image', args.image,
                      '--plan', plan, '--out', shots,
                      '--graphics', 'remastered',
                      '--draw-distance', args.distance,
                      '--aspect', args.aspect,
                      '--gl-scale', str(args.scale),
                      '--gl-shots', shots, '--gl-every', str(args.every),
                      '--trace']
            if args.props:
                # the ground the mesh's both ways; the pieces the
                # mesh's in one run and the game's in the other
                common += ['--stage-mesh', 'every', '--stage-props',
                           args.mesh if which == 'on' else 'off']
            elif which == 'on' or args.against == 'nodepth':
                common += ['--stage-mesh', args.mesh, '--stage-props', 'off']
            else:
                common += ['--stage-mesh', 'off', '--stage-props', 'off']
            if which == 'off' and args.against == 'nodepth':
                common += ['--stage-depth', 'off']
            if args.tall:
                common += ['--gl-tall', str(args.tall)]
            if args.mods:
                common += ['--mods', args.mods]
            log = os.path.join(folder, which + '.log')
            err = os.path.join(folder, which + '.err')
            for _ in range(3):
                seconds = remaster.run(common, log, err)
                got = remaster.numbers_of(log, err)
                if got['frames'] > 0:
                    break
                print('    (no frames; trying again)')
            got['seconds'] = seconds
            runs[which] = got
        same = same_trace(os.path.join(folder, 'off.log'),
                          os.path.join(folder, 'on.log'))
        seen = look(os.path.join(folder, 'off'), os.path.join(folder, 'on'),
                    args.pairs, folder, args.from_blank, args.mark)
        row = {'stage': stage, 'name': STAGE_NAMES[stage], 'same_trace': same,
               'off': runs['off'], 'on': runs['on'], 'pixels': seen}
        rows.append(row)
        print('%-24s trace %-5s  pictures %2d  differ %5.2f%%  '
              'patches %7d (%5.3f%%) worst %6d  holes %6d  filled %7d  '
              'vertices %6d -> %6d  %5.1f s -> %5.1f s'
              % (row['name'], 'same' if same else 'DIFFER', seen['shots'],
                 100.0 * seen['differ'] / max(seen['pixels'], 1),
                 seen['patch'],
                 100.0 * seen['patch'] / max(seen['pixels'], 1),
                 seen['worst_patch'], seen['holes'], seen['filled'],
                 runs['off']['vertices_mean'], runs['on']['vertices_mean'],
                 runs['off']['seconds'], runs['on']['seconds']))
        sys.stdout.flush()
    table = os.path.join(args.out, 'measured.tsv')
    with open(table, 'w', encoding='utf-8', newline='\n') as f:
        f.write('stage\tname\tsame_trace\tpictures\tpixels\tdiffer\tfar\t'
                'holes\tfilled\tworst\tvertices_off\tvertices_on\t'
                'seconds_off\tseconds_on\n')
        for row in rows:
            seen = row['pixels']
            f.write('%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%.1f\n'
                    % (row['stage'], row['name'], 1 if row['same_trace'] else 0,
                       seen['shots'], seen['pixels'], seen['differ'],
                       seen['far'], seen['holes'], seen['filled'],
                       seen['worst'], row['off']['vertices_mean'],
                       row['on']['vertices_mean'], row['off']['seconds'],
                       row['on']['seconds']))
    said = write_verdict(os.path.join(args.out, 'verdict.txt'), rows,
                         args.props)
    print()
    for row in rows:
        why = verdict_of(row)
        print('%-24s %s' % (row['name'],
                            'the mesh' if why is None
                            else 'THE TILES: ' + why))
    print()
    print('port_stage_prove: %d stages; %s; %s' % (len(rows), table, said))
    return 0


if __name__ == '__main__':
    sys.exit(main())
