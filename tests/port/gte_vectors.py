"""The port's GTE on the probe's cases: one number an operation.

The play test gte_model runs the same cases (tests/play/probe/cases.h) on
the emulator's GTE and compares every case's registers with the model's.
That needs the emulator, so it is not among the checks. What is held here,
on every push, is that the model still makes of those cases what it made
when the emulator last agreed: the numbers below, one for the 2,000 cases
of each instruction word, were written down then.

After a change to src/port/gte.c or to the cases: run the play test
(`wsl bash tools/build.sh playtest gte_model`), and when it passes copy
the new numbers here from `python3 tests/run.py --rebushido -v
port/gte_vectors`.
"""

# instruction word: the number for its cases
AGREED = {
    '00000000': '17C3F9CF',
    '4A180001': '61FEFA2F',
    '4A280030': 'C2474225',
    '4B400006': 'D39FBB23',
    '4B58002D': 'B499647B',
    '4B68002E': 'A24956BD',
    '4A480012': '60F61693',
    '4A486012': '5D3142A7',
    '4A41E012': '9DA2E5AD',
    '4A49E012': '00291376',
    '4A4DA412': '5ACA1CEA',
    '4AC8041E': '548FDC4E',
    '4AD80420': 'D17A7A09',
    '4B08041B': '20BF2B6F',
    '4B98003D': '0491F0E7',
    '4B90003D': '6C6647A6',
    '4BA8003E': 'FB7E0DF4',
    '4BA0003E': 'F3ED8410',
    '4AA00428': '5F5CD3C1',
    '4AA80428': 'AA31F657',
    '4B78000C': '678BA6A3',
    '4B70000C': 'D3689683',
    '4AF8002A': 'A679F475',
    '4A680029': 'D0605125',
    '4A980011': 'D189BCE9',
    '4A780010': '32FC494D',
    '4AE80413': 'E00BB176',
    '4AF80416': 'F26FA866',
    '4B18043F': 'C00F35CB',
    '4B38041C': 'D3EC427F',
    '4B280414': '7864FA02',
    '4A100001': 'A95D5587',
    '4A180401': '5A881EEF',
    '4A200030': 'B6A4B92D',
    '4A400012': '5A322ED0',
    '4A488012': 'D54EF07F',
    '4A490012': '7229BBBB',
    '4A4A6412': '290F3087',
    '4A4C2012': '6F88657D',
    '4A484012': '74212B61',
    '4A4FC012': 'C747F383',
    '4A4E6012': '742A98EB',
}


def check(lines, checks):
    seen = {}
    for line in lines:
        word, number = line.split()
        seen[word] = number
    checks.equal('the operations', sorted(seen), sorted(AGREED))
    for word in sorted(AGREED):
        if word in seen:
            checks.equal('the cases of %s' % word, seen[word], AGREED[word])
