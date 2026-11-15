"""The port's memory: every address written into the game's C is memory the port maps.

The native port gives the game the PlayStation's RAM and scratchpad at the
PlayStation's addresses (src/port/memory.c), and the comment at the top of
that file lists what the game reaches there by a number written into its
source. This reads the game's C for those numbers (src/main/*.c,
src/rebushido/*.c and the game's headers; not Sony's library code, which
the port does not compile where it touches the console) and holds the list
to them:

  - a number that is an address of the RAM (0x80000001 to 0x80FFFFFF) or of
    the scratchpad's region (0x1F800000 to 0x1F80FFFF) is inside what the
    port maps;
  - an address of the RAM is above the PlayStation program's own place:
    the port's variables are not there, so nothing may be reached below it;
  - each one is in memory.c's list, and the list has nothing the source
    has not.

A number with the top bit and more than 24 bits under it (0x80808080, a
colour) is no address. 0x80000000 by itself is a sign bit wherever the
source has it, but in the one place that also has the RAM's end,
0x80200000: the two bounds a save file's patches are held to
(memcard_save.c), which are the bounds of what the port maps.
"""
import os
import re

import support

RAM = (0x80000000, 0x80200000)
SCRATCHPAD = (0x1F800000, 0x1F800400)
# The common sound bank: the first thing above the program.
ABOVE_THE_PROGRAM = 0x8010DB00
MEMORY_C = 'src/port/memory.c'
FOLDERS = ('src/main', 'src/rebushido', 'include')
NUMBER = re.compile(r'\b0[xX]([0-9A-Fa-f]{7,8})\b')


def addresses_of(text):
    """The numbers of a piece of C that are addresses of the console."""
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    out = set()
    for m in NUMBER.finditer(text):
        value = int(m.group(1), 16)
        if value == RAM[1]:
            continue  # the RAM's end: a bound, not a place
        if 0x80000000 < value <= 0x80FFFFFF or 0x1F800000 <= value <= 0x1F80FFFF:
            out.add(value)
    return out


def run(checks):
    found = {}
    for folder in FOLDERS:
        for name in sorted(os.listdir(os.path.join(support.ROOT, folder))):
            if not name.endswith(('.c', '.h')) or name.startswith('port_'):
                continue
            with open(os.path.join(support.ROOT, folder, name), encoding='utf-8') as f:
                for value in addresses_of(f.read()):
                    found.setdefault(value, '%s/%s' % (folder, name))
    checks.true('the source has addresses written into it', len(found) >= 10)
    for value in sorted(found):
        where = '0x%08X (%s)' % (value, found[value])
        in_ram = RAM[0] <= value < RAM[1]
        in_scratchpad = SCRATCHPAD[0] <= value < SCRATCHPAD[1]
        checks.true('%s is memory the port maps' % where, in_ram or in_scratchpad)
        if in_ram:
            checks.true('%s is above the program' % where, value >= ABOVE_THE_PROGRAM)

    # the list names each by its name in the game's headers
    defined = {}
    for header in ('include/sys.h', 'include/motion.h'):
        with open(os.path.join(support.ROOT, header), encoding='utf-8') as f:
            for m in re.finditer(r'^#define\s+(\w+)\s+(0[xX][0-9A-Fa-f]{8})\b', f.read(), re.M):
                defined[m.group(1)] = int(m.group(2), 16)
    with open(os.path.join(support.ROOT, MEMORY_C), encoding='utf-8') as f:
        comment = f.read().split('*/')[0]
    listed = {defined[name] for name in re.findall(r'\b[A-Z][A-Z0-9_]+\b', comment) if name in defined}
    checks.true('the list of %s names addresses' % MEMORY_C, len(listed) >= 10)
    for value in sorted(found):
        if SCRATCHPAD[0] <= value < SCRATCHPAD[1]:
            continue
        checks.true('0x%08X (%s) is in the list of %s' % (value, found[value], MEMORY_C), value in listed)
    for value in sorted(listed):
        checks.true('0x%08X of the list of %s is in the source' % (value, MEMORY_C), value in found)
