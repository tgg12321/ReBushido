#!/usr/bin/env python3
"""What the play test gte_model found: the cases the emulator and the port's
C made different things of, register by register: an operation of the GTE
on the emulator's and on the port's (src/port/gte.c), or one of the
routines that are assembly and the C the port has in its place.

    python3 tools/gte_probe.py            every case the last run wrote down
                                          (sixteen at most), the registers
                                          that differ
    python3 tools/gte_probe.py --all      and the registers that agree

Run it in WSL or on Linux, after `tools/build.sh playtest gte_model`: it
reads build/playtest/gte_model/gteprobe/bad.bin, which the probe filled
(tests/play/probe/probe.c), and runs the host tests' programs
(build/rebushido/tests/port/gte_vectors and asm_vectors) for the port's
side of each case.
tests/play/README.md, "The GTE probe".
"""
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAD = 'build/playtest/gte_model/gteprobe/bad.bin'
HOST = 'build/rebushido/tests/port/gte_vectors'
ROUTINE_HOST = 'build/rebushido/tests/port/asm_vectors'
CASES = 'tests/play/probe/cases.h'
ROUTINES = 'tests/play/probe/routines.h'
REGISTERS = 64
RESULTS = 40
ENTRY = 4 * (2 + REGISTERS + RESULTS)
# a routine's cases are marked so in the probe's table (probe.c)
BAD_ROUTINE = 0xA0000000

DATA = ('VXY0 VZ0 VXY1 VZ1 VXY2 VZ2 RGBC OTZ IR0 IR1 IR2 IR3 SXY0 SXY1 SXY2 SXYP SZ0 SZ1 SZ2 SZ3 '
        'RGB0 RGB1 RGB2 RES1 MAC0 MAC1 MAC2 MAC3 IRGB ORGB LZCS LZCR').split()
CONTROL = ('RT0 RT1 RT2 RT3 RT4 TRX TRY TRZ L0 L1 L2 L3 L4 RBK GBK BBK LC0 LC1 LC2 LC3 LC4 RFC GFC BFC '
           'OFX OFY H DQA DQB ZSF3 ZSF4 FLAG').split()


def main():
    os.chdir(ROOT)
    show_all = '--all' in sys.argv[1:]
    for path in (BAD, HOST, ROUTINE_HOST):
        if not os.path.exists(path):
            sys.exit('gte_probe.py: no %s (run the play test gte_model first)' % path)
    with open(CASES, encoding='utf-8') as f:
        words = [int(w, 16) for w in re.findall(r'^\s*X\((0x[0-9A-Fa-f]+)\)', f.read(), re.M)]
    with open(ROUTINES, encoding='utf-8') as f:
        routines = re.findall(r'^\s*X\((\w+)\)', f.read(), re.M)
    with open(BAD, 'rb') as f:
        data = f.read()
    shown = 0
    for at in range(0, len(data) - ENTRY + 1, ENTRY):
        fields = struct.unpack('<%dI' % (2 + REGISTERS + RESULTS), data[at:at + ENTRY])
        word, vector, emulator, results = fields[0], fields[1], fields[2:2 + REGISTERS], fields[2 + REGISTERS:]
        if not any(fields):
            break
        if word >= BAD_ROUTINE:
            # a routine: what it wrote, then the registers after it
            out = subprocess.run([ROUTINE_HOST, 'case', str(word - BAD_ROUTINE), str(vector)],
                                 capture_output=True, text=True).stdout
            model = [0] * REGISTERS
            for m in re.finditer(r'data +(\d+) +in \w+ +out (\w+) +control +\d+ +in \w+ +out (\w+)', out):
                n = int(m.group(1))
                model[n], model[32 + n] = int(m.group(2), 16), int(m.group(3), 16)
            wrote = [int(v, 16) for v in re.findall(r'result +\d+ +out (\w+)', out)]
            print('%s, case %d (kind %d)' % (routines[word - BAD_ROUTINE], vector, vector & 3))
            print('    %-9s %-8s  %-8s' % ('', 'emulator', 'port'))
            for n in range(RESULTS):
                if results[n] != wrote[n] or show_all:
                    print('  %s result %2d %08X  %08X' % ('*' if results[n] != wrote[n] else ' ', n, results[n],
                                                          wrote[n]))
            for n, name in enumerate(DATA + CONTROL):
                if emulator[n] != model[n] or show_all:
                    print('  %s %-9s %08X  %08X' % ('*' if emulator[n] != model[n] else ' ', name, emulator[n],
                                                    model[n]))
            shown += 1
            continue
        if word not in words:
            sys.exit('gte_probe.py: %s names the word %08X, which %s has not' % (BAD, word, CASES))
        out = subprocess.run([HOST, 'case', str(words.index(word)), str(vector)], capture_output=True,
                             text=True).stdout
        before, model = [0] * REGISTERS, [0] * REGISTERS
        for m in re.finditer(r'data +(\d+) +in (\w+) +out (\w+) +control +(\d+) +in (\w+) +out (\w+)', out):
            n = int(m.group(1))
            before[n], model[n] = int(m.group(2), 16), int(m.group(3), 16)
            before[32 + n], model[32 + n] = int(m.group(5), 16), int(m.group(6), 16)
        print('%08X, case %d (kind %d%s)' % (word, vector, vector & 3,
                                            ', loaded by number' if vector & 7 == 7 else ''))
        print('    %-5s %-8s  %-8s  %-8s' % ('', 'loaded', 'emulator', 'model'))
        for n, name in enumerate(DATA + CONTROL):
            differs = emulator[n] != model[n]
            if differs or show_all:
                print('  %s %-5s %08X  %08X  %08X' % ('*' if differs else ' ', name, before[n], emulator[n],
                                                      model[n]))
        shown += 1
    print('%d cases' % shown)
    return 0


if __name__ == '__main__':
    sys.exit(main())
