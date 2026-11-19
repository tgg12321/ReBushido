"""The custom arenas' tools (docs/MODS.md, "Custom arenas"): the ground
file's reader and writer (tools/stage_ground.py), the game's cast in Python
(tools/ground_cast.py), the compiler (tools/arena_compile.py) on the
sample arena's source, and the validator's checks of the file
(tools/arena_validate.py), which must pass on the sample and fail on the
sample with a hole made in it.

No C here, and nothing of the game's disc: the ground is made from the
sample's source, which is the repository's own."""
import contextlib
import io
import os

import arena_compile
import arena_validate
import ground_cast
import stage_ground

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SAMPLE = os.path.join(ROOT, 'tools', 'arenas', 'courtyard')


def floor_face(cell_x, cell_z, y, surface):
    """A level face over a whole cell and 250 past it, as the compiler
    writes one."""
    corner = (cell_x * 2000 - 32000, cell_z * 2000 - 32000)
    points = [[corner[0] - 250, y, corner[1] - 250], [corner[0] + 2250, y, corner[1] - 250],
              [corner[0] + 2250, y, corner[1] + 2250], [corner[0] - 250, y, corner[1] + 2250]]
    # counter-clockwise about the normal up (-y)
    points.reverse()
    return arena_compile.collision_face(points, [0.0, -1.0, 0.0], surface, cell_x, cell_z)


def validate(path):
    """(failed, what it printed) of the validator's checks of the file."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = arena_validate.main(['arena_validate', path])
    return code, out.getvalue()


def run(checks):
    # ---- the cast, on a grid of two cells ----
    face = floor_face(16, 16, 0, ground_cast.FIRM)
    checks.true('a level face is made', face is not None)
    grid = ground_cast.Grid({'cells': {16 * 32 + 16: [face],
                                       16 * 32 + 17: [floor_face(17, 16, -500, ground_cast.SAND)]}})
    hit = grid.cast((1000, -300, 1000), (1000, 300, 1000))
    checks.equal('a cast down meets the floor at', hit and (hit[0], hit[1], hit[2]),
                 ((1000, 0, 1000), (0, -4096, 0), ground_cast.FIRM))
    checks.equal('a cast up through it meets nothing (a face has one side)',
                 grid.cast((1000, 300, 1000), (1000, -300, 1000)), None)
    checks.equal('the next cell\'s floor, higher, and sand', grid.floor_under(3000, 1000, -2000),
                 (-500, (0, -4096, 0), ground_cast.SAND))
    checks.equal('no floor past the grid', grid.floor_under(-40000, 0, -2000, 4000), None)
    checks.equal('none in an empty cell', grid.floor_under(9000, 9000, -2000, 4000), None)
    # a move at the feet from the first cell into the second, whose floor
    # is 500 higher: it passes under that face, behind it, and a face is
    # met from its front alone
    walk = grid.cast((1500, -100, 1000), (2600, -100, 1000))
    checks.equal('a walk under a higher face meets nothing', walk, None)

    # ---- the compiler on the sample ----
    said = []
    data = arena_compile.compile_arena(SAMPLE, report=said.append)
    lines, ground_bytes = arena_compile.read_arena(data)
    checks.equal('the sample stands in for', lines.get('stands_in_for'), ['25'])
    checks.equal('its first pair of starts', lines.get('start1'), ['-2000 0 0 2000 0 0'])
    checks.true('the compiler said what it made', bool(said) and 'tiles' in said[-1])
    ground = stage_ground.read(ground_bytes)
    checks.equal('its sections', [s['kind'] for s in ground['sections']],
                 ['meshes', 'collision', 'unused', 'obstacles', 'cells', 'textures'])
    checks.true('its ground is written again the same', stage_ground.write(ground) == ground_bytes)
    checks.equal('its ground is whole sectors', len(ground_bytes) % 2048, 0)
    checks.equal('the sample compiled twice is the same', arena_compile.compile_arena(SAMPLE, report=said.append),
                 data)
    cells = stage_ground.section(ground, 'cells')
    checks.true('a tile a cell of the floor at least', len(cells['tiles']) >= 56)
    checks.equal('two posts a blade stops at', len(stage_ground.section(ground, 'obstacles')['records']), 2)
    textures = stage_ground.section(ground, 'textures')
    checks.equal('five textures, each with two smaller copies', len(textures['tims']), 15)

    # ---- the validator ----
    folder = os.path.join(ROOT, 'build', 'tests', 'arena')
    os.makedirs(folder, exist_ok=True)
    good = os.path.join(folder, 'courtyard.arena')
    with open(good, 'wb') as f:
        f.write(data)
    code, text = validate(good)
    checks.equal('the validator passes the sample', code, 0)
    if code:
        checks.true(text, False)
    # a hole: the floor's faces gone from the two cells about the first
    # start, (-2000, 0, 0)
    collision = stage_ground.section(ground, 'collision')
    for cell in (16 * 32 + 14, 16 * 32 + 15):
        collision['cells'][cell] = [f for f in collision['cells'][cell] if f['normal'][1] > -8192]
    holed = stage_ground.write(ground, sector=2048, align=True)
    text_part = data[:len(data) - len(ground_bytes)]
    text_part = text_part.replace(b'ground = %d\n' % len(ground_bytes), b'ground = %d\n' % len(holed))
    bad = os.path.join(folder, 'holed.arena')
    with open(bad, 'wb') as f:
        f.write(text_part + holed)
    code, text = validate(bad)
    checks.equal('the validator fails the sample with a hole', code, 1)
    checks.true('and says where a fighter falls', 'no floor under it' in text)
    checks.true('and that a start has no floor', 'start1 (-2000, 0, 0) stands on a level floor' in text
                and 'FAIL start1 (-2000, 0, 0)' in text)
