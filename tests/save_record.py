"""The save file's header as a memory card manager would read it: Sony's
magic, one icon frame, one block, and a title in Shift-JIS (full-width
letters) that says how many fighters of each clan are unlocked."""
import unicodedata

# A new game has three fighters of each clan (SAVE_DEFAULT_UNLOCKS: KANNUKI,
# TATSUMI, MIKADO; GENGORO, KAUN, JO) and none of the extra ones. With every
# bit set there are nine of each clan and three more: the two gunners and,
# with them, the victim (the original's behaviour: docs/STATUS.md).
TITLES = {
    'default': 'BB2 Narukagami:3 VS Shainto:3',
    'all': 'BB2 Narukagami:9 VS Shainto:9 +3',
    'none': 'BB2 Narukagami:0 VS Shainto:0',
}


def check(lines, checks):
    for line in lines:
        label, text = line.split()
        header = bytes.fromhex(text)
        checks.equal(label + ': magic', header[:2], b'SC')
        checks.equal(label + ': icon frames', header[2], 0x11)
        checks.equal(label + ': blocks', header[3], 1)
        title = header[4:0x44].split(b'\0')[0].decode('shift_jis')
        # full-width letters and spaces to the plain ones
        plain = unicodedata.normalize('NFKC', title).rstrip()
        checks.equal(label + ': title', plain, TITLES[label])
        checks.equal(label + ': reserved bytes', header[0x44:0x60], bytes(28))
        # the icon's palette: white first, and its last colour black with
        # the bit that makes it solid
        checks.equal(label + ': first colour', header[0x60:0x62], b'\xff\x7f')
        checks.equal(label + ': last colour', header[0x7E:0x80], b'\x00\x80')
