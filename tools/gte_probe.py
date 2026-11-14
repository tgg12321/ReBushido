#!/usr/bin/env python3
"""What the play test gte_model found: the cases the emulator's GTE and the
port's GTE in C made different things of, register by register.

    python3 tools/gte_probe.py            every case the last run wrote down
                                          (sixteen at most), the registers
                                          that differ
    python3 tools/gte_probe.py --all      and the registers that agree

Run it in WSL or on Linux, after `tools/build.sh playtest gte_model`: it
reads build/playtest/gte_model/gteprobe/bad.bin, which the probe filled
(tests/play/probe/probe.c), and runs the host test's program
(build/rebushido/tests/port/gte_vectors) for the model's side of each case.
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
CASES = 'tests/play/probe/cases.h'
REGISTERS = 64
ENTRY = 4 * (2 + REGISTERS)

DATA = ('VXY0 VZ0 VXY1 VZ1 VXY2 VZ2 RGBC OTZ IR0 IR1 IR2 IR3 SXY0 SXY1 SXY2 SXYP SZ0 SZ1 SZ2 SZ3 '
        'RGB0 RGB1 RGB2 RES1 MAC0 MAC1 MAC2 MAC3 IRGB ORGB LZCS LZCR').split()
CONTROL = ('RT0 RT1 RT2 RT3 RT4 TRX TRY TRZ L0 L1 L2 L3 L4 RBK GBK BBK LC0 LC1 LC2 LC3 LC4 RFC GFC BFC '
           'OFX OFY H DQA DQB ZSF3 ZSF4 FLAG').split()


def main():
    os.chdir(ROOT)
    show_all = '--all' in sys.argv[1:]
    for path in (BAD, HOST):
        if not os.path.exists(path):
            sys.exit('gte_probe.py: no %s (run the play test gte_model first)' % path)
    with open(CASES, encoding='utf-8') as f:
        words = [int(w, 16) for w in re.findall(r'^\s*X\((0x[0-9A-Fa-f]+)\)', f.read(), re.M)]
    with open(BAD, 'rb') as f:
        data = f.read()
    shown = 0
    for at in range(0, len(data) - ENTRY + 1, ENTRY):
        fields = struct.unpack('<%dI' % (2 + REGISTERS), data[at:at + ENTRY])
        word, vector, emulator = fields[0], fields[1], fields[2:]
        if not any(fields):
            break
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
