#!/usr/bin/env python3
"""A PlayStation memory card image: list it, check it, take a file out, put
one in (docs/PORT.md, "The memory card").

    python tools/card.py list FILE               the identifier, the files, the free blocks
    python tools/card.py check FILE              every rule of the format; exit 1 if one is broken
    python tools/card.py extract FILE NAME OUT   the bytes of one file of the card
    python tools/card.py insert FILE NAME IN     those bytes back, making the file if the card has none
    python tools/card.py delete FILE NAME        frees the file's blocks, as the BIOS does
    python tools/card.py format OUT              a new card image, formatted and empty

The images the native port reads and writes (src/port/card.c, --card1 and
--card2) are ordinary card images, and so are the ones an emulator writes:
this reads either. The format is public knowledge and is written out here
from the rules, not from any emulator's code.

A card is 128 KB: a header block and fifteen blocks of 8192 bytes, a block
64 frames of 128 bytes. The header block is

    frame  0        the identifier, "MC"
    frames 1 to 15  a directory frame for each of the fifteen blocks
    frames 16 to 35 the list of broken frames, twenty entries
    frames 36 to 55 the frames that replace them
    frame  63       the identifier again

and every frame's last byte is the exclusive or of its other 127. A
directory frame is

    0       the block's state: 0x51 the first block of a file, 0x52 a
            middle one, 0x53 the last, 0xA0 to 0xA3 free (the low half
            says what the block was, which a freed one keeps), 0xFF
            unusable
    4       the whole file's size in bytes, in the frame of its first
            block, low byte first
    8       the next block of the file, counted from 0, or 0xFFFF
    10      the file's name, at most twenty characters and a zero

Nothing here reads or writes game data: a card image is the player's own.
"""
import os
import sys

FRAME_SIZE = 128
BLOCK_SIZE = 0x2000
FRAMES_PER_BLOCK = BLOCK_SIZE // FRAME_SIZE
BLOCKS = 15
IMAGE_SIZE = (BLOCKS + 1) * BLOCK_SIZE

ID_FRAME = 0
DIR_FRAME = 1
BROKEN_FRAME = 16
BROKEN_ENTRIES = 20
ID_COPY_FRAME = 63
SUM_AT = FRAME_SIZE - 1

IDENTIFIER = b'MC'
NOT_BROKEN = 0xFFFFFFFF
NO_NEXT = 0xFFFF

DIR_STATE = 0
DIR_SIZE = 4
DIR_NEXT = 8
DIR_NAME = 10
NAME_SIZE = 20

FREE = 0xA0
FIRST = 0x51
MIDDLE = 0x52
LAST = 0x53
UNUSABLE = 0xFF
KIND_MASK = 0xF0
PART_MASK = 0x0F

STATE_WORDS = {FIRST: 'first', MIDDLE: 'middle', LAST: 'last'}


def frame_sum(frame):
    """The byte a frame's last one is: the exclusive or of the other 127."""
    sum_ = 0
    for byte in frame[:SUM_AT]:
        sum_ ^= byte
    return sum_


def read_image(path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())
    if len(data) != IMAGE_SIZE:
        raise SystemExit('card.py: %s is %d bytes; a card image is %d'
                         % (path, len(data), IMAGE_SIZE))
    return data


def frame_at(data, index):
    return data[index * FRAME_SIZE:(index + 1) * FRAME_SIZE]


def dir_at(data, block):
    """The directory frame of block `block` (1 to 15)."""
    return frame_at(data, DIR_FRAME + block - 1)


def half(frame, at):
    return frame[at] | (frame[at + 1] << 8)


def word(frame, at):
    return half(frame, at) | (half(frame, at + 2) << 16)


def put_half(data, at, value):
    data[at] = value & 0xFF
    data[at + 1] = (value >> 8) & 0xFF


def put_word(data, at, value):
    put_half(data, at, value & 0xFFFF)
    put_half(data, at + 2, (value >> 16) & 0xFFFF)


def name_of(frame):
    """A directory frame's name, which may fill its twenty characters with
    no zero after them."""
    raw = bytes(frame[DIR_NAME:DIR_NAME + NAME_SIZE])
    return raw.split(b'\0')[0].decode('ascii', 'replace')


def next_block(frame):
    """The block a file goes on in (1 to 15), or 0 where it ends."""
    link = half(frame, DIR_NEXT)
    return link + 1 if link < BLOCKS else 0


def chain_of(data, block):
    """The blocks of the file that begins in `block`, following the links;
    it stops at a block it has already been through."""
    blocks = []
    while block != 0 and block not in blocks and 1 <= block <= BLOCKS:
        blocks.append(block)
        block = next_block(dir_at(data, block))
    return blocks


def files_of(data):
    """(name, first block, size, chain) for each file of the card, and the
    same for the files a delete has freed, in block order."""
    live, freed = [], []
    for block in range(1, BLOCKS + 1):
        frame = dir_at(data, block)
        state = frame[DIR_STATE]
        if state == FIRST:
            live.append((name_of(frame), block, word(frame, DIR_SIZE),
                         chain_of(data, block)))
        elif (state & KIND_MASK) == (FREE & KIND_MASK) and \
                (state & PART_MASK) == (FIRST & PART_MASK) and \
                name_of(frame):
            freed.append((name_of(frame), block, word(frame, DIR_SIZE),
                          chain_of(data, block)))
    return live, freed


def free_blocks(data):
    return [block for block in range(1, BLOCKS + 1)
            if (dir_at(data, block)[DIR_STATE] & KIND_MASK) ==
            (FREE & KIND_MASK)]


def file_bytes(data, chain, size):
    """The bytes of a file, its blocks in order, cut to its size."""
    out = bytearray()
    for block in chain:
        out += data[block * BLOCK_SIZE:(block + 1) * BLOCK_SIZE]
    return bytes(out[:size])


def formatted_image():
    """A new card image: the header block as a format leaves it, and
    fifteen blocks of zeroes."""
    data = bytearray(IMAGE_SIZE)
    data[ID_FRAME * FRAME_SIZE:ID_FRAME * FRAME_SIZE + 2] = IDENTIFIER
    data[ID_COPY_FRAME * FRAME_SIZE:ID_COPY_FRAME * FRAME_SIZE + 2] = IDENTIFIER
    for block in range(1, BLOCKS + 1):
        at = (DIR_FRAME + block - 1) * FRAME_SIZE
        data[at + DIR_STATE] = FREE
        put_half(data, at + DIR_NEXT, NO_NEXT)
    for entry in range(BROKEN_ENTRIES):
        at = (BROKEN_FRAME + entry) * FRAME_SIZE
        put_word(data, at, NOT_BROKEN)
        put_half(data, at + DIR_NEXT, NO_NEXT)
    for index in range(FRAMES_PER_BLOCK):
        at = index * FRAME_SIZE
        data[at + SUM_AT] = frame_sum(data[at:at + FRAME_SIZE])
    return data


def problems_of(data):
    """Everything about the image that is not as the format says."""
    said = []
    if bytes(frame_at(data, ID_FRAME)[:2]) != IDENTIFIER:
        said.append('frame 0 does not begin with "MC": the card is not '
                    'formatted')
    if bytes(frame_at(data, ID_COPY_FRAME)[:2]) != IDENTIFIER:
        said.append('frame 63, the identifier again, does not begin with "MC"')
    for index in range(FRAMES_PER_BLOCK):
        frame = frame_at(data, index)
        if frame[SUM_AT] != frame_sum(frame):
            said.append('frame %d: its last byte is 0x%02X and the exclusive '
                        'or of the others is 0x%02X'
                        % (index, frame[SUM_AT], frame_sum(frame)))
    owner = {}
    for block in range(1, BLOCKS + 1):
        frame = dir_at(data, block)
        state = frame[DIR_STATE]
        if state not in (FIRST, MIDDLE, LAST, UNUSABLE) and \
                (state & KIND_MASK) != (FREE & KIND_MASK):
            said.append('block %d: 0x%02X is no state a block has' % (block, state))
        if state != FIRST:
            continue
        chain = chain_of(data, block)
        walk = block
        while True:
            link = next_block(dir_at(data, walk))
            if link == 0:
                break
            if link in chain[:chain.index(walk) + 1]:
                said.append('block %d: the file "%s" links round on itself'
                            % (block, name_of(frame)))
                break
            walk = link
        for part in chain:
            if part in owner:
                said.append('block %d is in two files, "%s" and "%s"'
                            % (part, owner[part], name_of(frame)))
            owner[part] = name_of(frame)
        for part in chain[1:]:
            if dir_at(data, part)[DIR_STATE] not in (MIDDLE, LAST):
                said.append('block %d is in the file "%s" and does not say '
                            'it is a middle or last block'
                            % (part, name_of(frame)))
        size = word(frame, DIR_SIZE)
        if size != len(chain) * BLOCK_SIZE:
            said.append('the file "%s" says it is %d bytes and has %d blocks'
                        % (name_of(frame), size, len(chain)))
    for entry in range(BROKEN_ENTRIES):
        frame = frame_at(data, BROKEN_FRAME + entry)
        broken = word(frame, 0)
        if broken != NOT_BROKEN and broken >= BLOCKS * FRAMES_PER_BLOCK:
            said.append('the list of broken frames names frame %d, which is '
                        'past the card' % broken)
    return said


def do_list(path):
    data = read_image(path)
    live, freed = files_of(data)
    free = free_blocks(data)
    print('%s: %d bytes, identifier %s'
          % (path, len(data),
             'MC' if bytes(frame_at(data, ID_FRAME)[:2]) == IDENTIFIER
             else 'not "MC" (the card is not formatted)'))
    print('%-21s %-7s %-9s %s' % ('name', 'blocks', 'bytes', 'the blocks'))
    for name, block, size, chain in live:
        print('%-21s %-7d %-9d %s'
              % (name, len(chain), size, ' '.join(str(b) for b in chain)))
    for name, block, size, chain in freed:
        print('%-21s %-7d %-9d %s   (freed by a delete)'
              % (name, len(chain), size, ' '.join(str(b) for b in chain)))
    print('%d of the %d blocks are free: %s'
          % (len(free), BLOCKS, ' '.join(str(b) for b in free) or 'none'))
    return 0


def do_check(path):
    data = read_image(path)
    said = problems_of(data)
    if said:
        print('%s: %d things are not as the format says:' % (path, len(said)))
        for line in said:
            print('  ' + line)
        return 1
    live, _ = files_of(data)
    print('OK: %s: a memory card image of %d bytes, %d file%s, %d free blocks'
          % (path, len(data), len(live), '' if len(live) == 1 else 's',
             len(free_blocks(data))))
    return 0


def find_file(data, name):
    live, _ = files_of(data)
    for found, block, size, chain in live:
        if found == name:
            return block, size, chain
    raise SystemExit('card.py: the card has no file called "%s"' % name)


def do_extract(path, name, out):
    data = read_image(path)
    _, size, chain = find_file(data, name)
    with open(out, 'wb') as f:
        f.write(file_bytes(data, chain, size))
    print('%s: %d bytes of "%s" to %s' % (path, size, name, out))
    return 0


def create_file(data, name, blocks):
    """Makes a file of `blocks` blocks in the free blocks, as the BIOS
    does. Returns its chain."""
    free = free_blocks(data)[:blocks]
    if len(free) < blocks:
        raise SystemExit('card.py: the card has %d free blocks and the file '
                         'needs %d' % (len(free_blocks(data)), blocks))
    for i, block in enumerate(free):
        at = (DIR_FRAME + block - 1) * FRAME_SIZE
        data[at:at + FRAME_SIZE] = bytearray(FRAME_SIZE)
        state = MIDDLE
        if i == 0:
            state = FIRST
        elif i == blocks - 1:
            state = LAST
        data[at + DIR_STATE] = state
        if i + 1 < blocks:
            put_half(data, at + DIR_NEXT, free[i + 1] - 1)
        else:
            put_half(data, at + DIR_NEXT, NO_NEXT)
        if i == 0:
            put_word(data, at + DIR_SIZE, blocks * BLOCK_SIZE)
            encoded = name.encode('ascii')[:NAME_SIZE]
            data[at + DIR_NAME:at + DIR_NAME + len(encoded)] = encoded
        data[at + SUM_AT] = frame_sum(data[at:at + FRAME_SIZE])
    return free


def do_insert(path, name, source):
    data = read_image(path)
    with open(source, 'rb') as f:
        bytes_in = f.read()
    live, _ = files_of(data)
    chain = None
    for found, block, size, found_chain in live:
        if found == name:
            chain = found_chain
            if len(bytes_in) > len(chain) * BLOCK_SIZE:
                raise SystemExit('card.py: "%s" is %d blocks on the card and '
                                 '%s is %d bytes'
                                 % (name, len(chain), source, len(bytes_in)))
            break
    if chain is None:
        blocks = max(1, (len(bytes_in) + BLOCK_SIZE - 1) // BLOCK_SIZE)
        chain = create_file(data, name, blocks)
    done = 0
    for block in chain:
        piece = bytes_in[done:done + BLOCK_SIZE]
        at = block * BLOCK_SIZE
        data[at:at + len(piece)] = piece
        done += len(piece)
    with open(path, 'wb') as f:
        f.write(data)
    print('%s: %d bytes into "%s", %d blocks'
          % (path, len(bytes_in), name, len(chain)))
    return 0


def do_delete(path, name):
    data = read_image(path)
    _, _, chain = find_file(data, name)
    for block in chain:
        at = (DIR_FRAME + block - 1) * FRAME_SIZE
        data[at + DIR_STATE] = (FREE & KIND_MASK) | (data[at + DIR_STATE] &
                                                     PART_MASK)
        data[at + SUM_AT] = frame_sum(data[at:at + FRAME_SIZE])
    with open(path, 'wb') as f:
        f.write(data)
    print('%s: "%s" is gone; its %d blocks are free' % (path, name, len(chain)))
    return 0


def do_format(out):
    if os.path.exists(out):
        raise SystemExit('card.py: %s is there already' % out)
    with open(out, 'wb') as f:
        f.write(formatted_image())
    print('%s: a new card image of %d bytes, formatted and empty'
          % (out, IMAGE_SIZE))
    return 0


def main():
    args = sys.argv[1:]
    what = args[0] if args else ''
    rest = args[1:]
    if what == 'list' and len(rest) == 1:
        return do_list(rest[0])
    if what == 'check' and len(rest) == 1:
        return do_check(rest[0])
    if what == 'extract' and len(rest) == 3:
        return do_extract(rest[0], rest[1], rest[2])
    if what == 'insert' and len(rest) == 3:
        return do_insert(rest[0], rest[1], rest[2])
    if what == 'delete' and len(rest) == 2:
        return do_delete(rest[0], rest[1])
    if what == 'format' and len(rest) == 1:
        return do_format(rest[0])
    print(__doc__.strip())
    return 2


if __name__ == '__main__':
    sys.exit(main())
