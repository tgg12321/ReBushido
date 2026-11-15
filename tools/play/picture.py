"""Video memory of two runs of a play test, compared (tests/play/README.md,
"The picture").

A run writes the whole of the PlayStation's video memory at the checkpoints
that say `vram` (tools/playtest.py): 1024 by 512 pixels of 16 bits, five
bits each of red, green and blue under the mask bit. The emulator's script
writes its software GPU's, the native port its own (docs/PORT.md, "The
picture"). Here two of them are held against each other pixel for pixel:
how many differ, where, and by how much.

Nothing here knows a test: tools/playtest.py and tools/port_compare.py give
it the bytes.
"""
import struct
import zlib

WIDTH = 1024
HEIGHT = 512
BYTES = WIDTH * HEIGHT * 2
# The game's two buffers, one above the other, and what is beside and below
# them: textures and colour tables.
REGIONS = (
    ("buffer 0", 0, 0, 640, 240),
    ("buffer 1", 0, 240, 640, 240),
    ("below the buffers", 0, 480, 640, 32),
    ("textures", 640, 0, 384, 512),
)


def read(path):
    """A run's video memory: the first megabyte of the file (the emulator
    writes a second megabyte, which is not video memory)."""
    with open(path, "rb") as f:
        data = f.read(BYTES)
    if len(data) != BYTES:
        raise ValueError("%s is not %d bytes of video memory" % (path, BYTES))
    return data


class Difference:
    """What differs between two video memories."""

    def __init__(self, first, second):
        self.pixels = []  # (x, y, first, second)
        if first != second:
            row = WIDTH * 2
            for y in range(HEIGHT):
                a, b = first[y * row:(y + 1) * row], second[y * row:(y + 1) * row]
                if a == b:
                    continue
                pa, pb = struct.unpack("<%dH" % WIDTH, a), struct.unpack("<%dH" % WIDTH, b)
                self.pixels += [(x, y, pa[x], pb[x]) for x in range(WIDTH) if pa[x] != pb[x]]
        self.count = len(self.pixels)

    def box(self, pixels=None):
        """(left, top, right, bottom) of the pixels that differ, the last
        ones included; None if none does."""
        pixels = self.pixels if pixels is None else pixels
        if not pixels:
            return None
        return (min(p[0] for p in pixels), min(p[1] for p in pixels),
                max(p[0] for p in pixels), max(p[1] for p in pixels))

    def by_region(self):
        """[(name, count, box)] for the regions that have a difference."""
        found = []
        for name, left, top, w, h in REGIONS:
            inside = [p for p in self.pixels if left <= p[0] < left + w and top <= p[1] < top + h]
            if inside:
                found.append((name, len(inside), self.box(inside)))
        return found

    def by_size(self):
        """{how far apart: pixels}: the most any of a pixel's three colours
        differs by, of 31; 0 is a pixel whose colours are the same and
        whose mask bit is not."""
        sizes = {}
        for _, _, a, b in self.pixels:
            most = max(abs((a >> shift & 31) - (b >> shift & 31)) for shift in (0, 5, 10))
            sizes[most] = sizes.get(most, 0) + 1
        return sizes

    def summary(self):
        """A line: how many, where, how far apart."""
        if not self.count:
            return "the same"
        regions = "; ".join("%s %d (%d,%d)-(%d,%d)" % ((name, count) + box) for name, count, box in self.by_region())
        sizes = self.by_size()
        small = sum(n for size, n in sizes.items() if size <= 1)
        return "%d pixels differ: %s; %d of them by 1 of 31 or less, the most by %d" % (
            self.count, regions, small, max(sizes))

    def picture(self, first):
        """RGB bytes of the first memory, dimmed, with every pixel that
        differs in red: for looking at."""
        lut = [bytes(((v & 31) * 3, (v >> 5 & 31) * 3, (v >> 10 & 31) * 3)) for v in range(65536)]
        rgb = bytearray(b"".join(lut[v] for (v,) in struct.iter_unpack("<H", first)))
        for x, y, _, _ in self.pixels:
            rgb[(y * WIDTH + x) * 3:(y * WIDTH + x) * 3 + 3] = b"\xff\x20\x20"
        return bytes(rgb)


def to_rgb(memory):
    """RGB bytes of a video memory, as it would be shown."""
    lut = [bytes(((v & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v >> 10 & 31) * 255 // 31))
           for v in range(65536)]
    return b"".join(lut[v] for (v,) in struct.iter_unpack("<H", memory))


def write_png(path, width, height, rgb):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))
