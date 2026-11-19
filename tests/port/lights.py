"""The light files the program ships, held to their stages' bounds.

tests/port/lights.c reads each file of src/port/mods/lights/lights/ as the
program does, checks that every line of it is used, and prints its lights;
here each light that is placed on the stage itself must lie inside its
stage's bounds (tools/port_lights.py, BOUNDS: measured from the stages'
meshes), and a light fastened to something else must be near it.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FOLDER = os.path.join(ROOT, 'src', 'port', 'mods', 'lights', 'lights')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import port_lights  # noqa: E402  (tools/port_lights.py)

# PORT_LIGHT_ON_... (include/port_lights.h)
ON_STAGE = 0
# how far from what it is fastened to a light may be put
NEAR = 3000


def shipped():
    files = []
    for folder, _, names in os.walk(FOLDER):
        files += [os.path.relpath(os.path.join(folder, n), ROOT).replace(os.sep, '/')
                  for n in names if n.lower().endswith('.txt')]
    return sorted(files)


def arguments():
    return shipped()


class Holds:
    """support.Checks, the condition first."""

    def __init__(self, checks):
        self.checks = checks

    def check(self, condition, what):
        self.checks.true(what, condition)


def check(lines, results):
    checks = Holds(results)
    files = shipped()
    checks.check(len(files) > 0, 'the program ships light files in %s' % FOLDER)
    seen = set()
    for line in lines:
        words = line.split()
        if not words or words[0] != 'light':
            checks.check(False, 'a line nothing reads: %s' % line)
            continue
        path, name = words[1], words[2]
        on, index, x, y, z, radius = (int(w) for w in words[3:9])
        key = os.path.splitext(os.path.basename(path))[0].lower()
        seen.add(path)
        checks.check(key.startswith('stage_') and key[6:].isdigit(),
                     '%s is named stage_NN.txt' % path)
        if not key[6:].isdigit():
            continue
        stage = int(key[6:])
        checks.check(0 <= stage < port_lights.ALL_STAGES, '%s: stage %d is one of the game\'s' % (path, stage))
        checks.check(0 < radius <= 20000, '%s: %s reaches %d, not more than 20,000' % (path, name, radius))
        if on != ON_STAGE:
            checks.check(max(abs(x), abs(y), abs(z)) <= NEAR,
                         '%s: %s is within %d of what it is fastened to' % (path, name, NEAR))
            continue
        bounds = port_lights.BOUNDS.get(stage)
        checks.check(bounds is not None, '%s: stage %d\'s bounds are in tools/port_lights.py' % (path, stage))
        if bounds is None:
            continue
        least, most = bounds
        inside = all(least[i] <= (x, y, z)[i] <= most[i] for i in range(3))
        checks.check(inside, '%s: %s at %d %d %d is inside stage %d (%s to %s)' % (
            path, name, x, y, z, stage, least, most))
    checks.check(seen == set(files), 'every shipped file has lights: %s' % sorted(set(files) - seen))
