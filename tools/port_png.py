#!/usr/bin/env python3
"""Cuts, enlarges and compares the PNGs the port writes (docs/PORT.md,
"The picture", "Remastered"), with the standard library alone.

    python3 tools/port_png.py crop IN OUT X Y W H [ZOOM]
    python3 tools/port_png.py beside OUT IN [IN ...]      side by side
    python3 tools/port_png.py diff A B [X Y W H]          how far apart

`crop` cuts a rectangle and enlarges it a whole number of times, blocks
and all, so that an edge can be looked at. `beside` puts pictures of the
same height next to each other with a line between. `diff` says how many
pixels of two pictures of the same size differ and by how much.

The port's own PNGs are three channels of eight bits with no filtering
(src/port/picture.c, src/port/gl/gl_main.c), which is all this reads.
"""
import struct
import sys
import zlib

LINE_GREY = 64


def read_png(path):
    """(width, height, bytearray of RGB rows)."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('port_png: %s is not a PNG' % path)
    at = 8
    width = height = 0
    pieces = []
    while at + 8 <= len(data):
        length, tag = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if tag == b'IHDR':
            width, height, depth, kind = struct.unpack('>IIBB', body[:10])
            if depth != 8 or kind != 2:
                sys.exit('port_png: %s is not three channels of eight bits' % path)
        elif tag == b'IDAT':
            pieces.append(body)
        elif tag == b'IEND':
            break
    raw = zlib.decompress(b''.join(pieces))
    out = bytearray(width * height * 3)
    row_bytes = width * 3
    for y in range(height):
        at = y * (row_bytes + 1)
        if raw[at] != 0:
            sys.exit('port_png: %s has a filtered row' % path)
        out[y * row_bytes:(y + 1) * row_bytes] = raw[at + 1:at + 1 + row_bytes]
    return width, height, out


def write_png(path, width, height, pixels):
    rows = bytearray()
    row_bytes = width * 3
    for y in range(height):
        rows.append(0)
        rows += pixels[y * row_bytes:(y + 1) * row_bytes]
    head = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    out = bytearray(b'\x89PNG\r\n\x1a\n')
    for tag, body in (b'IHDR', head), (b'IDAT', zlib.compress(bytes(rows), 6)), (b'IEND', b''):
        out += struct.pack('>I', len(body)) + tag + body
        out += struct.pack('>I', zlib.crc32(tag + body) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(bytes(out))


def crop(pixels, width, height, x, y, w, h, zoom):
    x, y = max(0, x), max(0, y)
    w, h = min(w, width - x), min(h, height - y)
    out = bytearray(w * zoom * h * zoom * 3)
    for j in range(h * zoom):
        row = (y + j // zoom) * width * 3
        at = j * w * zoom * 3
        for i in range(w * zoom):
            from_at = row + (x + i // zoom) * 3
            out[at:at + 3] = pixels[from_at:from_at + 3]
            at += 3
    return w * zoom, h * zoom, out


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    what = args[0]
    if what == 'crop' and len(args) >= 7:
        width, height, pixels = read_png(args[1])
        x, y, w, h = (int(a) for a in args[3:7])
        zoom = int(args[7]) if len(args) > 7 else 1
        w, h, pixels = crop(pixels, width, height, x, y, w, h, zoom)
        write_png(args[2], w, h, pixels)
        print('port_png: %s: %dx%d' % (args[2], w, h))
        return 0
    if what == 'beside' and len(args) >= 3:
        pictures = [read_png(path) for path in args[2:]]
        height = max(p[1] for p in pictures)
        width = sum(p[0] for p in pictures) + len(pictures) - 1
        out = bytearray([LINE_GREY] * (width * height * 3))
        at_x = 0
        for w, h, pixels in pictures:
            for y in range(h):
                to = (y * width + at_x) * 3
                out[to:to + w * 3] = pixels[y * w * 3:(y + 1) * w * 3]
            at_x += w + 1
        write_png(args[1], width, height, out)
        print('port_png: %s: %dx%d' % (args[1], width, height))
        return 0
    if what == 'diff' and len(args) >= 3:
        aw, ah, a = read_png(args[1])
        bw, bh, b = read_png(args[2])
        if len(args) >= 7:
            x, y, w, h = (int(v) for v in args[3:7])
            aw, ah, a = crop(a, aw, ah, x, y, w, h, 1)
            bw, bh, b = crop(b, bw, bh, x, y, w, h, 1)
        if (aw, ah) != (bw, bh):
            print('port_png: %dx%d against %dx%d: not the same size' % (aw, ah, bw, bh))
            return 1
        differ = worst = 0
        for i in range(0, len(a), 3):
            if a[i:i + 3] != b[i:i + 3]:
                differ += 1
                worst = max(worst, max(abs(a[i + c] - b[i + c]) for c in range(3)))
        print('port_png: %d of %d pixels differ, the worst channel by %d'
              % (differ, aw * ah, worst))
        return 0
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main())
