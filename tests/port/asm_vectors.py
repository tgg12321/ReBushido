"""The C beside the assembly on the probe's cases: one number a routine.

The play test gte_model calls the routines themselves, the listings of
asm/funcs/ as the game links them, on the emulator with the cases of
tests/play/probe/routines.h, and compares what each wrote and the GTE's
registers after it with what the port's C wrote. That needs the emulator,
so it is not among the checks. What is held here, on every push, is that
the C still makes of those cases what it made when the emulator last
agreed: the numbers below, one for the 500 cases of each routine, were
written down then.

After a change to that C, to src/port/gte.c or to the cases: run the play
test (`wsl bash tools/build.sh playtest gte_model`), and when it passes
copy the new numbers here from `python3 tests/run.py --rebushido -v
port/asm_vectors`.
"""
import glob
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROUTINES = os.path.join(HERE, '..', 'play', 'probe', 'routines.h')

# the routine: the number for its cases
AGREED = {
    'SetRotMatrix': 'C7311463',
    'SetColorMatrix': 'B91F031E',
    'SetTransMatrix': '730B7C45',
    'SetBackColor': '98DE9C0D',
    'SetFarColor': '935CF97E',
    'SetGeomOffset': '55179149',
    'SetGeomScreen': '76B60DAF',
    'ReadGeomScreen': '9C968DC1',
    'ReadSZfifo3': '7D5B8C13',
    'LightColor': 'B1D11460',
    'DpqColorLight': '767B2FF0',
    'DpqColor3': 'A3F88BED',
    'Intpl': 'E5DEFE00',
    'Square12': '56A1A385',
    'Square0': '3B83D2B9',
    'AverageZ3': '13CC4BF9',
    'AverageZ4': '1961AACE',
    'OuterProduct12': '9210CD4F',
    'OuterProduct0': '50E74A25',
    'Lzc': '2DA64C3A',
    'RotTransPers': '9195BAD0',
    'RotTransPers3': 'D8C6A6DA',
    'RotTransPers4': '85708BA7',
    'RotTrans': '02DD6B37',
    'ApplyRotMatrix': '4CD3C614',
    'ApplyMatrix': 'FF1CBF21',
    'ApplyMatrixLV': '4640D836',
    'ApplyRotMatrixLV': '34A45C19',
    'MulMatrix0': 'C9C73FD5',
    'MulMatrix': '0AF47A67',
    'MulMatrix2': '585FAA74',
    'CompMatrix': 'A04C6BD8',
    'ScaleMatrix': '7D619AE5',
    'ScaleMatrixL': '791296D4',
    'RotMatrix': 'E488B660',
    'RotMatrixZYX': 'C19088D4',
    'RotMatrixX': '57F403E0',
    'RotMatrixY': '278FF45F',
    'RotMatrixZ': '3F8E6592',
    'LoadAverage12': '47440CDC',
    'LoadAverage0': 'F38BC0B5',
    'LoadAverageShort12': '9E5700E7',
    'LoadAverageShort0': '6B4C8643',
    'LoadAverageByte': 'F94E66D8',
    'LoadAverageCol': 'C4311ED3',
    'SquareRoot0': '77A96B54',
    'SquareRoot12': '7C4A17C6',
    'mem_MoveWords': 'A2C9F457',
    'math_Sqrt': '7FE13401',
    'math_Length': '9B628E90',
    'math_LengthSquared': '6000D2CA',
    'math_LerpRotation': '085AF787',
    'math_LerpSVector': '03A4F4FC',
    'math_LerpMatrix3x3': '08B8052F',
    'math_MulMatrix': '384EA8F1',
    'math_Transform': '2C7F4D7B',
    'math_TransformVector': '4C3BC5AD',
    'math_TransformOffset': 'FD5CA7F8',
    'gte_LoadMatrix': '6C917B07',
    'gte_LoadRotation': '0A97D765',
    'gte_ReadFarColor': '51BA568C',
    'render_DepthToOtShifted': 'B6D4B50E',
    'collision_ProjectOnFace': '2F62EFD5',
    'math_RotMatrixZYX': 'BA9AC75B',
}


def sources():
    """Sony's modules that have the port's C (each compiled apart, as the
    library's modules are), their two tables, and the port's GTE."""
    modules = []
    for path in sorted(glob.glob('src/main/psxsdk/libgte/*.c')):
        with open(path, encoding='utf-8') as f:
            if '#if RB_PORT' in f.read():
                modules.append(path)
    return modules + ['src/main/psxsdk/libgte/cstbl.c', 'src/main/psxsdk/libgte/sqrtbl.c', 'src/port/gte.c']


def names():
    with open(ROUTINES, encoding='utf-8') as f:
        return re.findall(r'^\s*X\((\w+)\)', f.read(), re.M)


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
