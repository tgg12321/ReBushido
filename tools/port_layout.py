#!/usr/bin/env python3
"""The layout of every structure in the three builds: the 32-bit port, the
64-bit port and the PlayStation program (docs/PORT.md, "64 bits").

    python3 tools/port_layout.py            the structures that are not laid out the same in all
                                            three, each with whether config/port_layout.txt
                                            accounts for it
    python3 tools/port_layout.py --check    the same, and exit 1 if one is not accounted for, or
                                            the list names one that no longer differs
    python3 tools/port_layout.py --table    every structure: its size and its members' places in
                                            each build

`make port` runs `--check` after it has built both programs.

A pointer is 8 bytes in the 64-bit port and 4 in the others, so a structure
with a pointer in it grows there and what follows the pointer moves. Where
the layout is not the port's to choose (a file read from the disc, the
scratchpad, memory another structure is laid over) the member is a PTR32
slot (include/common.h) and nothing moves. Where the structure is only ever
the program's own it may grow, and config/port_layout.txt lists it with the
reason: `NAME  why it may grow`, a line each, `#` for a comment.
The port's own structures (src/port/) are not the game's: theirs are the
names that begin Port or Linux, and they need no line.

Where the sizes come from:

  - the two ports: the debug information of the programs `make port`
    built (build/port/rebushido and the other size's, beside it), read
    with binutils' readelf. A structure is in there if some file uses it;
  - the PlayStation: a probe, a file of `sizeof` and member offsets for
    every structure the headers declare, compiled by the game's own cpp and
    cc1 into assembly whose `.word` lines are the numbers. Where that
    compiler is not built the PlayStation's column is left out, which this
    says; a structure declared in a .c file is not in the probe either.

port_build.py uses this for the names it writes beside the 64-bit program:
`console_runs` says, for a variable whose type grew, which bytes of the
port's are which bytes of the PlayStation's, so that the play tests can
compare such a variable with the emulator's (tools/playtest.py).
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

ACCOUNTED = 'config/port_layout.txt'
OWN_PREFIXES = ('Port', 'Linux')
PROBE_DIR = 'build/port/layout'
# The game's own compiler, as the Makefile runs it for a file of the game.
CPP = ['mipsel-linux-gnu-cpp', '-Iinclude', '-nostdinc', '-undef', '-lang-c', '-fno-builtin', '-Dmips',
       '-D__GNUC__=2', '-D__OPTIMIZE__', '-D__mips__', '-D__mips', '-Dpsx', '-D__psx__', '-D__psx', '-D_PSYQ',
       '-D__EXTENSIONS__', '-D_MIPSEL', '-D_LANGUAGE_C', '-DLANGUAGE_C', '-DREBUSHIDO=1']
CC1 = ['tools/gcc-2.7.2/build/cc1', '-O2', '-G8', '-funsigned-char', '-quiet', '-mcpu=3000', '-mips1',
       '-mno-abicalls', '-fno-builtin', '-mel', '-msoft-float', '-w']

DIE = re.compile(r'^ <(\d+)><([0-9a-f]+)>: Abbrev Number: \d+ \((DW_TAG_\w+)\)')
ATTRIBUTE = re.compile(r'^\s+<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$')
# The attributes read: a name, a type, a size, a member's place and bits,
# an array's bound.
WANTED = {'DW_AT_name', 'DW_AT_type', 'DW_AT_byte_size', 'DW_AT_data_member_location', 'DW_AT_bit_size',
          'DW_AT_upper_bound', 'DW_AT_count', 'DW_AT_specification'}
STRUCTURES = ('DW_TAG_structure_type', 'DW_TAG_union_type')
TRANSPARENT = ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type')


class Debug:
    """A program's debug information, as far as types and variables go."""

    def __init__(self, path):
        text = subprocess.run(['readelf', '--debug-dump=info', path], capture_output=True, text=True,
                              errors='replace', env=dict(os.environ, LC_ALL='C')).stdout
        self.dies = {}
        stack = []
        die = None
        for line in text.split('\n'):
            if line.startswith(' <'):
                m = DIE.match(line)
                if m:
                    level = int(m.group(1))
                    die = {'tag': m.group(3), 'at': int(m.group(2), 16), 'kids': [], 'level': level}
                    self.dies[die['at']] = die
                    del stack[level:]
                    if level > 0 and stack:
                        stack[level - 1]['kids'].append(die)
                    stack.append(die)
                continue
            # of a die's lines, only the few this reads (most of a
            # program's are its functions')
            at = line.find('DW_AT_')
            if at < 0 or die is None or line[at:at + 28].split()[0].rstrip(':') not in WANTED:
                continue
            m = ATTRIBUTE.match(line)
            if m:
                value = m.group(2).strip()
                if value.startswith('(') and '):' in value:
                    value = value.split('):', 1)[1].strip()
                die[m.group(1)] = value

    def kind(self, die):
        """The type a die names, or None."""
        value = die.get('DW_AT_type')
        return self.dies.get(int(value.strip('<>'), 16)) if value else None

    def plain(self, die):
        """A type with its typedefs and qualifiers taken off."""
        while die is not None and die['tag'] in TRANSPARENT:
            die = self.kind(die)
        return die

    def counts(self, die):
        """An array type's dimensions."""
        found = []
        for kid in die['kids']:
            if kid['tag'] == 'DW_TAG_subrange_type':
                bound = kid.get('DW_AT_upper_bound', kid.get('DW_AT_count', ''))
                if 'DW_AT_count' in kid:
                    found.append(int(bound, 0))
                else:
                    found.append(int(bound, 0) + 1 if re.fullmatch(r'\d+|0x[0-9a-f]+', bound) else 0)
        return found

    def size(self, die):
        die = self.plain(die)
        if die is None:
            return 0
        if die['tag'] == 'DW_TAG_array_type':
            total = self.size(self.kind(die))
            for count in self.counts(die):
                total *= count
            return total
        if 'DW_AT_byte_size' in die:
            return int(die['DW_AT_byte_size'], 0)
        return 0

    def text(self, die):
        """A type as the source would write it, near enough."""
        if die is None:
            return 'void'
        tag = die['tag']
        if tag == 'DW_TAG_pointer_type':
            return self.text(self.kind(die)) + ' *'
        if tag in ('DW_TAG_const_type', 'DW_TAG_volatile_type'):
            return self.text(self.kind(die))
        if tag == 'DW_TAG_array_type':
            return self.text(self.kind(die)) + ''.join('[%d]' % count for count in self.counts(die))
        if tag == 'DW_TAG_subroutine_type':
            return 'function'
        return die.get('DW_AT_name', tag[7:-5])

    def is_slot(self, die):
        """Whether a union is a PTR32 slot: an address and the type it
        points at."""
        return die['tag'] == 'DW_TAG_union_type' and \
            [kid.get('DW_AT_name') for kid in die['kids'] if kid['tag'] == 'DW_TAG_member'] == ['address', 'target']

    def members(self, die, base=0, prefix=''):
        """(offset, name, type's text, bytes) of a structure's members; a
        member that is a structure with no name of its own is opened."""
        rows = []
        for kid in die['kids']:
            if kid['tag'] != 'DW_TAG_member':
                continue
            m = re.search(r'\d+', kid.get('DW_AT_data_member_location', '0'))
            offset = base + (int(m.group(0)) if m else 0)
            name = kid.get('DW_AT_name', '')
            kind = self.kind(kid)
            inner = self.plain(kind)
            if 'DW_AT_bit_size' in kid:
                name += ':' + kid['DW_AT_bit_size']
            unnamed = inner is not None and inner['tag'] in STRUCTURES and kind['tag'] not in TRANSPARENT and \
                'DW_AT_name' not in inner and not self.is_slot(inner)
            if unnamed:
                rows += self.members(inner, offset, prefix + name + '.' if name else prefix)
            else:
                text = 'slot' if inner is not None and self.is_slot(inner) and 'DW_AT_name' not in kind \
                    else self.text(kind)
                rows.append((offset, prefix + name, text, self.size(kind)))
        return rows

    def structures(self):
        """{name: (bytes, members)} of every structure with a name: the
        typedef's, or `struct NAME`. Two different ones of one name (two
        files' own) are NAME and NAME#2."""
        found = {}
        for die in self.dies.values():
            if die['tag'] == 'DW_TAG_typedef':
                inner, name = self.plain(die), die.get('DW_AT_name')
            elif die['tag'] in STRUCTURES and 'DW_AT_name' in die:
                inner, name = die, die['DW_AT_name']
            else:
                continue
            if inner is None or inner['tag'] not in STRUCTURES or 'DW_AT_byte_size' not in inner or not name:
                continue
            if self.is_slot(inner):
                continue
            entry = (int(inner['DW_AT_byte_size'], 0), tuple(self.members(inner)))
            number = 1
            key = name
            while key in found and found[key] != entry:
                number += 1
                key = '%s#%d' % (name, number)
            found[key] = entry
        return found

    def leaves(self, die, base=0):
        """A type as the run of plain values it is made of: (offset, bytes,
        whether it is a pointer). A union is its first member."""
        die = self.plain(die)
        if die is None:
            return []
        tag = die['tag']
        if tag == 'DW_TAG_array_type':
            element = self.kind(die)
            step = self.size(element)
            total = 1
            for count in self.counts(die):
                total *= count
            one = self.leaves(element)
            return [(base + i * step + offset, size, pointer) for i in range(total) for offset, size, pointer in one]
        if tag == 'DW_TAG_pointer_type' or self.is_slot(die):
            return [(base, self.size(die), True)]
        if tag in STRUCTURES:
            found = []
            for kid in die['kids']:
                if kid['tag'] != 'DW_TAG_member' or 'DW_AT_bit_size' in kid:
                    continue
                m = re.search(r'\d+', kid.get('DW_AT_data_member_location', '0'))
                found += self.leaves(self.kind(kid), base + (int(m.group(0)) if m else 0))
                if tag == 'DW_TAG_union_type':
                    break
            if not found and self.size(die):
                found = [(base, self.size(die), False)]    # bits only
            return found
        return [(base, self.size(die), False)]

    def variables(self):
        """{name: its type} of the program's variables that are not a
        function's own."""
        found = {}
        for die in self.dies.values():
            if die['tag'] != 'DW_TAG_variable' or die['level'] != 1:
                continue
            if 'DW_AT_specification' in die:
                # where it is defined: the name is with its declaration, and
                # the type here is the whole one (an array's length)
                declared = self.dies.get(int(die['DW_AT_specification'].strip('<>'), 16), {})
                if 'DW_AT_name' in declared:
                    found[declared['DW_AT_name']] = self.kind(die) or self.kind(declared)
            elif 'DW_AT_name' in die and 'DW_AT_type' in die:
                found.setdefault(die['DW_AT_name'], self.kind(die))
        return found


# ---- the PlayStation's column ----

def header_structures():
    """The names of the structures the game's headers declare by typedef."""
    names = []
    for folder in ('include', 'include/psxsdk'):
        for header in sorted(os.listdir(folder)):
            if not header.endswith('.h') or header.startswith('port_'):
                continue
            with open(os.path.join(folder, header), encoding='utf-8') as f:
                names += re.findall(r'^\} (\w+);', f.read(), re.M)
    return names


def probe_source(structures, names):
    """A file whose one table holds the size of each structure and the
    offset of each of its members; and what each number of it is."""
    rows = ['#include "bb2.h"', '#define AT(type, member) ((int)&((type *)0)->member)',
            'const int port_layout_probe[] = {']
    what = []
    for name in names:
        if name not in structures:
            continue
        rows.append('    sizeof(%s),' % name)
        what.append((name, None))
        for offset, member, kind, size in structures[name][1]:
            if ':' in member or not member:
                continue    # bits have no address
            rows.append('    AT(%s, %s),' % (name, member))
            what.append((name, member))
    rows += ['    -1};', '']
    return '\n'.join(rows), what


def playstation_structures(structures):
    """{name: (bytes, {member: offset})} by the game's own compiler, or
    None and why not."""
    if not os.path.exists(CC1[0]):
        return None, 'no %s (tools/build_cc1.sh builds it)' % CC1[0]
    os.makedirs(PROBE_DIR, exist_ok=True)
    # the structures the PlayStation build has: the port's own are behind
    # its switch
    try:
        seen = subprocess.run(CPP + ['-'], input='#include "bb2.h"\n', capture_output=True, text=True,
                              errors='replace')
    except OSError as error:
        return None, 'no %s (%s)' % (CPP[0], error)
    there = set(re.findall(r'^\} (\w+);', seen.stdout, re.M))
    source, what = probe_source(structures, [name for name in header_structures() if name in there])
    with open(PROBE_DIR + '/probe.c', 'w', encoding='utf-8', newline='\n') as f:
        f.write(source)
    try:
        cpp = subprocess.run(CPP + [PROBE_DIR + '/probe.c'], capture_output=True, text=True, errors='replace')
    except OSError as error:
        return None, 'no %s (%s)' % (CPP[0], error)
    if cpp.returncode != 0:
        return None, 'the probe does not go through cpp: %s' % cpp.stderr.strip().split('\n')[-1]
    cc1 = subprocess.run(CC1, input=cpp.stdout, capture_output=True, text=True, errors='replace')
    if cc1.returncode != 0:
        return None, 'the probe does not compile: %s' % (cc1.stderr.strip().split('\n') or ['?'])[0]
    at = cc1.stdout.find('port_layout_probe:')
    numbers = [int(n, 0) for n in re.findall(r'^\s*\.word\s+(-?\w+)', cc1.stdout[at:], re.M)]
    if at < 0 or len(numbers) < len(what) + 1 or numbers[len(what)] != -1:
        return None, "the probe's table is not in what the compiler wrote"
    found = {}
    for (name, member), number in zip(what, numbers):
        if member is None:
            found[name] = (number, {})
        else:
            found[name][1][member] = number
    return found, ''


# ---- the three together ----

def program_of(bits):
    import port_build
    folder = 'build/port' if bits == port_build.DEFAULT_BITS else 'build/port%d' % bits
    return folder + '/rebushido'


def accounted():
    """{name: why} of config/port_layout.txt."""
    found = {}
    with open(ACCOUNTED, encoding='utf-8') as f:
        for line in f:
            line = line.split('#')[0].strip()
            if line:
                name, _, why = line.partition(' ')
                found[name] = why.strip()
    return found


def compare():
    """(rows, problems, notes): a row for each structure that differs
    between two builds, as (name, sizes, the first member that moved,
    accounted for or not)."""
    notes = []
    missing = [program_of(bits) for bits in (32, 64) if not os.path.exists(program_of(bits))]
    if missing:
        return None, ['no %s: `make port` builds both programs' % ' and no '.join(missing)], notes
    small, large = Debug(program_of(32)).structures(), Debug(program_of(64)).structures()
    console, why = playstation_structures(small)
    if console is None:
        notes.append("the PlayStation's column is left out: %s" % why)
    listed = accounted()
    rows, problems = [], []
    for name in sorted(set(small) | set(large)):
        own = name.startswith(OWN_PREFIXES)
        if name not in small or name not in large:
            if not own and not name.startswith('__'):
                notes.append('%s is in only one of the two programs' % name)
            continue
        moved = []
        if small[name] != large[name]:
            pairs = zip(small[name][1], large[name][1])
            first = next((a[1] for a, b in pairs if a[0] != b[0]), 'its end')
            moved.append('%d bytes in the 32-bit port and %d in the 64-bit, from `%s` on'
                         % (small[name][0], large[name][0], first))
        base = name.split('#')[0]
        if console is not None and base in console and '#' not in name:
            size, places = console[base]
            wrong = [member for offset, member, kind, bytes_ in small[name][1]
                     if member in places and places[member] != offset]
            if size != small[name][0] or wrong:
                moved.append('%d bytes on the PlayStation and %d in the 32-bit port%s'
                             % (size, small[name][0], ', from `%s` on' % wrong[0] if wrong else ''))
        if not moved:
            if base in listed and not own and not any(k.split('#')[0] == base and small.get(k) != large.get(k)
                                                      for k in small):
                problems.append('%s is in %s and is laid out the same in every build: take the line out'
                                % (base, ACCOUNTED))
            continue
        ok = own or base in listed
        rows.append((name, '; '.join(moved), 'the port\'s own' if own else listed.get(base, '')))
        if not ok:
            problems.append('%s: %s, and %s does not say why it may' % (name, '; '.join(moved), ACCOUNTED))
    for name in listed:
        if not any(key.split('#')[0] == name for key in small):
            problems.append('%s is in %s and no file of the port uses such a structure' % (name, ACCOUNTED))
    return rows, problems, notes


def table():
    small, large = Debug(program_of(32)).structures(), Debug(program_of(64)).structures()
    console, why = playstation_structures(small)
    for name in sorted(small):
        there = console.get(name) if console else None
        print('%s: %s, %d, %s bytes (PlayStation, 32-bit port, 64-bit port)'
              % (name, there[0] if there else '-', small[name][0], large[name][0] if name in large else '-'))
        other = {member: offset for offset, member, kind, size in large.get(name, (0, ()))[1]}
        for offset, member, kind, size in small[name][1]:
            print('    %-5s %5d %-5s %s : %s' % (there[1].get(member, '-') if there else '-', offset,
                                                other.get(member, '-'), member, kind))


# ---- a variable of the 64-bit port as the PlayStation has it ----

def probe_debug(bits):
    """The debug information of one file that includes every header,
    compiled for `bits`-bit pointers: every structure the headers declare,
    used or not."""
    import port_build
    port_build.set_bits(port_build.BITS)
    port_build.host_tests.OUT_DIR = port_build.OUT_DIR
    flags = [f for f in port_build.GAME_FLAGS if f not in ('-m32', '-m64')] + ['-m%d' % bits]
    flags += port_build.host_tests.include_flags(True) + port_build.modern.DEFS
    flags += port_build.host_tests.REBUSHIDO_DEFS
    os.makedirs(PROBE_DIR, exist_ok=True)
    out = '%s/headers%d.o' % (PROBE_DIR, bits)
    proc = subprocess.run(['gcc'] + flags + ['-fno-eliminate-unused-debug-types', '-x', 'c', '-c', '-', '-o', out],
                          input='#include "bb2.h"\n', capture_output=True, text=True, errors='replace',
                          env=port_build.environment())
    if proc.returncode != 0:
        sys.exit('port_layout.py: the headers do not compile for %d bits:\n%s' % (bits, proc.stderr[:2000]))
    return Debug(out)


def console_runs(program):
    """{variable: (bytes on the PlayStation, [(offset there, offset here,
    bytes)])} for the variables of a 64-bit program whose type is not laid
    out as the PlayStation has it: which bytes of the port's variable are
    which of the console's. A pointer's four bytes there are the low four
    of its eight here. The PlayStation's layout is the 32-bit one of the
    same headers, which `--check` holds to the PlayStation's own."""
    here = Debug(program)
    there = probe_debug(32)
    kinds = {}
    for die in there.dies.values():
        if die['tag'] in ('DW_TAG_typedef',) + STRUCTURES and 'DW_AT_name' in die and die['level'] == 1:
            kinds.setdefault(die['DW_AT_name'], die)
    found = {}
    for name, kind in sorted(here.variables().items()):
        named = kind
        counts = []
        while named is not None and 'DW_AT_name' not in named and named['tag'] != 'DW_TAG_pointer_type':
            if named['tag'] == 'DW_TAG_array_type':
                counts += here.counts(named)
            named = here.kind(named)
        leaves = here.leaves(kind)
        if not any(pointer and size == 8 for offset, size, pointer in leaves):
            continue
        if named is not None and named['tag'] == 'DW_TAG_pointer_type':
            other = [(offset // 2, 4, True) for offset, size, pointer in leaves]
            total = here.size(kind) // 2
        elif named is not None and named['DW_AT_name'] in kinds:
            one = kinds[named['DW_AT_name']]
            step = there.size(one)
            copies = 1
            for count in counts:
                copies *= count
            first = there.leaves(one)
            other = [(i * step + offset, size, pointer) for i in range(copies) for offset, size, pointer in first]
            total = step * copies
        else:
            continue    # a structure of a .c file's own: not in the headers
        if len(other) != len(leaves):
            continue
        runs = []
        for (at_there, size_there, _), (at_here, size_here, _) in zip(other, leaves):
            size = min(size_there, size_here)
            if runs and runs[-1][0] + runs[-1][2] == at_there and runs[-1][1] + runs[-1][2] == at_here:
                runs[-1] = (runs[-1][0], runs[-1][1], runs[-1][2] + size)
            else:
                runs.append((at_there, at_here, size))
        found[name] = (total, runs)
    return found


def main():
    os.chdir(ROOT)
    args = sys.argv[1:]
    if '--table' in args:
        table()
        return 0
    rows, problems, notes = compare()
    if rows is None:
        print('port_layout: skipped: %s' % problems[0])
        return 1 if '--check' in args else 0
    for name, moved, why in rows:
        if '--check' not in args or not why:
            print('%-22s %s\n%22s   %s' % (name, moved, '', why or 'NOT ACCOUNTED FOR'))
    for note in notes:
        print('port_layout: %s' % note)
    own = sum(1 for row in rows if row[0].startswith(OWN_PREFIXES))
    print("port_layout: %d of the game's structures and %d of the port's own are not laid out the same in every "
          'build; %d not accounted for (%s)'
          % (len(rows) - own, own, sum(1 for row in rows if not row[2]), ACCOUNTED))
    if problems:
        for problem in problems:
            print('port_layout: ' + problem)
        if '--check' in args:
            print('FAILED: port_layout')
            return 1
    print('OK: port_layout')
    return 0


if __name__ == '__main__':
    sys.exit(main())
