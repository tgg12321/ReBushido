"""The mods' texture keys and PNGs, worked out a second time for tests/port/mods.c.

The keys are worked out here from docs/MODS.md's description of them (FNV-1a
of 64 bits over the depth, the size, the texels in order and the colour
table), over the same made-up video memory, and not from src/port/mods.c.
The PNGs the C reads are written here with Python's zlib, in every colour
type and depth, every filter, stored and packed, in one IDAT and in
several; the one the C writes is read here with zlib.
"""
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FOLDER = os.path.join(ROOT, 'build', 'tests', 'mods_png')
WIDTH, HEIGHT = 1024, 512

# ---- the keys ----

KEY_START = 0xCBF29CE484222325
KEY_PRIME = 0x100000001B3
DEPTH_CODE = {4: 0, 8: 1, 16: 2}


def made_vram():
    """tests/port/mods.c's video memory, step by step."""
    vram = [(i * 7919) & 0xFFFF for i in range(WIDTH * HEIGHT)]

    def texture4(page_x, page_y, u0, v0, clut_x, clut_y):
        for v in range(6):
            for u in range(13):
                at_u = u0 + u
                at = (page_y + v0 + v) * WIDTH + page_x + at_u // 4
                shift = (at_u % 4) * 4
                vram[at] = (vram[at] & ~(0xF << shift) & 0xFFFF) | (((u * 3 + v * 5) % 16) << shift)
        for i in range(16):
            vram[clut_y * WIDTH + clut_x + i] = 0 if i == 0 else 0x1000 + i * 0x111

    texture4(640, 0, 5, 9, 0, 480)
    texture4(768, 256, 2, 100, 512, 300)
    return vram


def value(vram, depth, page_x, page_y, u, v, window):
    and_u, or_u, and_v, or_v = window
    at_u, at_v = (u & and_u) | or_u, (v & and_v) | or_v
    row = ((page_y + at_v) % HEIGHT) * WIDTH
    if depth == 4:
        return (vram[row + (page_x + at_u // 4) % WIDTH] >> ((at_u % 4) * 4)) & 0xF
    if depth == 8:
        return (vram[row + (page_x + at_u // 2) % WIDTH] >> ((at_u % 2) * 8)) & 0xFF
    return vram[row + (page_x + at_u) % WIDTH]


def key_name(vram, depth, page_x, page_y, u0, v0, w, h, clut_x, clut_y, window=(0xFF, 0, 0xFF, 0)):
    data = bytearray([DEPTH_CODE[depth]]) + struct.pack('<HH', w, h)
    for v in range(h):
        for u in range(w):
            texel = value(vram, depth, page_x, page_y, u0 + u, v0 + v, window)
            data += bytes([texel]) if depth != 16 else struct.pack('<H', texel)
    colors = {4: 16, 8: 256, 16: 0}[depth]
    for i in range(colors):
        data += struct.pack('<H', vram[(clut_y % HEIGHT) * WIDTH + (clut_x + i) % WIDTH])
    key = KEY_START
    for byte in data:
        key = ((key ^ byte) * KEY_PRIME) & 0xFFFFFFFFFFFFFFFF
    return '%dx%d-%d-%016x' % (w, h, depth, key)


def expected_keys():
    vram = made_vram()
    out = {'four': key_name(vram, 4, 640, 0, 5, 9, 13, 6, 0, 480)}
    # the test then changes a texel of the second copy for good
    at = (256 + 101) * WIDTH + 768 + 1
    vram[at] ^= 0x10
    out['window'] = key_name(vram, 4, 640, 0, 0, 0, 4, 2, 0, 480, (0xF0, 5, 0xF8, 9))
    out['sixteen'] = key_name(vram, 16, 100, 200, 0, 0, 20, 3, 0, 0, (WIDTH - 1, 0, HEIGHT - 1, 0))
    out['eight'] = key_name(vram, 8, 320, 64, 3, 1, 9, 4, 16, 500)
    # the scene's pages (docs/MODS.md, "The scene's textures by page"): the
    # stage's own pixels are x 640 to 663, y 0 to 20
    own = set((x, y) for x in range(640, 664) for y in range(21))
    out['page_own'] = page_key_name(vram, 4, 640, 0, 0, 480, own)
    out['page_all'] = page_key_name(vram, 4, 640, 0, 0, 480, None)
    out['page_eight'] = page_key_name(vram, 8, 768, 256, 256, 481, None)
    return out


def page_key_name(vram, depth, page_x, page_y, clut_x, clut_y, own):
    """A whole page of 256 by 256 texels through a table: a texel whose
    pixel of video memory is not the stage's own counts as 0."""
    per = {4: 4, 8: 2, 16: 1}[depth]
    data = bytearray([DEPTH_CODE[depth]]) + struct.pack('<HH', 256, 256)
    for v in range(256):
        for u in range(256):
            pixel = (page_x + u // per, page_y + v)
            texel = 0
            if own is None or pixel in own:
                texel = value(vram, depth, page_x, page_y, u, v, (0xFF, 0, 0xFF, 0))
            data += bytes([texel]) if depth != 16 else struct.pack('<H', texel)
    for i in range({4: 16, 8: 256, 16: 0}[depth]):
        data += struct.pack('<H', vram[clut_y * WIDTH + clut_x + i])
    key = KEY_START
    for byte in data:
        key = ((key ^ byte) * KEY_PRIME) & 0xFFFFFFFFFFFFFFFF
    return 'page-256x256-%d-%016x' % (depth, key)

# ---- the PNGs ----


def chunk(kind, data):
    body = kind + data
    return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filtered(rows, back):
    """Each row filtered by the next of the five filters in turn."""
    out = bytearray()
    prior = bytes(len(rows[0]))
    for y, row in enumerate(rows):
        kind = y % 5
        line = bytearray()
        for i, byte in enumerate(row):
            a = row[i - back] if i >= back else 0
            b = prior[i]
            c = prior[i - back] if i >= back else 0
            guess = [0, a, b, (a + b) // 2, paeth(a, b, c)][kind]
            line.append((byte - guess) & 0xFF)
        out += bytes([kind]) + line
        prior = row
    return bytes(out)


def pack(samples, depth):
    """A row's samples at a depth, the first highest in its byte."""
    if depth == 16:
        return b''.join(struct.pack('>H', s) for s in samples)
    if depth == 8:
        return bytes(samples)
    out = bytearray()
    per = 8 // depth
    for i in range(0, len(samples), per):
        byte = 0
        for j in range(per):
            s = samples[i + j] if i + j < len(samples) else 0
            byte |= s << (8 - depth * (j + 1))
        out.append(byte)
    return bytes(out)


def png(w, h, depth, color, rows, extra=b'', level=9, pieces=1, interlace=0):
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    back = max(1, channels * depth // 8)
    data = zlib.compress(filtered([pack(r, depth) for r in rows], back), level)
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, color, 0, 0, interlace))
    out += extra
    step = (len(data) + pieces - 1) // pieces
    for i in range(0, len(data), step):
        out += chunk(b'IDAT', data[i:i + step])
    return out + chunk(b'IEND', b'')


def sample(x, y, c, top):
    return (x * 37 + y * 101 + c * 59 + x * y * 7) % (top + 1)


def cases():
    """name: (file bytes, the RGBA the reader is to make, or None for one
    it is to refuse)"""
    out = {}

    def rgba_of(w, h, color, depth, rows, key=None, palette=None):
        pixels = bytearray()
        for row in rows:
            for x in range(w):
                if color == 3:
                    pixels += bytes(palette[row[x]])
                    continue
                n = {0: 1, 2: 3, 4: 2, 6: 4}[color]
                s = row[x * n:(x + 1) * n]
                byte = [(v >> 8) if depth == 16 else v * 255 // ((1 << depth) - 1) for v in s]
                if color in (0, 4):
                    alpha = byte[1] if color == 4 else (0 if key is not None and s[0] == key[0] else 255)
                    pixels += bytes([byte[0], byte[0], byte[0], alpha])
                else:
                    alpha = byte[3] if color == 6 else (0 if key is not None and list(s) == key else 255)
                    pixels += bytes(byte[:3] + [alpha])
        return bytes(pixels)

    def plain(name, w, h, depth, color, level=9, pieces=1, key=None, palette=None, trns=b''):
        n = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
        top = (1 << depth) - 1
        if color == 3:
            top = len(palette) - 1
        rows = [[sample(x, y, c, top) for x in range(w) for c in range(n)] for y in range(h)]
        if key is not None:
            rows[0][:n] = key  # the first pixel is the clear colour
        extra = b''
        if color == 3:
            extra = chunk(b'PLTE', b''.join(bytes(p[:3]) for p in palette))
        if trns:
            extra += chunk(b'tRNS', trns)
        out[name] = (png(w, h, depth, color, rows, extra, level, pieces),
                     rgba_of(w, h, color, depth, rows, key, palette), w, h)

    plain('rgba8', 9, 7, 8, 6)
    plain('rgba8_stored', 9, 7, 8, 6, level=0)
    plain('rgba8_large', 300, 40, 8, 6, pieces=3)
    plain('rgba16', 3, 3, 16, 6)
    plain('rgb8_key', 5, 4, 8, 2, key=[10, 20, 30], trns=struct.pack('>HHH', 10, 20, 30))
    plain('gray1', 11, 3, 1, 0)
    plain('gray2', 9, 3, 2, 0)
    plain('gray16_key', 4, 4, 16, 0, key=[1234], trns=struct.pack('>H', 1234))
    plain('graya8', 6, 5, 8, 4)
    palette = [(255, 0, 0, 0), (0, 255, 0, 128), (0, 0, 255, 255), (9, 9, 9, 255), (200, 100, 50, 255)]
    plain('palette4', 7, 5, 4, 3, palette=palette, trns=bytes(p[3] for p in palette[:3]))
    big = [(i, 255 - i, (i * 7) % 256, 255) for i in range(256)]
    plain('palette8', 33, 9, 8, 3, palette=big)
    good = out['rgba8'][0]
    interlaced = png(9, 7, 8, 6, [[0] * 36] * 7, interlace=1)
    out['interlaced'] = (interlaced, None, 0, 0)
    out['cut_short'] = (good[:len(good) // 2], None, 0, 0)
    return out


def fnv32(data):
    s = 0x811C9DC5
    for byte in data:
        s = ((s ^ byte) * 0x01000193) & 0xFFFFFFFF
    return s


def arguments():
    os.makedirs(FOLDER, exist_ok=True)
    names = []
    for name, (data, _, _, _) in sorted(cases().items()):
        with open(os.path.join(FOLDER, name + '.png'), 'wb') as f:
            f.write(data)
        names.append(name + '.png')
    return [FOLDER] + names


def check(lines, checks):
    keys = expected_keys()
    made = cases()
    seen_keys = 0
    seen_pngs = 0
    for line in lines:
        words = line.split()
        if words[0] == 'key':
            checks.equal('the key of %s, worked out from docs/MODS.md' % words[1], words[2], keys[words[1]])
            seen_keys += 1
        elif words[0] == 'png':
            name = words[1][:-len('.png')]
            _, want, w, h = made[name]
            if want is None:
                checks.true('%s is refused, not read' % name, words[2] in ('refused', 'damaged'))
            else:
                checks.equal('%s: its size and pixels' % name, words[2:5], [str(w), str(h), str(fnv32(want))])
            seen_pngs += 1
        elif words[0] == 'written':
            data = bytes(int(v) for v in words[1:])
            checks.equal('the PNG written begins as every PNG', data[:8], b'\x89PNG\r\n\x1a\n')
            w, h, depth, color = struct.unpack('>IIBB', data[16:26])
            checks.equal('its header', (w, h, depth, color), (7, 3, 8, 6))
            at, stream = 8, b''
            while at < len(data):
                length = struct.unpack('>I', data[at:at + 4])[0]
                kind = data[at + 4:at + 8]
                body = data[at + 4:at + 8 + length]
                crc = struct.unpack('>I', data[at + 8 + length:at + 12 + length])[0]
                checks.equal('chunk %s: its sum' % kind.decode(), crc, zlib.crc32(body) & 0xFFFFFFFF)
                if kind == b'IDAT':
                    stream += data[at + 8:at + 8 + length]
                at += 12 + length
            rows = zlib.decompress(stream)
            want = b''.join(bytes([0]) + b''.join(bytes([x * 30, y * 80, x + y, 255 - x]) for x in range(7))
                            for y in range(3))
            checks.equal('its rows, as zlib reads them', rows, want)
    checks.equal('keys printed', seen_keys, len(keys))
    checks.equal('PNGs read', seen_pngs, len(made))
