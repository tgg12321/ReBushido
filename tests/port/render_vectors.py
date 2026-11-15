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
    'render_EmitFT3': '5CE4C6DA',
    'render_EmitFT4': 'F52AC7AE',
    'render_EmitGT3': '8CCB6E77',
    'render_EmitGT4': '3323B109',
    'render_EmitPackedFT3': '9BC3C472',
    'render_EmitPackedFT4': '3FD3A6AD',
    'render_EmitPackedGT3': '7494DADB',
    'render_EmitPackedGT4': 'BEABA66E',
    'render_EmitUnlitFT3': '32FA17DA',
    'render_EmitUnlitFT4': 'C1B8A9A0',
    'render_EmitTwoSidedFT3': '0E04EDE8',
    'render_EmitTwoSidedFT4': 'E924A69A',
    'render_EmitTwoSidedGT3': '26AA7B73',
    'render_EmitTwoSidedGT4': 'ADEBAD62',
    'render_EmitClothFT3': '6F8AF92C',
    'render_EmitClothFT4': '46BCCDA2',
    'render_EmitClothGT3': '13B486D2',
    'render_EmitClothGT4': '4A2D8862',
    'render_EmitTintedFT3': 'F1ABF169',
    'render_EmitTintedFT4': '6E3F049C',
    'render_EmitBlackF3': 'EBD2CA98',
    'render_EmitBlackF4': '7D62BD9C',
    'render_EmitOutlineFT3': '07C2568E',
    'render_EmitOutlineFT4': 'D56ECFED',
    'render_EmitOutlineGT3': '7A1276A0',
    'render_EmitOutlineGT4': '17894418',
    'render_EmitSkinOutlineFT3': '4AA0B064',
    'render_EmitSkinOutlineFT4': '490A30A6',
    'render_EmitSkinOutlineGT3': '2BBF758B',
    'render_EmitSkinOutlineGT4': 'FA03FE7A',
    'render_EmitSkinFT3': '8A304005',
    'render_EmitSkinFT4': '65E13ABB',
    'render_EmitSkinGT3': '50613DDC',
    'render_EmitSkinGT4': 'EAD820EB',
    'render_EmitF3': '5B8B9623',
    'render_EmitModeF4': '9B27B654',
    'render_EmitTinyF3': 'DEEA9213',
    'render_EmitTinyF4': '7F6C551C',
    'ProjectVertices': '77F4A004',
    'ProjectPackedVertices': '714C0049',
    'DrawGroups': '66E28CEB',
    'LightSkin': 'B9202F2B',
    'DrawSkinnedMesh': '4DB41C5F',
    'DrawSkinned': 'CF84E30D',
    'DrawCloth': '8941D1C4',
    'EmitSplitFT3': '1647B227',
    'EmitSplitFT4': '8CB1FA6A',
    'DrawStageTiles': '81B91348',
    'DrawQueue': 'DCA7E10F',
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
