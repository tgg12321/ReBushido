"""The group battle card's names in the port's typeface (tools/port_ui.py): a picture for every piece the card draws

The card before a group battle's fight and after each (hud.c,
hud_DrawTeamScores) draws each fighter's name as pieces of the game's
sheet of twenty names (g_hud_name_cells, and NIGHTSTALKER's tail,
g_hud_name_8_tail_cells). The sharp menus (src/port/mods/ui/) replace each
piece with a piece of the name lettered in Shippori Mincho. Read from the
source alone, with no disc and no Pillow: every one of the twenty names
has a block in port_ui.py's table as wide as its pieces laid end to end,
each piece in the place the game draws it and as wide as its key says,
twelve texels high; no key is two pieces'; and each piece's picture is in
the mod, eight pixels a texel."""
import ast
import os
import re
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HUD = os.path.join(ROOT, 'src', 'main', 'hud.c')
PORT_UI = os.path.join(ROOT, 'tools', 'port_ui.py')
TEXTURES = os.path.join(ROOT, 'src', 'port', 'mods', 'ui', 'textures')
SCALE = 8
HEIGHT = 12
LONG_NAME = 'NIGHTSTALKER'


def cells_of(text, table):
    """[(comment, [(x, y, u, v, w, h), ...])] of a SpriteCell table of
    hud.c, each row's cells under the comment before them."""
    body = re.search(r'static SpriteCell %s\[[^=]*= \{(.*?)\n\};' % re.escape(table), text, re.S).group(1)
    rows = []
    for comment, cells in re.findall(r'/\* ([A-Z ]+) \*/\s*\{(.*?)\}\},', body, re.S):
        rows.append((comment, [tuple(int(n) for n in cell.split(','))
                               for cell in re.findall(r'\{([-\d, ]+)\}', cells + '}')]))
    return rows


def tail_cells(text):
    body = re.search(r'static SpriteCell g_hud_name_8_tail_cells\[3\] = \{(.*?)\};', text, re.S).group(1)
    return [tuple(int(n) for n in cell.split(',')) for cell in re.findall(r'\{([-\d, ]+)\}', body)]


def blocks_of_port_ui():
    """port_ui.py's BLOCKS, read without running the script (which needs
    Pillow): each block as (size, texts' words, pieces)."""
    with open(PORT_UI, encoding='utf-8') as f:
        source = f.read()
    tree = ast.parse(source)
    node = next(n for n in tree.body if isinstance(n, ast.Assign) and n.targets[0].id == 'BLOCKS')
    names = {n.id for n in ast.walk(node.value) if isinstance(n, ast.Name)}
    # the looks (TITLE, GROUP, ...) are spread into text()'s arguments
    space = {name: {} for name in names}
    space['block'] = lambda size, texts, pieces: (size, [t for t in texts if t], pieces)
    space['text'] = lambda words, *args, **kwargs: words
    space['rule'] = lambda *args: None
    return eval(compile(ast.Expression(node.value), PORT_UI, 'eval'), space)


def png_size(path):
    with open(path, 'rb') as f:
        head = f.read(24)
    if head[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    return struct.unpack('>II', head[16:24])


def run(checks):
    with open(HUD, encoding='utf-8') as f:
        hud = f.read()
    names = cells_of(hud, 'g_hud_name_cells')
    checks.equal('the names of the sheet', len(names), 20)
    tail = tail_cells(hud)
    blocks = blocks_of_port_ui()
    seen = {}
    for name, cells in names:
        if name == LONG_NAME:
            cells = cells + tail
        u0 = cells[0][2]
        v0 = cells[0][3]
        width = sum(cell[4] for cell in cells)
        # the game draws each piece at its own x, where its texels lie in
        # the name; the picture's pieces lie in the block where it does
        want = [(cell[2] - u0, cell[4]) for cell in cells]
        checks.true('%s: its pieces are one row of the sheet, drawn where they lie in it' % name,
                    all(cell[3] == v0 and cell[5] == HEIGHT and cell[0] == cell[2] - u0 for cell in cells))
        found = [b for b in blocks if b[1] == [name] and tuple(b[0]) == (width, HEIGHT)
                 and [(x, int(key.split('x')[0])) for key, x, _ in b[2]] == want]
        checks.true('%s: a block of port_ui.py, %d by %d, its pieces as the card cuts it' % (name, width, HEIGHT),
                    len(found) == 1)
        if len(found) != 1:
            continue
        for key, x, y in found[0][2]:
            w, h = (int(n) for n in key.split('-')[0].split('x'))
            checks.true('%s: %s is twelve texels high, on the block\'s row' % (name, key), h == HEIGHT and y == 0)
            checks.true('%s: %s is no other piece\'s' % (name, key), key not in seen)
            seen[key] = name
            checks.equal('%s: %s\'s picture in the mod, eight pixels a texel' % (name, key),
                         png_size(os.path.join(TEXTURES, key + '.png')), (w * SCALE, h * SCALE))
