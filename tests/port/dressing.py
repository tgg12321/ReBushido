"""The dressing files the program ships, read by the C and by the check.

tests/port/dressing.c reads each file of src/port/mods/dressing/dressing/ as
the program does, checks that every line of it is used, and prints its
blocks; here tools/port_dressing.py must read the same blocks from each
(the check's reading is the program's), and the check itself must pass:
every block inside its stage's bounds, and, where the stages' ground files
are there to walk (tools/port_dressing.py, `--grounds`; the disc's), nothing
that looks solid where a fighter can walk.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FOLDER = os.path.join(ROOT, 'src', 'port', 'mods', 'dressing', 'dressing')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import port_dressing  # noqa: E402  (tools/port_dressing.py)


def shipped():
    files = []
    for folder, _, names in os.walk(FOLDER):
        files += [os.path.relpath(os.path.join(folder, n), ROOT).replace(os.sep, '/')
                  for n in names if n.lower().endswith('.txt')]
    return sorted(files)


def arguments():
    return shipped()


def check(lines, results):
    files = shipped()
    results.true('the program ships dressing files in %s' % FOLDER, len(files) > 0)
    seen = {}
    for line in lines:
        words = line.split()
        if not words or words[0] != 'block':
            results.true('a line nothing reads: %s' % line, False)
            continue
        seen.setdefault(words[1], []).append(tuple(words[2:]))
    for path in files:
        with open(os.path.join(ROOT, path), encoding='utf-8') as f:
            blocks, problems = port_dressing.parse(f.read())
        results.true('%s: the check reads every line (%s)' % (path, problems), not problems)
        want = [(b['kind'], b['name'], str(b['count']), str(int(b['solid'])), str(len(b['areas'])))
                for b in blocks]
        results.true('%s: the C and the check read the same blocks' % path, seen.get(path) == want)
    # the check itself: the bounds always, the walk where the grounds are
    done = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'port_dressing.py'), 'check'],
                          capture_output=True, text=True)
    results.true('tools/port_dressing.py check passes:\n%s' % done.stdout[-3000:], done.returncode == 0)
