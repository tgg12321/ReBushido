#!/usr/bin/env python3
"""Find the pointers the 64-bit port makes from a number that is not a
32-bit address (docs/PORT.md, "64 bits").

    python3 tools/port_casts.py            list them
    python3 tools/port_casts.py --check    the same, and exit 1 if there is one

`make port` runs `--check` when it builds the 64-bit program.

Everything the game can point at is below 4 GB in the port, and the game
keeps addresses in 32-bit numbers. Where such a number is made a pointer it
goes through ADDR() (include/common.h), which takes its low 32 bits with
nothing above them. The compiler itself refuses a cast from a 32-bit number
to a pointer that has no ADDR(). What it does not refuse is a cast from a
number that is already 64 bits wide, and C makes one without being asked:

    file = (SelectFile *)(memory + 2 * sizeof(PrimPool));

`sizeof` is 64 bits wide in a 64-bit program, so `memory`, an `s32`, is
widened with its sign before the sum, and an address of the PlayStation's
RAM, whose top bit is set, comes out with all 32 bits above it set too. A
difference of two pointers is 64 bits wide in the same way.

So this reads what the compiler made of every file of the game (its SSA
form, in which every value has its type written) and reports each cast to a
pointer whose operand is a 64-bit number, unless that number was made by
widening an unsigned 32-bit one, which is what ADDR() does. The port's own
files are not read: they keep no address of the game's in a 64-bit number
but through the same macro, and a pointer of theirs may be anywhere.

One more thing no compiler reports is a negative number written into the
source and cast to a pointer, `(SpriteSheet *)-1`: the mark some lists end
with. In a file it is the 32-bit word 0xFFFFFFFF, and a 64-bit pointer made
of -1 is not that. The source is read for those, and each wants ADDR() too.
"""
import concurrent.futures
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_build  # noqa: E402

WIDE = ('long unsigned int', 'long int', 'PortAddress', 'PortWord', 'sizetype', 'ssizetype')
NARROW_UNSIGNED = ('unsigned int', 'u32')
PLACE = re.compile(r'\[[^\]]*\] ')
FIRST_PLACE = re.compile(r'\[([^\]:]+):(\d+):\d+')
DECLARATION = re.compile(r'^  (\S.*?)\s*(\**)\s*([A-Za-z_][\w.]*);$')
NAME = r'([A-Za-z_][\w.]*)(?:\(D\))?'
CAST = re.compile(r'^' + NAME + r' = \(([^()]*(?:\([^()]*\)[^()]*)*)\) ' + NAME + r';$')
CAST_OF_PLACE = re.compile(r'^' + NAME + r' = \(([^()]*)\) &.*;$')
CONSTANT = re.compile(r'^' + NAME + r' = (-?[0-9]+);$')
ADDRESS_MAX = 0xFFFFFFFF
COPY = re.compile(r'^' + NAME + r' = ' + NAME + r';$')
SUM = re.compile(r'^' + NAME + r' = (\S+) ([-+]) (\S+);$')
PHI = re.compile(r'^# ' + NAME + r' = PHI <(.*)>$')
NUMBER = re.compile(r'^-?\d+$')


def kind_of(name, kinds):
    """The type a name of the dump was declared with: `_12`, `memory_4`
    (the fourth value of `memory`), `g_free.3_7` (a copy of a global)."""
    name = name.replace('(D)', '')
    if name in kinds:
        return kinds[name]
    return kinds.get(re.sub(r'_\d+$', '', name))


def read_function(lines, kinds):
    """The casts to a pointer in one function's lines that are not from an
    address: (file, line, the pointer's type).

    A 64-bit number is an address if it is an unsigned 32-bit number
    widened, a pointer made a number, a copy of an address, or an address
    with something added (an index: the compiler turns `((s32 *)ADDR(x))[i]`
    into the sum of the two before the cast)."""
    statements = []
    for line in lines:
        place = FIRST_PLACE.search(line)
        text = PLACE.sub('', line).strip()
        m = DECLARATION.match(line)
        if m and not place:
            kinds[m.group(3)] = 'pointer' if m.group(2) or '*' in m.group(1) else m.group(1).strip()
            continue
        statements.append((place.groups() if place else None, text))

    def is_address(word, addresses):
        word = word.replace('(D)', '')
        return NUMBER.match(word) is not None or word in addresses

    addresses = set()
    while True:
        before = len(addresses)
        for place, text in statements:
            m = CAST.match(text)
            if m:
                target, kind, operand = m.group(1), m.group(2).strip(), m.group(3)
                if kind in WIDE and (kind_of(operand, kinds) in NARROW_UNSIGNED + ('pointer',)
                                     or operand in addresses):
                    addresses.add(target)
                continue
            m = CAST_OF_PLACE.match(text)
            if m:
                # the address of a variable or a member, made a number
                if m.group(2).strip() in WIDE:
                    addresses.add(m.group(1))
                continue
            m = CONSTANT.match(text)
            if m:
                # a number written into the source: the scratchpad's address
                if 0 <= int(m.group(2)) <= ADDRESS_MAX:
                    addresses.add(m.group(1))
                continue
            m = COPY.match(text)
            if m:
                if m.group(2) in addresses:
                    addresses.add(m.group(1))
                continue
            m = SUM.match(text)
            if m:
                target, first, sign, second = m.groups()
                if first in addresses or (sign == '+' and second in addresses):
                    addresses.add(target)
                continue
            m = PHI.match(text)
            if m:
                words = [word.split('(')[0] for word in m.group(2).split(', ')]
                if all(is_address(word, addresses) for word in words):
                    addresses.add(m.group(1))
        if len(addresses) == before:
            break

    found = []
    for place, text in statements:
        m = CAST.match(text)
        if not m or '*' not in m.group(2) or place is None:
            continue
        operand = m.group(3)
        if kind_of(operand, kinds) in WIDE and operand not in addresses:
            found.append((place[0], int(place[1]), m.group(2).strip()))
    return found


def parameters(line, kinds):
    """The types of a function's parameters, from its first line."""
    inside = line[line.find('(') + 1:line.rfind(')')]
    for piece in inside.split(','):
        m = re.match(r'^\s*(.*?)\s*(\**)\s*([A-Za-z_]\w*)$', piece)
        if m:
            kinds[m.group(3)] = 'pointer' if m.group(2) else m.group(1)


def read_dump(text):
    found = []
    for piece in re.split(r'^;; Function ', text, flags=re.M)[1:]:
        lines = piece.split('\n')
        # the line with the parameters is the first that ends in `)`
        head = next((line for line in lines[1:6] if line.endswith(')')), '')
        kinds = {}
        parameters(head, kinds)
        found += read_function(lines[1:], kinds)
    return found


def dump_one(job):
    path, flags, folder = job
    out = folder + '/' + path.replace('/', '_') + '.ssa'
    cmd = ['gcc'] + flags + ['-fdump-tree-ssa-lineno=' + out, '-x', 'c', '-S', '-', '-o', '/dev/null']
    proc = subprocess.run(cmd, input=port_build.modern.modern_source(path), capture_output=True, text=True,
                          env=port_build.environment(), encoding='utf-8', errors='replace')
    if proc.returncode != 0:
        return path, None, proc.stderr
    if not os.path.exists(out):
        return path, [], ''     # a file with no function
    with open(out, encoding='utf-8', errors='replace') as f:
        return path, read_dump(f.read()), ''


NEGATIVE = re.compile(r'\(\s*((?:const |struct |unsigned )*\w+\s*\*+)\s*\)\s*\(?\s*-\s*\d')


def negative_constants(path):
    """The casts of a negative number to a pointer in a file's source."""
    found = []
    with open(path, encoding='utf-8') as f:
        for number, line in enumerate(f, 1):
            m = NEGATIVE.search(line)
            if m:
                found.append((path, number, m.group(1) + ', of a negative number'))
    return found


def find():
    """The casts, as (file, line, the pointer's type), sorted."""
    port_build.set_bits(64)
    folder = port_build.OUT_DIR + '/casts'
    os.makedirs(folder, exist_ok=True)
    port_build.host_tests.OUT_DIR = port_build.OUT_DIR
    port_build.host_tests.write_headers(True)
    flags = (port_build.GAME_FLAGS + port_build.host_tests.include_flags(True) + port_build.modern.DEFS
             + port_build.host_tests.REBUSHIDO_DEFS + port_build.MOVOVL_INCLUDES
             + ['-DRB_GIT_HASH="casts"'])
    jobs = [(path, flags + mine, folder) for path, own, mine in port_build.sources() if not own]
    found = set()
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for path, casts, problem in pool.map(dump_one, jobs):
            if casts is None:
                sys.exit('port_casts.py: %s does not compile:\n%s' % (path, problem[:2000]))
            found |= set(casts)
            found |= set(negative_constants(path))
    return sorted(found)


def main():
    os.chdir(ROOT)
    port_build.set_bits(64)
    problem = port_build.check_host()
    if problem:
        print('port_casts: skipped: %s' % problem)
        return 0
    found = find()
    for where, line, kind in found:
        print('%s:%d: a pointer (%s) made of a 64-bit number that is not an address: its operand wants ADDR()'
              % (where, line, kind))
    print('port_casts: %d pointers made of a 64-bit number that is not a 32-bit address' % len(found))
    if found and '--check' in sys.argv[1:]:
        print('FAILED: port_casts')
        return 1
    print('OK: port_casts')
    return 0


if __name__ == '__main__':
    sys.exit(main())
