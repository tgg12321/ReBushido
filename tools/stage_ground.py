#!/usr/bin/env python3
"""Reads a stage's ground file (NDATA.DAT's S00 to S37, `--dump-stages`'
s00.dat to s37.dat) into an editable form and writes it back, byte for
byte (docs/formats/GROUND.md), with the standard library alone.

    python3 tools/stage_ground.py roundtrip FILE.dat|FOLDER
    python3 tools/stage_ground.py unpack FILE.dat FOLDER
    python3 tools/stage_ground.py pack FOLDER FILE.dat
    python3 tools/stage_ground.py info FILE.dat

`roundtrip` reads every section into numbers (the meshes' vertices and
faces, the collision grid's faces with their surfaces, the obstacles, the
cells, the textures' table), writes the file again from the numbers alone
and fails if one byte differs; with a folder it does every `s*.dat` in it.
`unpack` writes the numbers as `ground.json` with the TIMs beside it
(`tims/NNN.tim`) and any section it does not read whole as a `.bin`;
`pack` writes a ground file from such a folder, its sections laid out
again (a section may change size). `info` says what is in a file.

The module is the arena compiler's too (tools/arena_compile.py): `Ground`
and its parts are what it fills, and `write` lays them out as the game
reads them (src/main/resource.c, resource_LoadStage).

Nothing of the player's disc is in the repository: the files are read
from a dump of the player's own (`rebushido --dump-stages FOLDER`).
"""
import base64
import json
import os
import struct
import sys

# ---- the numbers of the format (docs/formats/GROUND.md) ----

GRID_CELLS = 32                 # COLLISION_GRID_CELLS, STAGE_CELLS
CELL_SIZE = 2000                # COLLISION_CELL_SIZE
GRID_HALF = 32000               # COLLISION_GRID_HALF
NO_FACES = 0xFFFF               # an empty cell of the collision grid
GRID_TABLE_BYTES = GRID_CELLS * GRID_CELLS * 2
FACE_FIXED_HALVES = 15          # plane (5), frame (9), surface and edges (1)
ONE = 0x4000                    # the collision grid's 1 (a normal, an axis)
TEXTURE_GROUPS = 32             # the textures' section's table of groups
TEXTURE_RECORD_BYTES = 12
OBSTACLE_BYTES = 16
UNUSED_RECORD_BYTES = 20
END_OF_LIST = 0xFFFF            # the cells' three lists end so
RUN_LAST = 0x8000               # the last entry of a cell's list
TILE_CAN_OPEN = 0x8000          # a tile's mesh number: it can be opened
PIECE_HALVES = 10               # a piece: mesh, x y z (two halves each), angles
# a tile's mesh (MESH_COMPACT, gl_stage.c): its records are this long
TILE_RECORD_BYTES = 0x18
GROUP_TRIANGLES = 0
GROUP_QUADS = 1
GROUP_TYPE_SHIFT = 3
TILE_LAYOUT = 1                 # MESH_COMPACT

# The common sections of a stage's header (enum StageFileSection), by
# their number in the header. The last is the textures' and the one
# before it the cells'; stages with more have more between.
SECTION_MESHES = 1
SECTION_COLLISION = 2
SECTION_UNUSED = 3
SECTION_OBSTACLES = 4


def u16(data, at):
    return struct.unpack_from('<H', data, at)[0]


def s16(data, at):
    return struct.unpack_from('<h', data, at)[0]


def u32(data, at):
    return struct.unpack_from('<I', data, at)[0]


def s32(data, at):
    return struct.unpack_from('<i', data, at)[0]


class GroundError(Exception):
    pass


# ---- the meshes ----

def read_tile_mesh(blob):
    """A tile's mesh (gl_stage.c, port_GlStageTile): {'vertices': [[x, y,
    z]], 'groups': [{'type', 'records': [hex]}]} -- or None if it is not
    one this reads whole (a piece's, which has other layouts), so that it
    is kept as bytes."""
    if len(blob) < 4:
        return None
    count, layout = u16(blob, 0), u16(blob, 2)
    if layout != TILE_LAYOUT or count & 0x8000:
        return None
    at = 4
    vertices = []
    for i in range(count):
        if at + 6 > len(blob):
            return None
        vertices.append(list(struct.unpack_from('<3h', blob, at)))
        at += 6
    pad = blob[at:(at + 3) & ~3]
    at = (at + 3) & ~3
    groups = []
    while True:
        if at + 4 > len(blob):
            return None
        records, kind = u16(blob, at), u16(blob, at + 2)
        at += 4
        if records == 0:
            end_word = kind
            break
        if kind >> GROUP_TYPE_SHIFT not in (GROUP_TRIANGLES, GROUP_QUADS):
            return None
        if at + records * TILE_RECORD_BYTES > len(blob):
            return None
        group = {'type': kind, 'records': []}
        for r in range(records):
            group['records'].append(blob[at:at + TILE_RECORD_BYTES].hex())
            at += TILE_RECORD_BYTES
        groups.append(group)
    mesh = {'vertices': vertices, 'groups': groups}
    if pad != bytes(len(pad)):
        mesh['pad'] = pad.hex()
    if end_word:
        mesh['end'] = end_word
    if at != len(blob):
        mesh['tail'] = blob[at:].hex()
    return mesh


def write_tile_mesh(mesh):
    out = bytearray(struct.pack('<HH', len(mesh['vertices']), TILE_LAYOUT))
    for v in mesh['vertices']:
        out += struct.pack('<3h', *v)
    pad = (4 - len(out) % 4) % 4
    out += bytes.fromhex(mesh['pad']) if 'pad' in mesh else bytes(pad)
    for group in mesh['groups']:
        out += struct.pack('<HH', len(group['records']), group['type'])
        for record in group['records']:
            out += bytes.fromhex(record) if isinstance(record, str) else record
    out += struct.pack('<HH', 0, mesh.get('end', 0))
    out += bytes.fromhex(mesh.get('tail', ''))
    return bytes(out)


def read_meshes(data):
    """The meshes' table (reloc_RelocateBlock: a count, the offset of each
    mesh from the table's start, then the end's) and the meshes: a list,
    each a tile's mesh as numbers or a piece's as bytes."""
    count = u32(data, 0)
    if count & 0x8000 or 4 + 4 * (count + 1) > len(data):
        raise GroundError('the meshes\' table is not one')
    offsets = [u32(data, 4 + 4 * i) for i in range(count + 1)]
    first = 4 + 4 * (count + 1)
    if offsets != sorted(offsets) or (count and offsets[0] != first) or offsets[-1] > len(data):
        raise GroundError('the meshes are not one after another')
    meshes = []
    for i in range(count):
        blob = data[offsets[i]:offsets[i + 1]]
        tile = read_tile_mesh(blob)
        meshes.append(tile if tile is not None and write_tile_mesh(tile) == blob
                      else {'bytes': blob.hex()})
    tail = data[offsets[-1]:]
    return {'meshes': meshes, 'tail': tail.hex()} if tail else {'meshes': meshes}


def mesh_bytes(mesh):
    return bytes.fromhex(mesh['bytes']) if 'bytes' in mesh else write_tile_mesh(mesh)


def write_meshes(section):
    blobs = [mesh_bytes(m) for m in section['meshes']]
    first = 4 + 4 * (len(blobs) + 1)
    offsets = []
    at = first
    for blob in blobs:
        offsets.append(at)
        at += len(blob)
    offsets.append(at)
    out = bytearray(struct.pack('<I', len(blobs)))
    out += struct.pack('<%dI' % len(offsets), *offsets)
    for blob in blobs:
        out += blob
    out += bytes.fromhex(section.get('tail', ''))
    return bytes(out)


# ---- the collision grid ----

def read_face(data, at):
    """One face of a cell (collision.c): its plane, frame, surface and
    outline. Returns (face, the offset after it)."""
    nx, ny, nz = struct.unpack_from('<3h', data, at)
    d = s32(data, at + 6)
    frame = struct.unpack_from('<9h', data, at + 10)
    word = u16(data, at + 28)
    edges = word & 0xFF
    outline = struct.unpack_from('<%dh' % (2 * (edges + 1)), data, at + 30)
    face = {
        'normal': [nx, ny, nz], 'd': d,
        'origin': list(frame[0:3]), 'u': list(frame[3:6]), 'v': list(frame[6:9]),
        'surface': word >> 8,
        'outline': [[outline[2 * i], outline[2 * i + 1]] for i in range(edges + 1)],
    }
    return face, at + 30 + 4 * (edges + 1)


def write_face(face):
    out = bytearray(struct.pack('<3hi', *face['normal'], face['d']))
    out += struct.pack('<9h', *face['origin'], *face['u'], *face['v'])
    edges = len(face['outline']) - 1
    if not 0 <= edges <= 0xFF or not 0 <= face['surface'] <= 0xFF:
        raise GroundError('a face has %d edges, surface %d' % (edges, face['surface']))
    out += struct.pack('<H', (face['surface'] << 8) | edges)
    for u, v in face['outline']:
        out += struct.pack('<2h', u, v)
    return bytes(out)


def read_collision(data):
    """The grid: a halfword a cell, the offset of its faces from the
    grid's start (NO_FACES: none), then each cell's faces: a count and the
    faces. Returns {'cells': {index: [face]}, 'order': [index]} -- the
    order the faces' lists are laid out in, which is the cells' own."""
    table = [u16(data, 2 * i) for i in range(GRID_CELLS * GRID_CELLS)]
    used = sorted((offset, cell) for cell, offset in enumerate(table) if offset != NO_FACES)
    cells = {}
    order = []
    at = GRID_TABLE_BYTES
    for offset, cell in used:
        if offset != at:
            raise GroundError('cell %d\'s faces are at %d, not %d' % (cell, offset, at))
        count = s16(data, at)
        at += 2
        faces = []
        for i in range(count):
            face, at = read_face(data, at)
            faces.append(face)
        cells[cell] = faces
        order.append(cell)
    section = {'cells': cells}
    if order != sorted(order):
        section['order'] = order
    if at != len(data):
        section['tail'] = data[at:].hex()
    return section


def write_collision(section):
    cells = {int(k): v for k, v in section['cells'].items()}
    order = section.get('order') or sorted(cells)
    table = [NO_FACES] * (GRID_CELLS * GRID_CELLS)
    body = bytearray()
    for cell in order:
        offset = GRID_TABLE_BYTES + len(body)
        if offset > 0xFFFE:
            raise GroundError('the collision grid is longer than 65,534 bytes: '
                              'a cell\'s offset is a halfword')
        table[cell] = offset
        body += struct.pack('<h', len(cells[cell]))
        for face in cells[cell]:
            body += write_face(face)
    out = struct.pack('<%dH' % len(table), *table) + bytes(body)
    return out + bytes.fromhex(section.get('tail', ''))


# ---- the list nothing reads, the obstacles ----

def read_unused(data):
    count = u32(data, 0)
    records = [list(struct.unpack_from('<5i', data, 4 + UNUSED_RECORD_BYTES * i))
               for i in range(count)]
    section = {'records': records}
    tail = data[4 + UNUSED_RECORD_BYTES * count:]
    if tail:
        section['tail'] = tail.hex()
    return section


def write_unused(section):
    out = struct.pack('<I', len(section['records']))
    for r in section['records']:
        out += struct.pack('<5i', *r)
    return out + bytes.fromhex(section.get('tail', ''))


def read_obstacles(data):
    """Things a blade meets (StageObstacle, fighter_hit.c): a type (2: a
    prop that is cut down; anything else stops the blade), a halfword the
    fight uses, and x, y, z; a type of 0 ends the list."""
    records = []
    at = 0
    while s16(data, at) != 0:
        kind, used, x, y, z = struct.unpack_from('<2h3i', data, at)
        records.append({'type': kind, 'used': used, 'at': [x, y, z]})
        at += OBSTACLE_BYTES
    section = {'records': records, 'end': s16(data, at + 2)}
    at += 4
    if at != len(data):
        section['tail'] = data[at:].hex()
    return section


def write_obstacles(section):
    out = bytearray()
    for r in section['records']:
        out += struct.pack('<2h3i', r['type'], r.get('used', 0), *r['at'])
    out += struct.pack('<2h', 0, section.get('end', 0))
    return bytes(out) + bytes.fromhex(section.get('tail', ''))


# ---- the cells ----

def read_cells(data):
    """The stage grid (stage_ReadCells): the tiles (a cell and a mesh),
    the pieces (a mesh, a place, three angles) and each cell's list of
    what is drawn when it is seen, each list ended by END_OF_LIST."""
    at = 0
    tiles = []
    while u16(data, at) != END_OF_LIST:
        cell, mesh = u16(data, at), u16(data, at + 2)
        tiles.append({'cell': cell, 'mesh': mesh & ~TILE_CAN_OPEN,
                      'can_open': bool(mesh & TILE_CAN_OPEN)})
        at += 4
    at += 2
    pieces = []
    while u16(data, at) != END_OF_LIST:
        h = struct.unpack_from('<%dh' % PIECE_HALVES, data, at)
        place = [(h[1] & 0xFFFF) | (h[2] << 16), (h[3] & 0xFFFF) | (h[4] << 16),
                 (h[5] & 0xFFFF) | (h[6] << 16)]
        pieces.append({'mesh': h[0] & 0xFFFF, 'at': place, 'angles': list(h[7:10])})
        at += 2 * PIECE_HALVES
    at += 2
    runs = []
    while u16(data, at) != END_OF_LIST:
        cell = u16(data, at)
        at += 2
        entries = []
        while True:
            word = u16(data, at)
            at += 2
            entries.append(word & ~RUN_LAST)
            if word & RUN_LAST:
                break
        runs.append({'cell': cell, 'draws': entries})
    at += 2
    section = {'tiles': tiles, 'pieces': pieces, 'runs': runs}
    if at != len(data):
        section['tail'] = data[at:].hex()
    return section


def write_cells(section):
    out = bytearray()
    for t in section['tiles']:
        out += struct.pack('<HH', t['cell'], t['mesh'] | (TILE_CAN_OPEN if t.get('can_open') else 0))
    out += struct.pack('<H', END_OF_LIST)
    for p in section['pieces']:
        x, y, z = p['at']
        out += struct.pack('<H6H3h', p['mesh'], x & 0xFFFF, (x >> 16) & 0xFFFF,
                           y & 0xFFFF, (y >> 16) & 0xFFFF, z & 0xFFFF, (z >> 16) & 0xFFFF,
                           *p['angles'])
    out += struct.pack('<H', END_OF_LIST)
    for r in section['runs']:
        out += struct.pack('<H', r['cell'])
        for i, entry in enumerate(r['draws']):
            out += struct.pack('<H', entry | (RUN_LAST if i == len(r['draws']) - 1 else 0))
    out += struct.pack('<H', END_OF_LIST)
    return bytes(out) + bytes.fromhex(section.get('tail', ''))


# ---- the textures ----

def tim_length(data, at):
    """How long the TIM at `at` is: its header, colour table and picture."""
    if u32(data, at) != 0x10:
        raise GroundError('no TIM at %d' % at)
    flags = u32(data, at + 4)
    length = 8
    if flags & 8:
        length += u32(data, at + length)
    length += u32(data, at + length)
    return length


def read_textures(data):
    """The textures (tex_LoadStageGroup): a table of TEXTURE_GROUPS
    groups' offsets (the game loads group 0), each group a count and
    records of a TIM's offset, where its picture goes in video memory and
    where its colour table goes; then the TIMs. Offsets are from the
    section's start."""
    groups_at = [u32(data, 4 * i) for i in range(TEXTURE_GROUPS)]
    expect = 4 * TEXTURE_GROUPS
    groups = []
    for g_at in groups_at:
        if g_at != expect:
            raise GroundError('texture group at %d, not %d' % (g_at, expect))
        count = u32(data, g_at)
        records = []
        for r in range(count):
            offset, ix, iy, cx, cy = struct.unpack_from('<I4h', data, g_at + 4 + TEXTURE_RECORD_BYTES * r)
            records.append({'tim': offset, 'image': [ix, iy], 'clut': [cx, cy]})
        groups.append(records)
        expect = g_at + 4 + TEXTURE_RECORD_BYTES * count
    # the TIMs, in the order they are laid out
    offsets = sorted(set(r['tim'] for g in groups for r in g))
    tims = []
    at = expect
    index_of = {}
    for offset in offsets:
        if offset != at:
            raise GroundError('a TIM at %d, not %d' % (offset, at))
        length = tim_length(data, offset)
        index_of[offset] = len(tims)
        tims.append(data[offset:offset + length])
        at = offset + length
    for g in groups:
        for r in g:
            r['tim'] = index_of[r['tim']]
    section = {'groups': groups, 'tims': tims}
    if at != len(data):
        section['tail'] = data[at:].hex()
    return section


def write_textures(section):
    groups = section['groups']
    table_bytes = 4 * TEXTURE_GROUPS
    at = table_bytes
    groups_at = []
    for g in groups:
        groups_at.append(at)
        at += 4 + TEXTURE_RECORD_BYTES * len(g)
    tim_at = []
    for tim in section['tims']:
        tim_at.append(at)
        at += len(tim)
    out = bytearray(struct.pack('<%dI' % TEXTURE_GROUPS, *groups_at))
    for g in groups:
        out += struct.pack('<I', len(g))
        for r in g:
            out += struct.pack('<I4h', tim_at[r['tim']], *r['image'], *r['clut'])
    for tim in section['tims']:
        out += tim
    return bytes(out) + bytes.fromhex(section.get('tail', ''))


# ---- the whole file ----

READERS = {'meshes': read_meshes, 'collision': read_collision, 'unused': read_unused,
           'obstacles': read_obstacles, 'cells': read_cells, 'textures': read_textures}
WRITERS = {'meshes': write_meshes, 'collision': write_collision, 'unused': write_unused,
           'obstacles': write_obstacles, 'cells': write_cells, 'textures': write_textures,
           'bytes': lambda s: bytes.fromhex(s['bytes']) if isinstance(s['bytes'], str) else s['bytes']}


def section_kinds(count):
    """What each of a header's `count` sections is: the four with a fixed
    place, the cells and the textures last; what is between (a stage's
    areas, props, backdrop) is kept as bytes."""
    kinds = ['bytes'] * count
    kinds[SECTION_MESHES - 1] = 'meshes'
    kinds[SECTION_COLLISION - 1] = 'collision'
    kinds[SECTION_UNUSED - 1] = 'unused'
    kinds[SECTION_OBSTACLES - 1] = 'obstacles'
    kinds[count - 2] = 'cells'
    kinds[count - 1] = 'textures'
    return kinds


def read(data):
    """A ground file as numbers: {'sections': [{'kind', ...}], 'tail'}."""
    count = u32(data, 0)
    if not 6 <= count <= 64:
        raise GroundError('a header of %d sections' % count)
    offsets = [u32(data, 4 + 4 * i) for i in range(count + 1)]
    if offsets[0] != 4 * (count + 2) or offsets != sorted(offsets) or offsets[-1] > len(data):
        raise GroundError('the header\'s offsets are not in order')
    sections = []
    for i, kind in enumerate(section_kinds(count)):
        blob = data[offsets[i]:offsets[i + 1]]
        if kind != 'bytes':
            try:
                section = READERS[kind](blob)
                section['kind'] = kind
                if WRITERS[kind](section) != blob:
                    raise GroundError('written again it is not the same')
            except (GroundError, struct.error, IndexError, KeyError) as e:
                section = {'kind': 'bytes', 'bytes': blob.hex(), 'why': '%s: %s' % (kind, e)}
        else:
            section = {'kind': 'bytes', 'bytes': blob.hex()}
        sections.append(section)
    return {'sections': sections, 'tail': data[offsets[-1]:].hex()}


def write(ground, sector=None, align=False):
    """The file of a ground as numbers; padded to whole sectors of
    `sector` bytes if given (with zeros, as a file of NDATA is). With
    `align` a section that does not end on a word is given the zeros it
    needs, as the disc's are; without, it is a mistake."""
    blobs = [WRITERS[s['kind']](s) for s in ground['sections']]
    if align:
        blobs = [b + bytes((-len(b)) % 4) for b in blobs]
    count = len(blobs)
    at = 4 * (count + 2)
    offsets = []
    for blob in blobs:
        if at % 4:
            raise GroundError('a section does not start on a word')
        offsets.append(at)
        at += len(blob)
    offsets.append(at)
    out = bytearray(struct.pack('<I', count) + struct.pack('<%dI' % len(offsets), *offsets))
    for blob in blobs:
        out += blob
    tail = ground.get('tail', '')
    out += bytes.fromhex(tail) if isinstance(tail, str) else tail
    if sector:
        out += bytes((-len(out)) % sector)
    return bytes(out)


def section(ground, kind):
    for s in ground['sections']:
        if s['kind'] == kind:
            return s
    return None


# ---- the editable form ----

def unpack(path, folder):
    with open(path, 'rb') as f:
        ground = read(f.read())
    os.makedirs(os.path.join(folder, 'tims'), exist_ok=True)
    for i, s in enumerate(ground['sections']):
        if s['kind'] == 'textures':
            for j, tim in enumerate(s['tims']):
                with open(os.path.join(folder, 'tims', '%03d.tim' % j), 'wb') as f:
                    f.write(tim)
            s['tims'] = ['tims/%03d.tim' % j for j in range(len(s['tims']))]
        elif s['kind'] == 'bytes':
            name = 'section%02d.bin' % (i + 1)
            with open(os.path.join(folder, name), 'wb') as f:
                f.write(bytes.fromhex(s['bytes']))
            s['bytes'] = name
        elif s['kind'] == 'collision':
            s['cells'] = {str(k): v for k, v in s['cells'].items()}
    with open(os.path.join(folder, 'ground.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(ground, f, indent=1)
        f.write('\n')


def load_folder(folder):
    with open(os.path.join(folder, 'ground.json'), encoding='utf-8') as f:
        ground = json.load(f)
    for s in ground['sections']:
        if s['kind'] == 'textures':
            tims = []
            for name in s['tims']:
                with open(os.path.join(folder, name), 'rb') as f:
                    tims.append(f.read())
            s['tims'] = tims
        elif s['kind'] == 'bytes' and not all(c in '0123456789abcdef' for c in s['bytes']):
            with open(os.path.join(folder, s['bytes']), 'rb') as f:
                s['bytes'] = f.read()
    return ground


def roundtrip(path):
    """(ok, what): the file read into numbers and written again."""
    with open(path, 'rb') as f:
        data = f.read()
    ground = read(data)
    again = write(ground)
    raw = [s for s in ground['sections'] if s['kind'] == 'bytes']
    whys = [s['why'] for s in raw if 'why' in s]
    kinds = ' '.join(s['kind'][0] for s in ground['sections'])
    if again != data:
        first = next(i for i in range(min(len(again), len(data))) if again[i] != data[i]) \
            if again[:len(data)] != data[:len(again)] else min(len(again), len(data))
        return False, 'differs from byte %d (%d bytes, %d written)' % (first, len(data), len(again))
    # the same written from the JSON of it, as `unpack` and `pack` do
    text = json.loads(json.dumps(ground, default=lambda b: b.hex()))
    for s in text['sections']:
        if s['kind'] == 'textures':
            s['tims'] = [bytes.fromhex(t) for t in s['tims']]
    if write(text) != data:
        return False, 'differs when written from its JSON'
    return True, '%d bytes, sections %s%s' % (
        len(data), kinds, ('; kept as bytes: ' + '; '.join(whys)) if whys else '')


def info(path):
    with open(path, 'rb') as f:
        ground = read(f.read())
    for i, s in enumerate(ground['sections']):
        k = s['kind']
        if k == 'meshes':
            tiles = sum(1 for m in s['meshes'] if 'bytes' not in m)
            print('%2d meshes: %d (%d tiles\' read whole)' % (i + 1, len(s['meshes']), tiles))
        elif k == 'collision':
            faces = sum(len(f) for f in s['cells'].values())
            print('%2d collision: %d cells, %d faces' % (i + 1, len(s['cells']), faces))
        elif k == 'unused':
            print('%2d unused list: %d records' % (i + 1, len(s['records'])))
        elif k == 'obstacles':
            print('%2d obstacles: %d' % (i + 1, len(s['records'])))
        elif k == 'cells':
            print('%2d cells: %d tiles, %d pieces, %d cells\' lists' % (
                i + 1, len(s['tiles']), len(s['pieces']), len(s['runs'])))
        elif k == 'textures':
            print('%2d textures: %d TIMs, %d records in group 0' % (i + 1, len(s['tims']), len(s['groups'][0])))
        else:
            print('%2d bytes: %d%s' % (i + 1, len(s['bytes']) // 2, ' (%s)' % s['why'] if 'why' in s else ''))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    command = argv[1]
    if command == 'roundtrip':
        target = argv[2]
        paths = ([os.path.join(target, n) for n in sorted(os.listdir(target))
                  if n.startswith('s') and n.endswith('.dat')] if os.path.isdir(target) else [target])
        failed = 0
        for path in paths:
            try:
                ok, what = roundtrip(path)
            except (GroundError, struct.error) as e:
                ok, what = False, str(e)
            print('%s %s: %s' % ('OK  ' if ok else 'FAIL', os.path.basename(path), what))
            failed += not ok
        print('stage_ground: %d of %d files written again byte for byte' % (len(paths) - failed, len(paths)))
        return 1 if failed or not paths else 0
    if command == 'unpack' and len(argv) == 4:
        unpack(argv[2], argv[3])
        return 0
    if command == 'pack' and len(argv) == 4:
        data = write(load_folder(argv[2]))
        with open(argv[3], 'wb') as f:
            f.write(data)
        return 0
    if command == 'info':
        info(argv[2])
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
