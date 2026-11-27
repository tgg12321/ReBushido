"""A move joined to another file's motion (tools/bbm_join.py): two motion
files made up here, with frames that need every code and a file whose
frames end within a group, one's move pointed at the other's motion; the
joined file read by tests/motion_decode.py's decoder, written from
docs/formats/BBM.md apart from the tool, every frame of every motion the
frame it was made from; the moves, the motions, the marks and the sections'
places checked; a move that is not one refused. With the disc's motion
files (disc/MOTION, not in git), each fighter file's stream packed again
by the tool is the disc's, byte for byte, and the file the same."""
import os
import struct

import bbm_join
import motion_decode
import support

ROWS = 11
HEADER_HALFWORDS = 2 + 27 + 3 + 9 + ROWS * 3
STANDING, ATTACK, OTHER = 0, 8, 14  # the made-up moves' places
COMMAND = 0x00D5


class Numbers:
    """Numbers that look random, the same every run (tests/random.py is a
    test of its own: the standard library's module is not to be had by
    that name here)."""

    def __init__(self, seed):
        self.state = seed

    def next(self):
        self.state = (self.state * 1103515245 + 12345) % (1 << 31)
        return self.state >> 8

    def randrange(self, low, high=None):
        if high is None:
            low, high = 0, low
        return low + self.next() % (high - low)

    def randint(self, low, high):
        return self.randrange(low, high + 1)

    def choice(self, items):
        return items[self.randrange(len(items))]


def made_frames(rng, count, wild):
    """`count` frames (root, 63 values) that move smoothly, the values of a
    group's first frame whole (12 bits); `wild` ones change their rate by
    up to 2,000 a frame, which only some codes can say."""
    frames = []
    values = [rng.randrange(0x1000) for _ in range(bbm_join.PACKED_VALUES)]
    rates = [0] * bbm_join.PACKED_VALUES
    for index in range(count):
        if index % bbm_join.GROUP_FRAMES == 0:
            values = [v % 0x1000 for v in values]
            rates = [0] * bbm_join.PACKED_VALUES
        else:
            for i in range(bbm_join.PACKED_VALUES):
                if i in (5, 6):
                    continue  # never changes within a group: code 0
                if i == 30:
                    # seldom, and then by much and upwards: code 2
                    rates[i] += 3000 if index % bbm_join.GROUP_FRAMES == 3 else 0
                    values[i] = (values[i] + rates[i]) % 0x10000
                    continue
                reach = 2000 if i in wild else 3
                rates[i] += rng.randint(-reach, reach)
                values[i] = (values[i] + rates[i]) % 0x10000
        root = tuple(rng.choice((0, rng.randrange(1, 0x10000))) for _ in range(3))
        frames.append((root, tuple(values)))
    return frames


def made_file(rng, motions, wild):
    """A fighter's motion file: a standing move whose command COMMAND leads
    to the attack at ATTACK, another move, and `motions` (each its length
    and its kind), their frames one after another."""
    header = [3, 0] + [0] * 27 + [STANDING] * 3 + [STANDING] * 9 + [OTHER] * (ROWS * 3)
    moves = [0x100, 0x8000 | STANDING, 0, (20 << 8), 20, COMMAND, 0x8000 | ATTACK, 0,
             0x101, 0x8000 | STANDING, 1, (9 << 8) | 2, 0x0805, 0,
             0x102, 0x8000 | STANDING, 0, 0, 0, 0]
    marks = bytearray()
    entries = []
    first = 0
    for length, kind in motions:
        entries.append((first, len(marks)))
        marks += bytes([kind, length, 0x15, 0x09, 0x80, 2, 0x81, length - 1, 0xFF]) + bytes(6) + b'\0'
        first += length
    frames = made_frames(rng, first, wild)
    pose = list(frames[0][1])
    stream = bbm_join.encode(pose, frames, bbm_join.groups_of(len(frames)))
    out = bytearray(b'MW\0' + bytes([ROWS]) + bytes(16))
    out += struct.pack('<%dH' % HEADER_HALFWORDS, *header)
    out += struct.pack('<%dH' % len(moves), *moves)
    offsets = [len(out)]
    for entry in entries:
        out += struct.pack('<HH', *entry)
    offsets.append(len(out))
    out += marks
    out += bytes(-len(out) % 4)
    offsets.append(len(out))
    out += b'CPU!'
    offsets.append(len(out))
    out += stream
    struct.pack_into('<4I', out, 4, *offsets)
    return bytes(out), frames


def run(checks):
    rng = Numbers(1998)
    # 61 frames: the file's frames end within a group
    into_data, into_frames = made_file(rng, [(20, 1), (41, 2)], wild=())
    from_data, from_frames = made_file(rng, [(13, 1), (30, 2), (27, 2)], wild=(9, 40, 51))
    into = bbm_join.MotionFile(into_data, 'into')
    source = bbm_join.MotionFile(from_data, 'from')
    checks.equal('a made-up file read and written again is the same', into.bytes(), into_data)
    checks.equal("the made-up files' moves", (list(into.each_move()), list(source.each_move())),
                 ([STANDING, ATTACK, OTHER], [STANDING, ATTACK, OTHER]))
    checks.equal('the command from standing', into.commands(STANDING), [(COMMAND, 0x8000 | ATTACK)])
    _, codes, _ = bbm_join.decode(source.stream, source.frame_count)
    checks.equal("the wild file's codes: every one", sorted(set(codes)), [0, 1, 2, 3])

    # the attack of `into` plays the third motion of `from`, which begins
    # within a group (frame 43)
    source.move_halfwords(ATTACK)
    words = list(struct.unpack_from('<5H', source.moves, ATTACK * 2))
    words[2] = 2
    struct.pack_into('<5H', source.moves, ATTACK * 2, *words)
    joined = bbm_join.join(into, bbm_join.MotionFile(source.bytes(), 'from'), [(ATTACK, ATTACK)],
                           say=lambda line: None)
    again = bbm_join.MotionFile(joined, 'joined')
    checks.equal('the joined file has a motion more', len(again.motions), 3)
    first, _ = again.motions[2]
    checks.equal("its first frame: after the file's, in the place in its group it had",
                 first, 64 + 43 % 8)
    checks.equal('its marks are the ones brought', again.motion_marks(2),
                 bbm_join.MotionFile(from_data, 'from').motion_marks(2))
    checks.equal("the move plays it, the source move's frames and last command frame",
                 again.move_halfwords(ATTACK)[2:], (2, (9 << 8) | 2, 0x0805))
    checks.equal('the other moves are as they were',
                 (again.move_halfwords(STANDING), again.move_halfwords(OTHER), again.commands(STANDING)),
                 (into.move_halfwords(STANDING), into.move_halfwords(OTHER), into.commands(STANDING)))
    checks.equal('the CPU section is carried', again.cpu, b'CPU!')
    offsets = struct.unpack_from('<4I', joined, 4)
    checks.true('every section on a word', all(o % 4 == 0 for o in offsets))
    # the documented decoder, apart from the tool
    stream = motion_decode.Stream(joined)
    want = list(into_frames) + [None] * 3 + from_frames[40:70]
    for index in list(range(61)) + list(range(64 + 3, 64 + 3 + 27)):
        checks.equal('frame %d decoded from the format' % index, list(stream.frame(index)),
                      list(bbm_join.shown(want[index])))
    # packed again from what it decodes to, the frames between too, the
    # stream is the same: the codes are the fewest bits
    starts = bbm_join.groups_of(56) + [(56, 8)] + bbm_join.groups_of(30, 64)
    pose, _, decoded = bbm_join.decode(again.stream, again.frame_count, starts)
    checks.equal('the joined stream packed again is the same',
                 bbm_join.encode(pose, decoded, starts), again.stream)
    try:
        bbm_join.join(bbm_join.MotionFile(into_data, 'into'), source, [(3, ATTACK)],
                      say=lambda line: None)
        checks.true('a place that is not a move is refused', False)
    except SystemExit as stop:
        checks.true('a place that is not a move is refused: %s' % stop, 'no move begins at 3' in str(stop))

    # the disc's own files, where they are
    try:
        folder = support.disc_file('MOTION')
    except support.Skip:
        return
    for name in sorted(os.listdir(folder)):
        if not name.upper().endswith('.BBM') or name.upper() == 'COMMON.BBM':
            continue
        with open(os.path.join(folder, name), 'rb') as f:
            data = f.read()
        motion_file = bbm_join.MotionFile(data, name)
        pose, _, frames = bbm_join.decode(motion_file.stream, motion_file.frame_count)
        checks.equal('%s: its stream packed again is the disc\'s, byte for byte' % name,
                     bbm_join.encode(pose, frames, bbm_join.groups_of(len(frames))), motion_file.stream)
        checks.equal('%s: the file written again is the disc\'s' % name, motion_file.bytes(), data)
