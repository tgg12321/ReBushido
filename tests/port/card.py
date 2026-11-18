"""The card images the port makes, worked out a second time here from the
rules of the format, and tools/card.py on images made up in this file."""
import os
import subprocess
import sys
import threading

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), '..', 'tools'))

import card as card_tool  # noqa: E402  (tools/card.py)

FRAME_SIZE = 128
FRAMES_PER_BLOCK = 64
BLOCKS = 15
NAME_AT = 10
SIZE_AT = 4
NEXT_AT = 8
STATE_AT = 0
SUM_AT = FRAME_SIZE - 1

FIRST = 0x51
MIDDLE = 0x52
LAST = 0x53
FREE_KIND = 0xA0


def frame_sum(frame):
    """A frame's last byte: the exclusive or of its other 127, worked out
    here and not by the tool."""
    out = 0
    for byte in frame[:SUM_AT]:
        out ^= byte
    return out


def formatted_block():
    """The header block of a card as a format leaves it, built from the
    rules of docs/PORT.md, "The memory card": the identifier, fifteen free
    directory frames, twenty entries that name no broken frame, zeroes,
    and the identifier again."""
    block = bytearray(FRAMES_PER_BLOCK * FRAME_SIZE)
    block[0:2] = b'MC'
    block[63 * FRAME_SIZE:63 * FRAME_SIZE + 2] = b'MC'
    for i in range(BLOCKS):
        at = (1 + i) * FRAME_SIZE
        block[at + STATE_AT] = FREE_KIND
        block[at + NEXT_AT:at + NEXT_AT + 2] = b'\xff\xff'
    for i in range(20):
        at = (16 + i) * FRAME_SIZE
        block[at:at + 4] = b'\xff\xff\xff\xff'
        block[at + NEXT_AT:at + NEXT_AT + 2] = b'\xff\xff'
    for i in range(FRAMES_PER_BLOCK):
        at = i * FRAME_SIZE
        block[at + SUM_AT] = frame_sum(block[at:at + FRAME_SIZE])
    return bytes(block)


def directory_of(block):
    """(name, state, size, next block) for each of the fifteen directory
    frames of a header block."""
    out = []
    for i in range(BLOCKS):
        frame = block[(1 + i) * FRAME_SIZE:(2 + i) * FRAME_SIZE]
        name = bytes(frame[NAME_AT:NAME_AT + 20]).split(b'\0')[0].decode(
            'ascii', 'replace')
        size = int.from_bytes(frame[SIZE_AT:SIZE_AT + 4], 'little')
        link = int.from_bytes(frame[NEXT_AT:NEXT_AT + 2], 'little')
        out.append((name, frame[STATE_AT], size, link))
    return out


def run_tool(checks, *args):
    """tools/card.py, with its output; a failure of its own is a failure
    here."""
    proc = subprocess.run([sys.executable, '-I', 'tools/card.py'] + list(args),
                          capture_output=True, text=True)
    checks.equal('card.py %s exits 0' % args[0], proc.returncode, 0)
    return proc.stdout


def check_tool(checks, work):
    """tools/card.py on an image of its own: formatted, a file put in,
    listed, checked, taken out again, deleted. Nothing of the game's is
    read; the bytes are made up here."""
    # the 32-bit and the 64-bit runs check side by side: a folder each
    work = os.path.join(work, 'tool-%d' % threading.get_ident())
    os.makedirs(work, exist_ok=True)
    image = os.path.join(work, 'card.mcd')
    inside = os.path.join(work, 'inside.bin')
    out = os.path.join(work, 'out.bin')
    made_up = bytes((i * 11 + 5) & 0xFF for i in range(0x3000))
    for path in (image, inside, out):
        if os.path.exists(path):
            os.remove(path)
    with open(inside, 'wb') as f:
        f.write(made_up)

    run_tool(checks, 'format', image)
    with open(image, 'rb') as f:
        data = f.read()
    checks.equal('the tool formats 128 KB', len(data), 0x20000)
    checks.true('the tool and this file format a card the same way',
                data[:FRAMES_PER_BLOCK * FRAME_SIZE] == formatted_block())
    checks.true('and the blocks are zeroes',
                data[FRAMES_PER_BLOCK * FRAME_SIZE:] ==
                bytes(0x20000 - FRAMES_PER_BLOCK * FRAME_SIZE))

    run_tool(checks, 'insert', image, 'MADE-UP-FILE', inside)
    listed = run_tool(checks, 'list', image)
    checks.true('the tool lists the file it put in', 'MADE-UP-FILE' in listed)
    checks.true('with its two blocks', ' 2 ' in listed)
    run_tool(checks, 'check', image)
    run_tool(checks, 'extract', image, 'MADE-UP-FILE', out)
    with open(out, 'rb') as f:
        got = f.read()
    checks.true('what comes out of the card is what went in',
                got[:len(made_up)] == made_up)
    run_tool(checks, 'delete', image, 'MADE-UP-FILE')
    listed = run_tool(checks, 'list', image)
    checks.true('a deleted file is marked as one', 'freed by a delete' in listed)
    checks.true('and its blocks are free', '15 of the 15 blocks are free'
                in listed)

    # a damaged frame: the tool says which, and exits 1
    with open(image, 'r+b') as f:
        f.seek(FRAME_SIZE + NAME_AT)
        f.write(b'X')
    proc = subprocess.run([sys.executable, '-I', 'tools/card.py', 'check',
                           image], capture_output=True, text=True)
    checks.equal('a damaged frame fails the check', proc.returncode, 1)
    checks.true('and it says which frame', 'frame 1:' in proc.stdout)

    # an image that is not 128 KB is refused
    short = os.path.join(work, 'short.mcd')
    with open(short, 'wb') as f:
        f.write(bytes(0x1000))
    proc = subprocess.run([sys.executable, '-I', 'tools/card.py', 'list',
                           short], capture_output=True, text=True)
    checks.true('an image that is not 128 KB is refused',
                proc.returncode != 0)


def check(lines, checks):
    work = os.path.join('build', 'tests', 'port_card')
    os.makedirs(work, exist_ok=True)

    frames = {}
    directory = None
    for line in lines:
        words = line.split()
        if len(words) == 3 and words[0] == 'frame':
            frames[int(words[1])] = bytes.fromhex(words[2])
        elif len(words) == 2 and words[0] == 'directory':
            directory = bytes.fromhex(words[1])

    # the header block the port writes for a new card, against this file's
    # own and against the tool's
    checks.equal('the port printed every frame of a new card\'s header',
                 len(frames), FRAMES_PER_BLOCK)
    port_block = b''.join(frames[i] for i in range(FRAMES_PER_BLOCK))
    checks.true('the port formats a card as the rules say',
                port_block == formatted_block())
    checks.true('and as tools/card.py does',
                port_block == bytes(card_tool.formatted_image()
                                    [:FRAMES_PER_BLOCK * FRAME_SIZE]))
    for i in range(FRAMES_PER_BLOCK):
        checks.equal('frame %d of a new card carries its own exclusive or' % i,
                     frames[i][SUM_AT], frame_sum(frames[i]))

    # the directory the port left after the file operations: one file of
    # one block and one of three, the second in the blocks a delete freed
    checks.true('the port printed the directory it left', directory is not None)
    if directory:
        checks.equal('the directory is the identifier and fifteen frames',
                     len(directory), (BLOCKS + 1) * FRAME_SIZE)
        rows = directory_of(directory)
        files = [(name, state, size, link)
                 for name, state, size, link in rows if state == FIRST]
        checks.equal('two files are on it', len(files), 2)
        checks.equal('the first is the one of one block', files[0][0],
                     'ONE-BLOCK')
        checks.equal('... of 8192 bytes', files[0][2], 0x2000)
        checks.equal('... and it ends in its own block', files[0][3], 0xFFFF)
        checks.equal('the second was made in the freed blocks', files[1][0],
                     'AFTER-DELETE')
        checks.equal('... of three blocks', files[1][2], 3 * 0x2000)
        # its chain: block 2 to 3 to 4, counted from 0 in the field
        chain = [files[1][3]]
        while chain[-1] != 0xFFFF:
            chain.append(rows[chain[-1]][3])
        checks.equal('... linked through the blocks after it', chain,
                     [2, 3, 0xFFFF])
        checks.equal('the middle block says it is one', rows[2][1], MIDDLE)
        checks.equal('the last block says it is the last', rows[3][1], LAST)
        checks.true('a freed block keeps what part of its file it was',
                    all((state & 0xF0) == FREE_KIND
                        for name, state, size, link in rows[4:]))
        for i in range(BLOCKS + 1):
            frame = directory[i * FRAME_SIZE:(i + 1) * FRAME_SIZE]
            checks.equal('frame %d of the directory carries its own '
                         'exclusive or' % i, frame[SUM_AT], frame_sum(frame))

    check_tool(checks, work)
