"""The motion decoder against the disc: every frame of every motion of
disc/MOTION/*.BBM, decoded by the game's C and by the decoder below, which
is written from docs/formats/BBM.md and works another way: it takes the
stream as one long string of bits and decodes every frame from the start of
its group of eight, where the game's goes on from the frame before.

Skipped when the disc's files are not there."""
import os
import struct

import support

FRAME_VALUES = 66    # the root's three, then 63 angles
PACKED_VALUES = 63
GROUP_FRAMES = 8
GROUP_TABLE = 0x70   # bytes into the stream
# The two triples that are signed: the 17th and the 19th of the bones'.
SIGNED_VALUES = (54, 55, 56, 60, 61, 62)


def motion_files():
    directory = support.disc_file('MOTION')
    return sorted(os.path.join(directory, name) for name in os.listdir(directory)
                  if name.upper().endswith('.BBM'))


def frame_count(data):
    """How many frames the file's motions use: a motion is the number of its
    first frame and where its marks are, and the marks begin with the
    motion's kind and its length in frames."""
    motions, marks = struct.unpack_from('<II', data, 4)
    count = 0
    for at in range(motions, marks, 4):
        first, marks_offset = struct.unpack_from('<HH', data, at)
        count = max(count, first + data[marks + marks_offset + 1])
    return count


class Stream:
    def __init__(self, data):
        stream_offset = struct.unpack_from('<I', data, 0x10)[0]
        self.bytes = data[stream_offset:]
        words = struct.unpack_from('<%dI' % (len(self.bytes) // 4), self.bytes)
        self.bits = ''.join(format(word, '032b') for word in words)
        self.at = 0
        self.pose = [self.read(12) for _ in range(PACKED_VALUES)]
        self.codes = [self.read(2) for _ in range(PACKED_VALUES)]

    def read(self, width):
        field = self.bits[self.at:self.at + width]
        self.at += width
        return int(field, 2) if width else 0

    def read_root(self):
        return self.read(16) if self.read(1) else 0

    def read_sized(self, most):
        """A number packed so that small ones are short: zero bits up to a
        set bit say how long it is."""
        zeros = 0
        while zeros < most and self.read(1) == 0:
            zeros += 1
        if zeros == most:           # `most` bits long: its top bit, then the rest
            return (1 << (most - 1)) | self.read(most - 1)
        if zeros >= 2:              # zeros - 1 bits after a leading one
            return (1 << (zeros - 1)) | self.read(zeros - 1)
        return zeros                # 0 or 1

    def read_change(self, code):
        """How much a value's rate of change has changed."""
        if code == 1:
            number = self.read_sized(12)
            return -(number >> 1) - 1 if number & 1 else number >> 1
        if code == 2:
            return 0 if self.read(1) else self.read(12)
        if self.read(1) == 0:
            return 0
        negative = self.read(1)
        low = self.read(3)
        size = ((self.read_sized(8) << 3) | low) + 1
        return -size if negative else size

    def frame(self, index):
        """A frame, decoded from the first frame of its group."""
        group, steps = divmod(index, GROUP_FRAMES)
        entry = self.bytes[GROUP_TABLE + 3 * group:GROUP_TABLE + 3 * group + 3]
        self.at = GROUP_TABLE * 8 + int.from_bytes(entry, 'big')

        root = [self.read_root() for _ in range(3)]
        values = [self.read(12) if self.read(1) else self.pose[i] for i in range(PACKED_VALUES)]
        rates = [0] * PACKED_VALUES
        for _ in range(steps):
            root = [self.read_root() for _ in range(3)]
            for i in range(PACKED_VALUES):
                if self.codes[i]:
                    rates[i] += self.read_change(self.codes[i])
                    values[i] += rates[i]
        frame = [value & 0xFFFF for value in root + values]
        for i in SIGNED_VALUES:
            value = frame[i] & 0xFFF
            frame[i] = (value - 0x1000 if value & 0x800 else value) & 0xFFFF
        return frame


FILES = []


def arguments():
    """For the program: each file and how many frames to decode."""
    args = []
    for path in motion_files():
        with open(path, 'rb') as f:
            data = f.read()
        FILES.append((path, data))
        args += [path, frame_count(data)]
    return args


def check(lines, checks):
    at = 0
    for file_index, (path, data) in enumerate(FILES):
        name = os.path.basename(path)
        stream = Stream(data)
        for index in range(frame_count(data)):
            words = lines[at].split()
            at += 1
            checks.equal('%s: the line for frame %d' % (name, index),
                         words[:2], [str(file_index), str(index)])
            got = [int(words[2][i:i + 4], 16) for i in range(0, 4 * FRAME_VALUES, 4)]
            want = stream.frame(index)
            if got != want:
                differ = [i for i in range(FRAME_VALUES) if got[i] != want[i]]
                checks.true('%s frame %d: value %d is 0x%04x, and 0x%04x by the Python decoder'
                            ' (%d values differ)' % (name, index, differ[0], got[differ[0]],
                                                     want[differ[0]], len(differ)), False)
            else:
                checks.true('', True)
    checks.equal('lines left over', len(lines) - at, 0)
