#!/usr/bin/env python3
"""bbm_join.py - a move of one motion file joined to another file's motion
(docs/MODS.md, "A move joined to another file's motion"; the format is
docs/formats/BBM.md).

A move plays a motion of its own file: the motion is a number in the
file's table, its marks are in the file's marks and its frames in the
file's stream, packed with the file's own codes. So a move cannot name
another file's motion: the motion is copied in. The frames of the other
file's motion are decoded, and the whole stream is packed again, the
file's own frames and the new ones, with codes that hold both; the motion
and its marks are added at the ends of their sections; the move is pointed
at the new motion, and plays the frames the other file's move played.
Nothing else of the file changes: its moves, the commands that lead to
them, the CPU's scripts.

The stream is packed again losslessly: every frame of the file, and every
frame of the motion brought in, decodes to the values it had (the tool
decodes the new stream and compares before it writes anything).

    bbm_join.py moves FILE.bbm
        every move of the file: its place (what the file's moves and the
        header name it by, in halfwords), the motion it plays and its
        frames, the motion's kind and length, and what the header calls it
    bbm_join.py join INTO.bbm FROM.bbm --move P --from-move Q -o OUT.bbm
        OUT is INTO with its move at P playing the motion FROM's move at Q
        plays, over the same frames; several pairs may be given
        (--move P1 --from-move Q1 --move P2 --from-move Q2)
    bbm_join.py check FILE.bbm
        the stream packed again and decoded again, every frame compared;
        and how large the file would be

The joined file is larger than the one it was made from (by the motion's
frames, its marks and four bytes): a mod reads it as a weapon's own motions
(`motions = KEY`, room for the disc's largest set's file, 292,724 bytes),
not in place of the disc's file of the same name, which must not grow.
"""

import argparse
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HEADER_SIZE = 0x14
MOVES_BEFORE_ROWS = 0x66  # a fighter file's moves follow its rows from here
ROW_BYTES = 6  # a row of per-stance moves: three halfwords
STANCES = 3
CHARACTERS = 27
MOVE_HEAD = 5  # halfwords before a move's commands
COMMAND_LONG = 0xC000  # a command whose flags have bit 14 or 15 is four halfwords
OWN_FILE = 0x8000  # a move id of the fighter's own file

PACKED_VALUES = 63
ROOT_VALUES = 3
VALUE_BITS = 12
ROOT_BITS = 16
CODE_BITS = 2
GROUP_FRAMES = 8
GROUP_TABLE = 0x70  # bytes into the stream
ENTRY_BYTES = 3
WORD = 4
# the 17th and the 19th bones' triples: their low 12 bits are signed, the
# rest of their 16 is not read (motion_decode.c)
SIGNED_VALUES = (51, 52, 53, 57, 58, 59)  # of the 63 packed values
FULL = 0x10000
LOW = 0x1000
# what each code can say of a change in a rate (docs/formats/BBM.md)
CODE_FIXED, CODE_SMALL, CODE_RARE, CODE_WIDE = 0, 1, 2, 3
SMALL_MOST = 12
WIDE_MOST = 8
WIDE_LOW = 3
MOTION_LENGTH_MOST = 255  # a move's first and last frames are bytes

# A mark (a byte of 0x80 or more) is followed by its frame; a byte below
# 0x80 by one byte; 0xFF by six.
MARK_FIRST = 0x80
MARK_LONG = 0xFF
MARK_LONG_BYTES = 6
MARKS_HEAD = 4


# ---- the file ----

class MotionFile:
    """A fighter's motion file, its sections apart."""

    def __init__(self, data, name):
        self.name = name
        if len(data) < HEADER_SIZE or data[:2] not in (b'MW', b'MC'):
            raise SystemExit('%s: not a motion file' % name)
        if data[:2] == b'MC':
            raise SystemExit('%s: the common file has no stances to join a move in; '
                             'a fighter file (MW) is needed' % name)
        self.head = bytearray(data[:HEADER_SIZE])
        self.rows = data[3]
        offsets = struct.unpack_from('<4I', data, 4)
        self.moves_at = MOVES_BEFORE_ROWS + self.rows * ROW_BYTES
        self.header = bytearray(data[HEADER_SIZE:self.moves_at])
        self.moves = bytearray(data[self.moves_at:offsets[0]])
        self.motions = [struct.unpack_from('<HH', data, at) for at in range(offsets[0], offsets[1], 4)]
        self.marks = bytearray(data[offsets[1]:offsets[2]])
        self.cpu = bytes(data[offsets[2]:offsets[3]])
        self.stream = bytes(data[offsets[3]:])
        self.frame_count = max(first + self.motion_length(i)
                               for i, (first, _) in enumerate(self.motions))

    def motion_length(self, motion):
        return self.marks[self.motions[motion][1] + 1]

    def motion_kind(self, motion):
        return self.marks[self.motions[motion][1]]

    def motion_marks(self, motion):
        """The motion's head and marks, to the zero that ends them."""
        at = self.motions[motion][1]
        end = at + MARKS_HEAD
        while self.marks[end] != 0:
            byte = self.marks[end]
            end += 1 + (MARK_LONG_BYTES if byte == MARK_LONG else 1)
        return bytes(self.marks[at:end + 1])

    def move_halfwords(self, place):
        return struct.unpack_from('<%dH' % MOVE_HEAD, self.moves, place * 2)

    def each_move(self):
        """Each move's place, in halfwords, in the file's order."""
        place = 0
        count = len(self.moves) // 2
        while place + MOVE_HEAD <= count:
            yield place
            at = place + MOVE_HEAD
            while at < count:
                flags = struct.unpack_from('<H', self.moves, at * 2)[0]
                if flags == 0:
                    at += 1
                    break
                at += 4 if flags & COMMAND_LONG else 2
            place = at

    def commands(self, place):
        """A move's commands: (flags, the move id they lead to)."""
        out = []
        at = place + MOVE_HEAD
        count = len(self.moves) // 2
        while at < count:
            flags = struct.unpack_from('<H', self.moves, at * 2)[0]
            if flags == 0:
                break
            wide = 4 if flags & COMMAND_LONG else 2
            words = struct.unpack_from('<%dH' % wide, self.moves, at * 2)
            out.append((flags, words[-1]))
            at += wide
        return out

    def names(self):
        """What the header calls a move, by its place."""
        said = {}
        words = struct.unpack_from('<%dH' % (len(self.header) // 2), self.header)
        # stance count, entrance (common), 27 win poses (common)
        at = 2 + CHARACTERS
        for stance in range(STANCES):
            said.setdefault(words[at + stance], 'standing in stance %d' % (stance + 1))
        at += STANCES
        for one in range(STANCES):
            for other in range(STANCES):
                said.setdefault(words[at + one * STANCES + other],
                                'from stance %d to %d' % (one + 1, other + 1))
        at += STANCES * STANCES
        for row in range(self.rows):
            for stance in range(STANCES):
                said.setdefault(words[at + row * STANCES + stance],
                                'row %d of stance %d' % (row, stance + 1))
        return said

    def bytes(self):
        """The file again, its sections each on a word."""
        def align(n):
            return (n + WORD - 1) // WORD * WORD
        out = bytearray(self.head) + self.header + self.moves
        offsets = [align(len(out))]
        out += bytes(offsets[0] - len(out))
        for first, marks in self.motions:
            out += struct.pack('<HH', first, marks)
        offsets.append(len(out))
        out += self.marks
        offsets.append(align(len(out)))
        out += bytes(offsets[2] - len(out))
        out += self.cpu
        offsets.append(align(len(out)))
        out += bytes(offsets[3] - len(out))
        out += self.stream
        struct.pack_into('<4I', out, 4, *offsets)
        return bytes(out)


# ---- the stream ----

class BitReader:
    def __init__(self, data):
        count = len(data) // WORD
        words = struct.unpack_from('<%dI' % count, data)
        self.bits = ''.join(format(word, '032b') for word in words)
        self.at = 0

    def read(self, width):
        field = self.bits[self.at:self.at + width]
        self.at += width
        return int(field, 2) if width else 0

    def sized(self, most):
        zeros = 0
        while zeros < most and self.read(1) == 0:
            zeros += 1
        if zeros == most:
            return (1 << (most - 1)) | self.read(most - 1)
        if zeros >= 2:
            return (1 << (zeros - 1)) | self.read(zeros - 1)
        return zeros


def decode(stream, frame_count, starts=None):
    """The stream's opening pose, its codes, and its frames: each the
    root's three values and the 63 packed values as the decoder holds them
    (16 bits; motion_decode.c gives the signed ones' low 12 a sign). The
    groups and how many frames of each are decoded are `starts`' (each
    group's first frame and its count), or every frame to `frame_count`;
    a frame of no group is None."""
    reader = BitReader(stream)
    pose = [reader.read(VALUE_BITS) for _ in range(PACKED_VALUES)]
    codes = [reader.read(CODE_BITS) for _ in range(PACKED_VALUES)]
    if starts is None:
        starts = groups_of(frame_count)
    frames = [None] * frame_count
    for start, count in starts:
        at = GROUP_TABLE + ENTRY_BYTES * (start // GROUP_FRAMES)
        reader.at = GROUP_TABLE * 8 + int.from_bytes(stream[at:at + ENTRY_BYTES], 'big')
        values = None
        rates = [0] * PACKED_VALUES
        for step in range(count):
            root = [reader.read(ROOT_BITS) if reader.read(1) else 0 for _ in range(ROOT_VALUES)]
            if step == 0:
                values = [reader.read(VALUE_BITS) if reader.read(1) else pose[i]
                          for i in range(PACKED_VALUES)]
            else:
                for i in range(PACKED_VALUES):
                    if codes[i] == CODE_FIXED:
                        continue
                    if codes[i] == CODE_SMALL:
                        n = reader.sized(SMALL_MOST)
                        change = -(n >> 1) - 1 if n & 1 else n >> 1
                    elif codes[i] == CODE_RARE:
                        change = 0 if reader.read(1) else reader.read(VALUE_BITS)
                    elif reader.read(1) == 0:
                        change = 0
                    else:
                        negative = reader.read(1)
                        low = reader.read(WIDE_LOW)
                        change = ((reader.sized(WIDE_MOST) << WIDE_LOW) | low) + 1
                        change = -change if negative else change
                    rates[i] = (rates[i] + change) % FULL
                    values[i] = (values[i] + rates[i]) % FULL
            frames[start + step] = (tuple(root), tuple(values))
    return pose, codes, frames


def shown(frame):
    """A frame as the game is given it: the signed values' low 12 bits with
    a sign."""
    root, values = frame
    out = list(root) + list(values)
    for i in SIGNED_VALUES:
        low = values[i] % LOW
        out[ROOT_VALUES + i] = (low - LOW if low & (LOW >> 1) else low) % FULL
    return tuple(out)


def needed_changes(frames, starts):
    """For each packed value, the change in its rate each frame after a
    group's first needs: modulo 65,536, or 4,096 for the signed ones (whose
    other bits are not read). `starts` says where each group begins. A
    frame that is None (between the file's last frame and a group brought
    in) keeps every rate: no change."""
    changes = [[] for _ in range(PACKED_VALUES)]
    for start, count in starts:
        for i in range(PACKED_VALUES):
            modulo = LOW if i in SIGNED_VALUES else FULL
            value = frames[start][1][i]
            rate = 0
            for step in range(1, count):
                if frames[start + step] is None:
                    change = 0
                else:
                    change = (frames[start + step][1][i] - value - rate) % modulo
                changes[i].append(change)
                rate = (rate + change) % modulo
                value = (value + rate) % modulo
    return changes


def signed(change, modulo):
    return change - modulo if change >= modulo // 2 else change


def sized_bits(n, most):
    if n == 0:
        return '1'
    if n == 1:
        return '01'
    length = n.bit_length()
    if length > most:
        return None
    rest = format(n, 'b')[1:]
    if length == most:
        return '0' * most + rest
    return '0' * length + '1' + rest


def change_bits(code, change, modulo):
    """A change in a rate in a code, or None where the code cannot say it:
    the change is taken as the decoder will add it, modulo `modulo`."""
    if code == CODE_FIXED:
        return '' if change == 0 else None
    if code == CODE_RARE:
        if change == 0:
            return '1'
        # twelve bits, added as they are
        if modulo == LOW or change < LOW:
            return '0' + format(change % LOW, '012b')
        return None
    s = signed(change, modulo)
    if code == CODE_SMALL:
        n = 2 * s if s >= 0 else -2 * s - 1
        return sized_bits(n, SMALL_MOST)
    if s == 0:
        return '0'
    size = abs(s) - 1
    rest = sized_bits(size >> WIDE_LOW, WIDE_MOST)
    if rest is None:
        return None
    return '1' + ('1' if s < 0 else '0') + format(size & ((1 << WIDE_LOW) - 1), '03b') + rest


def choose_codes(changes):
    """For each value the code that says every change it needs in the
    fewest bits."""
    codes = []
    for i, values in enumerate(changes):
        modulo = LOW if i in SIGNED_VALUES else FULL
        best = None
        for code in (CODE_FIXED, CODE_SMALL, CODE_RARE, CODE_WIDE):
            total = 0
            for change in values:
                bits = change_bits(code, change, modulo)
                if bits is None:
                    total = None
                    break
                total += len(bits)
            if total is not None and (best is None or total < best[0]):
                best = (total, code)
        if best is None:
            raise SystemExit('value %d of the frames changes more than any of the codes can say '
                             'in a frame: the motions cannot share one file' % i)
        codes.append(best[1])
    return codes


def encode(pose, frames, starts):
    """A stream of `frames`, in groups as `starts` says (each group's first
    frame and its count), with the opening pose `pose` and codes that hold
    every change: its bytes."""
    changes = needed_changes(frames, starts)
    codes = choose_codes(changes)
    head = ''.join(format(v, '012b') for v in pose) + ''.join(format(c, '02b') for c in codes)
    head += '0' * (GROUP_TABLE * 8 - len(head))
    table_bytes = ENTRY_BYTES * (starts[-1][0] // GROUP_FRAMES + 1)
    while (GROUP_TABLE + table_bytes) % WORD:
        table_bytes += 1
    body = []
    at = table_bytes * 8
    places = [0] * (starts[-1][0] // GROUP_FRAMES + 1)
    for start, count in starts:
        places[start // GROUP_FRAMES] = at
        rates = [0] * PACKED_VALUES
        values = list(frames[start][1])
        for step in range(count):
            if frames[start + step] is None:
                # a frame no motion plays: every rate as it was
                root, target = (0, 0, 0), [(values[i] + rates[i]) % FULL
                                           for i in range(PACKED_VALUES)]
            else:
                root, target = frames[start + step]
            bits = ''.join('1' + format(v, '016b') if v else '0' for v in root)
            if step == 0:
                for i in range(PACKED_VALUES):
                    v = target[i]
                    if v == pose[i]:
                        bits += '0'
                    elif v < LOW:
                        bits += '1' + format(v, '012b')
                    else:
                        raise SystemExit('a group\'s first frame has a value of more than 12 bits')
            else:
                for i in range(PACKED_VALUES):
                    if codes[i] == CODE_FIXED:
                        continue
                    modulo = LOW if i in SIGNED_VALUES else FULL
                    change = (target[i] - values[i] - rates[i]) % modulo
                    bits += change_bits(codes[i], change, modulo)
                    # as the decoder adds it: twelve bits of a rare code
                    # are added as they are, a signed change as its sign
                    if codes[i] == CODE_RARE:
                        added = change % LOW if modulo == LOW else change
                    else:
                        added = signed(change, modulo)
                    rates[i] = (rates[i] + added) % FULL
                    values[i] = (values[i] + rates[i]) % FULL
            body.append(bits)
            at += len(bits)
    if at >= 1 << (8 * ENTRY_BYTES):
        raise SystemExit('the frames are more than a group table can reach')
    table = b''.join(place.to_bytes(ENTRY_BYTES, 'big') for place in places)
    table += bytes(table_bytes - len(table))
    bits = ''.join(body)
    bits += '0' * (-len(bits) % 32)
    head_bytes = pack_bits(head)
    return head_bytes + table + pack_bits(bits)


def pack_bits(bits):
    return b''.join(struct.pack('<I', int(bits[i:i + 32], 2)) for i in range(0, len(bits), 32))


def groups_of(count, first=0):
    return [(first + g, min(GROUP_FRAMES, count - g)) for g in range(0, count, GROUP_FRAMES)]


# ---- what the tool does ----

def read_file(path):
    with open(path, 'rb') as f:
        return MotionFile(f.read(), path)


def kind_names():
    """enum MoveKind's names (include/motion.h), where the source is."""
    try:
        with open(os.path.join(REPO, 'include', 'motion.h'), encoding='utf-8') as f:
            text = f.read()
    except OSError:
        return {}
    return {int(n): name.lower() for name, n in re.findall(r'MOVE_KIND_(\w+) = (\d+)', text)}


def cmd_moves(path):
    """Every move, with what the header calls it and the commands that lead
    to it from standing in a stance: the same command leads to the moves
    two files have for the same thing (the first blow of a stance, say)."""
    motion_file = read_file(path)
    names = motion_file.names()
    kinds = kind_names()
    led = {}
    for place, said in sorted(names.items()):
        if not said.startswith('standing in stance'):
            continue
        for flags, target in motion_file.commands(place):
            if target & OWN_FILE:
                led.setdefault(target & ~OWN_FILE, []).append(
                    'stance %s command %04x' % (said[-1], flags))
    print('%s: %d moves, %d motions, %d frames' % (
        path, sum(1 for _ in motion_file.each_move()), len(motion_file.motions),
        motion_file.frame_count))
    print('place  motion  frames   kind          length  what leads to it')
    for place in motion_file.each_move():
        _, _, motion, frames, _ = motion_file.move_halfwords(place)
        if motion >= len(motion_file.motions):
            print('%5d  %6d  (no such motion)' % (place, motion))
            continue
        kind = motion_file.motion_kind(motion)
        said = [names[place]] if place in names else []
        print('%5d  %6d  %3d-%-3d  %-12s  %6d  %s' % (
            place, motion, frames & 0xFF, frames >> 8, kinds.get(kind, str(kind)),
            motion_file.motion_length(motion), '; '.join(said + led.get(place, []))))


def check_lossless(old_frames, new_stream, starts, what):
    """Every frame of the groups decoded from the new stream as it was."""
    _, _, again = decode(new_stream, len(old_frames), starts)
    for start, count in starts:
        for index in range(start, start + count):
            if old_frames[index] is None:
                continue
            if again[index] is None or shown(old_frames[index]) != shown(again[index]):
                raise SystemExit('%s: frame %d does not decode as it did; nothing written'
                                 % (what, index))


def cmd_check(path):
    motion_file = read_file(path)
    pose, _, frames = decode(motion_file.stream, motion_file.frame_count)
    stream = encode(pose, frames, groups_of(len(frames)))
    check_lossless(frames, stream, groups_of(len(frames)), path)
    print('%s: %d frames packed again and decoded again, every one the same; the stream %d '
          'bytes (it was %d)' % (path, len(frames), len(stream), len(motion_file.stream)))


def join(into, source, pairs, say=print):
    """The bytes of `into` (a MotionFile) with each move of `pairs` (its
    place, and the place of `source`'s move) playing `source`'s move's
    motion over its frames; checked as the module says."""
    pose, _, frames = decode(into.stream, into.frame_count)
    _, _, source_frames = decode(source.stream, source.frame_count)
    own_places = set(into.each_move())
    source_places = set(source.each_move())
    starts = groups_of(len(frames))
    brought = {}
    for place, from_place in pairs:
        if place not in own_places:
            raise SystemExit('%s: no move begins at %d (bbm_join.py moves lists them)'
                             % (into.name, place))
        if from_place not in source_places:
            raise SystemExit('%s: no move begins at %d' % (source.name, from_place))
        _, _, motion, played, last_and_flags = source.move_halfwords(from_place)
        if motion not in brought:
            first, _ = source.motions[motion]
            length = source.motion_length(motion)
            # the motion's groups whole from the one its first frame is in
            # (a group's first frame is the only one whose values are
            # whole), after the file's last group
            group_first = first - first % GROUP_FRAMES
            begin = len(frames) + (-len(frames) % GROUP_FRAMES)
            count = first + length - group_first
            # the frames between end the file's last group: no motion
            # plays them, and they keep its rates
            if begin != len(frames):
                starts[-1] = (starts[-1][0], GROUP_FRAMES)
            frames += [None] * (begin - len(frames))
            frames += source_frames[group_first:first + length]
            starts += groups_of(count, begin)
            new_motion = len(into.motions)
            marks = source.motion_marks(motion)
            into.motions.append((begin + first % GROUP_FRAMES, len(into.marks)))
            into.marks += marks
            brought[motion] = new_motion
            say('%s: motion %d of %s (%d frames, kind %d) brought in as motion %d' % (
                into.name, motion, source.name, length, marks[0], new_motion))
        words = list(into.move_halfwords(place))
        words[2] = brought[motion]
        words[3] = played
        # the last frame a command is taken at is the motion's; the move's
        # flags stay its own
        words[4] = (words[4] & 0xFF00) | (last_and_flags & 0xFF)
        struct.pack_into('<%dH' % MOVE_HEAD, into.moves, place * 2, *words)
        say('%s: the move at %d plays it, frames %d to %d, commands taken to frame %d '
            '(as the move at %d of %s)' % (into.name, place, played & 0xFF, played >> 8,
                                           last_and_flags & 0xFF, from_place, source.name))
    into.stream = encode(pose, frames, starts)
    check_lossless(frames, into.stream, starts, into.name)
    data = into.bytes()
    if MotionFile(data, into.name).frame_count != len(frames):
        raise SystemExit('%s: the motions do not end where the frames do; nothing written'
                         % into.name)
    return data


def cmd_join(into_path, from_path, pairs, out_path):
    with open(into_path, 'rb') as f:
        was = len(f.read())
    data = join(read_file(into_path), read_file(from_path), pairs)
    with open(out_path, 'wb') as f:
        f.write(data)
    print('%s: %d bytes (%s was %d); every frame of both decoded again, each as it was' % (
        out_path, len(data), into_path, was))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = parser.add_subparsers(dest='command', required=True)
    moves = sub.add_parser('moves', help='list a file\'s moves')
    moves.add_argument('file')
    check = sub.add_parser('check', help='pack a file\'s stream again and compare')
    check.add_argument('file')
    join = sub.add_parser('join', help='a move joined to another file\'s motion')
    join.add_argument('into')
    join.add_argument('source')
    join.add_argument('--move', type=int, action='append', required=True,
                      help='the move of INTO, by its place')
    join.add_argument('--from-move', type=int, action='append', required=True,
                      help='the move of FROM whose motion and frames it plays')
    join.add_argument('-o', '--out', required=True)
    args = parser.parse_args()
    if args.command == 'moves':
        cmd_moves(args.file)
    elif args.command == 'check':
        cmd_check(args.file)
    else:
        if len(args.move) != len(args.from_move):
            parser.error('each --move needs its --from-move')
        cmd_join(args.into, args.source, list(zip(args.move, args.from_move)), args.out)


if __name__ == '__main__':
    sys.exit(main())
