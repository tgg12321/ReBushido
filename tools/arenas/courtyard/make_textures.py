#!/usr/bin/env python3
"""The stone courtyard's textures (tools/arenas/courtyard/), drawn by
numbers: 64 by 64 PNGs of flagstones, sand, wall blocks, the steps' stone
and wood, into FOLDER (textures/ beside this file if nothing says).
courtyard_blender.py puts them on the scene's materials. Standard library
alone; the same pictures every time (a fixed seed).

    python3 tools/arenas/courtyard/make_textures.py [FOLDER]
"""
import os
import random
import struct
import sys
import zlib

SIZE = 64


def write_png(path, pixels):
    rows = b''.join(b'\x00' + bytes(c for p in pixels[y * SIZE:(y + 1) * SIZE] for c in p)
                    for y in range(SIZE))

    def chunk(tag, body):
        return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(rows, 9)))
        f.write(chunk(b'IEND', b''))


def clamp(c):
    return max(0, min(255, int(c)))


def shade(base, k):
    return tuple(clamp(c * k) for c in base)


def blocks(rng, base, mortar, rows, cols, stagger):
    """Stones in rows, the joints darker, each stone a little different."""
    out = []
    tones = {}
    h = SIZE // rows
    w = SIZE // cols
    for y in range(SIZE):
        r = y // h
        off = (w // 2) if (stagger and r % 2) else 0
        for x in range(SIZE):
            c = ((x + off) % SIZE) // w
            if y % h == 0 or (x + off) % w == 0:
                out.append(shade(mortar, 0.9 + 0.2 * rng.random()))
                continue
            tone = tones.setdefault((r, c), 0.82 + 0.3 * rng.random())
            edge = 0.9 if (y % h == 1 or (x + off) % w == 1) else 1.0
            out.append(shade(base, tone * edge * (0.93 + 0.14 * rng.random())))
    return out


def noise(rng, base, spread):
    return [shade(base, 1 - spread / 2 + spread * rng.random()) for _ in range(SIZE * SIZE)]


def wood(rng):
    out = []
    for y in range(SIZE):
        for x in range(SIZE):
            grain = 0.85 + 0.15 * ((x * 7 + (y // 9) * 3) % 11) / 10
            out.append(shade((120, 78, 44), grain * (0.92 + 0.12 * rng.random())))
    return out


def main(argv):
    folder = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'textures')
    os.makedirs(folder, exist_ok=True)
    rng = random.Random(1998)
    pictures = {
        'flagstone': blocks(rng, (150, 146, 138), (70, 66, 62), 2, 2, False),
        'sand': noise(rng, (196, 172, 128), 0.25),
        'wall': blocks(rng, (128, 120, 112), (60, 56, 52), 4, 2, True),
        'step': blocks(rng, (172, 168, 158), (90, 86, 80), 1, 2, False),
        'wood': wood(rng),
    }
    for name, pixels in pictures.items():
        write_png(os.path.join(folder, name + '.png'), pixels)
    print('make_textures: %s in %s' % (', '.join(sorted(pictures)), folder))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
