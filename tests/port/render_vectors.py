"""The renderer's C on the probe's cases: one number a routine.

The play test gte_model runs the renderer's listings, as the game links
them, on the emulator with the made-up meshes of
tests/play/probe/render_cases.h, and compares everything each left (the
ordering tables, the primitives, the mesh, the scratchpad, the cursors and
the GTE's registers) with what the port's C left. That needs the emulator,
so it is not among the checks. What is held here, on every push, is that
the C still makes of those cases what it made when the emulator last
agreed: the numbers below, one for the 200 cases of each routine, were
written down then.

After a change to that C, to src/port/gte.c or to the cases: run the play
test (`wsl bash tools/build.sh playtest gte_model`), and when it passes
copy the new numbers here from `python3 tests/run.py --rebushido -v
port/render_vectors`.
"""
import glob
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
CASES = os.path.join(HERE, '..', 'play', 'probe', 'render_cases.h')

# the routine: the number for its cases
AGREED = {
    'render_EmitFT3': '1F86A770',
    'render_EmitFT4': '55B016A4',
    'render_EmitGT3': '86A83F6D',
    'render_EmitGT4': '5D09B296',
    'render_EmitPackedFT3': '9DBC5A76',
    'render_EmitPackedFT4': '2D7E3448',
    'render_EmitPackedGT3': '3DD690A3',
    'render_EmitPackedGT4': '77A3D699',
    'render_EmitUnlitFT3': 'F1853139',
    'render_EmitUnlitFT4': 'F9CF8081',
    'render_EmitTwoSidedFT3': 'A000D5CA',
    'render_EmitTwoSidedFT4': '5BBE3AE1',
    'render_EmitTwoSidedGT3': '224AE2BE',
    'render_EmitTwoSidedGT4': 'DE98FBC5',
    'render_EmitTintedFT3': '6490EB65',
    'render_EmitTintedFT4': '095973F2',
    'render_EmitBlackF3': 'E0C4A936',
    'render_EmitBlackF4': 'BE561653',
    'render_EmitOutlineFT3': '63C617D5',
    'render_EmitOutlineFT4': '227E390F',
    'render_EmitOutlineGT3': 'E75459C0',
    'render_EmitOutlineGT4': 'A1166E24',
    'render_EmitSkinOutlineFT3': 'CCAF09B7',
    'render_EmitSkinOutlineFT4': '81367512',
    'render_EmitSkinOutlineGT3': '0E0E0805',
    'render_EmitSkinOutlineGT4': 'B88EA803',
    'render_EmitSkinFT3': 'E876B466',
    'render_EmitSkinFT4': 'A2960A63',
    'render_EmitSkinGT3': '3B67B712',
    'render_EmitSkinGT4': '52F0D6AA',
    'render_EmitF3': 'D4C31BB8',
    'render_EmitModeF4': 'BBE8922C',
    'render_EmitTinyF3': 'E75FEC91',
    'render_EmitTinyF4': '5A49A327',
    'ProjectVertices': 'A30452F0',
    'ProjectPackedVertices': '586B82BB',
    'DrawGroups': '75345C5B',
    'LightSkin': '98FC7D3E',
    'DrawSkinnedMesh': 'AB430F2A',
    'DrawSkinned': '633BD4CD',
}


def sources():
    """Sony's modules that have the port's C (the renderer calls their
    products of matrices), their two tables, and the port's GTE."""
    modules = []
    for path in sorted(glob.glob('src/main/psxsdk/libgte/*.c')):
        with open(path, encoding='utf-8') as f:
            if '#if RB_PORT' in f.read():
                modules.append(path)
    return modules + ['src/main/psxsdk/libgte/cstbl.c', 'src/main/psxsdk/libgte/sqrtbl.c', 'src/port/gte.c']


def names():
    """The routines in the order of the list: the handlers' table, then
    the others."""
    with open(CASES, encoding='utf-8') as f:
        text = f.read()
    emits = re.findall(r'^\s*X\((render_\w+),', text, re.M)
    others = re.findall(r'^\s*X\((\w+)\)', text, re.M)
    return emits + others


def check(lines, checks):
    order = names()
    seen = {}
    for line in lines:
        index, number = line.split()
        seen[order[int(index)]] = number
    checks.equal('the routines', sorted(seen), sorted(AGREED))
    for name in sorted(AGREED):
        if name in seen:
            checks.equal('the cases of %s' % name, seen[name], AGREED[name])
