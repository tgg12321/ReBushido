"""The sharp menus' blocks of the screens the art's tours reached last (tools/port_ui.py): the options' pages, the slash mode, the staff roll, a fight begun again

tools/port_ui.py's table has a section for each of those screens (between
`# -- NAME --` and `# -- end of NAME --`), measured from the textures the
tours of tools/port_art.py dumped. Read from the source alone, with no disc
and no Pillow: every piece lies inside its block and is a picture of the
mod eight pixels a texel; no key is a piece of two blocks of the whole
table; every text lies inside its block and every piece holds a text's
letters, a rule or a panel; the staff roll's pieces are each a name or a heading of their own,
as STAFF.BIN cuts them, each holding one text whole; the gold of the
options' rows under the cursor is cut where the white is; and the italic
of the roll's Lightweight staff leans, the rest stand up."""
import ast
import os
import re
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PORT_UI = os.path.join(ROOT, 'tools', 'port_ui.py')
TEXTURES = os.path.join(ROOT, 'src', 'port', 'mods', 'ui', 'textures')
SCALE = 8
SECTIONS = {
    "the options' pages (setup)": 11,
    'the slash mode (slash)': 8,
    'the staff roll (ending-0)': 13,
    'a fight begun again (story)': 1,
}
STAFF = 'the staff roll (ending-0)'
SETUP = "the options' pages (setup)"


def evaluate(source):
    """Blocks of port_ui.py's source, each (size, [(words, boxes, slant,
    drawn)], [(key, x, y)], [every rectangle anything is drawn in])."""
    tree = ast.parse(source, mode='eval')
    names = {n.id for n in ast.walk(tree) if isinstance(n, ast.Name)}
    space = {name: {} for name in names}
    space['block'] = lambda size, texts, pieces: (size, [t for t in texts if t[0] is not None], pieces,
                                                  [r for t in texts for r in t[3]])
    # a text, and what it draws: its rectangles and its panel's
    space['text'] = lambda words, boxes, *args, **kwargs: (
        words, boxes, kwargs.get('slant', 0.0), boxes + ([kwargs['panel']] if kwargs.get('panel') else []))
    space['rule'] = lambda box, colour: (None, None, 0.0, [box])
    return eval(compile(tree, PORT_UI, 'eval'), space)


def sections(source):
    out = {}
    for name in SECTIONS:
        begin = source.index('    # -- %s --\n' % name)
        end = source.index('    # -- end of %s --\n' % name)
        out[name] = evaluate('[\n' + source[begin:end] + ']')
    return out


def all_blocks(source):
    tree = ast.parse(source)
    node = next(n for n in tree.body if isinstance(n, ast.Assign) and n.targets[0].id == 'BLOCKS')
    return evaluate(ast.get_source_segment(source, node.value))


def png_size(path):
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        head = f.read(24)
    if head[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    return struct.unpack('>II', head[16:24])


def size_of(key):
    return tuple(int(n) for n in key.split('-')[0].split('x'))


def meets(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def run(checks):
    with open(PORT_UI, encoding='utf-8') as f:
        source = f.read()
    found = sections(source)
    owners = {}
    for block in all_blocks(source):
        for key, _, _ in block[2]:
            owners[key] = owners.get(key, 0) + 1
    for name, want in SECTIONS.items():
        blocks = found[name]
        checks.equal('%s: its blocks' % name, len(blocks), want)
        for (w, h), texts, pieces, drawn in blocks:
            for words, boxes, _, _ in texts:
                checks.true('%s: "%s" lies inside its block, %d by %d' % (name, words, w, h),
                            all(0 <= x0 < x1 <= w and 0 <= y0 < y1 <= h for x0, y0, x1, y1 in boxes))
            for key, x, y in pieces:
                pw, ph = size_of(key)
                rect = (x, y, x + pw, y + ph)
                checks.true('%s: %s lies inside its block' % (name, key), 0 <= x and 0 <= y and x + pw <= w and y + ph <= h)
                checks.equal('%s: %s is a piece of one block of the table' % (name, key), owners.get(key), 1)
                checks.true('%s: %s holds letters, a rule or a panel' % (name, key),
                            any(meets(rect, box) for box in drawn))
                checks.equal("%s: %s's picture in the mod, eight pixels a texel" % (name, key),
                             png_size(os.path.join(TEXTURES, key + '.png')), (pw * SCALE, ph * SCALE))
    # the staff roll: a text for each piece, whole within it
    for (w, h), texts, pieces, _ in found[STAFF]:
        checks.equal('the staff roll: a text for each piece (%s)' % texts[0][0], len(texts), len(pieces))
        for key, x, y in pieces:
            pw, ph = size_of(key)
            inside = [t for t in texts if len(t[1]) == 1 and x <= t[1][0][0] and t[1][0][2] <= x + pw
                      and y <= t[1][0][1] and t[1][0][3] <= y + ph]
            checks.equal('the staff roll: %s holds one text whole' % key, len(inside), 1)
    words = {t[0]: t[2] for block in found[STAFF] for t in block[1]}
    checks.true("the staff roll: the Lightweight staff's names lean",
                all(words[n] > 0 for n in ('KUNIHIKO', 'Director', 'Motion Capturing', 'TETSUYA')))
    checks.true("the staff roll: Square's stand up",
                all(words[n] == 0 for n in ('Executive', 'Jonathan Williams', 'Special Thanks to')))
    # the gold of the cursor's row: cut where the white is
    white, gold = found[SETUP][-2], found[SETUP][-1]
    white_cuts = set((x, y, size_of(k)[0]) for k, x, y in white[2])
    checks.true("the options' rows: the gold under the cursor cut where the white is",
                white[0] == gold[0] and all((x, y, size_of(k)[0]) in white_cuts for k, x, y in gold[2]))
    checks.true("the options' rows: the gold's own colour", re.search(r"\(209, 209, 33\)", source) is not None)
