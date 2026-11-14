"""The release's patch tool (tools/rebushido_patch.py): BPS written and applied

No C here: the tool is Python, and so is its test. Every byte it patches
is made up in this file; nothing of the game is read."""
import hashlib
import io
import zlib

import rebushido_patch as rp


def made_up(count, name):
    """`count` bytes with no pattern, the same on every run."""
    blocks = (hashlib.sha256(b'%s %d' % (name, i)).digest() for i in range(count // 32 + 1))
    return b''.join(blocks)[:count]


def number(value):
    return rp.encode_number(value)


def action(kind, length, carried=b''):
    """One action as the format has it; a copy carries how far its cursor
    moves, a TargetRead its bytes."""
    out = number((length - 1) << 2 | kind)
    if kind == rp.TARGET_READ:
        return out + carried
    if kind in (rp.SOURCE_COPY, rp.TARGET_COPY):
        return out + number(abs(carried) << 1 | (carried < 0))
    return out


def by_hand(source, target, actions, source_crc=None, target_crc=None):
    """A patch put together here, byte by byte, not by the tool."""
    body = rp.MAGIC + number(len(source)) + number(len(target)) + number(0) + b''.join(actions)
    body += (zlib.crc32(source) if source_crc is None else source_crc).to_bytes(4, 'little')
    body += (zlib.crc32(target) if target_crc is None else target_crc).to_bytes(4, 'little')
    return body + zlib.crc32(body).to_bytes(4, 'little')


def applied(patch, source):
    out = io.BytesIO()
    rp.apply_patch(patch, io.BytesIO(source), out)
    return out.getvalue()


def refused(function, *args):
    """What the tool said in refusing, or None if it did not refuse."""
    try:
        function(*args)
    except rp.PatchError as error:
        return str(error)
    return None


def made(source, target):
    patch, placed = rp.create_patch(io.BytesIO(source), io.BytesIO(target))
    carried = sum(length for kind, _, length, _ in placed if kind == rp.TARGET_READ)
    return patch, carried


def run(checks):
    # Numbers: the spellings the format's description gives, and back.
    for value, spelt in ((0, '80'), (1, '81'), (127, 'ff'), (128, '0080'), (129, '0180'),
                         (255, '7f80'), (256, '0081'), (16511, '7fff'), (16512, '000080')):
        checks.equal('%d written' % value, number(value).hex(), spelt)
    for value in (0, 1, 127, 128, 16511, 16512, 2113663, 2113664, 701714496, 1 << 40):
        spelt = number(value)
        checks.equal('%d read back' % value, rp.decode_number(spelt, 0, len(spelt)), (value, len(spelt)))
    checks.true('a number with no last byte is refused',
                refused(rp.decode_number, b'\x00\x00', 0, 2) is not None)

    # A patch written by hand, with every kind of action.
    source = b'abcdefgh'
    target = b'abc' + b'XY' + b'fgh' + b'bc' + b'bcbcbc' + b'Z' + b'ZZZZZ' + b'abc'
    hand = by_hand(source, target, [
        action(rp.SOURCE_READ, 3),            # abc, where the output is
        action(rp.TARGET_READ, 2, b'XY'),
        action(rp.SOURCE_COPY, 3, 5),         # fgh: the cursor from 0 to 5
        action(rp.SOURCE_COPY, 2, -7),        # bc: from 8 back to 1
        action(rp.TARGET_COPY, 6, 8),         # bc again and again: it runs
                                              # into what it writes
        action(rp.TARGET_READ, 1, b'Z'),
        action(rp.TARGET_COPY, 5, 2),         # the Z, five times: 14 to 16
        action(rp.TARGET_COPY, 3, -21),       # abc: from 21 back to 0
    ])
    checks.equal('the patch made by hand', applied(hand, source), target)
    patch = rp.Patch(hand)
    checks.equal('its sizes', (patch.source_size, patch.target_size), (len(source), len(target)))
    checks.equal('its actions', [(kind, length) for kind, length, _ in patch.actions()],
                 [(0, 3), (1, 2), (2, 3), (2, 2), (3, 6), (1, 1), (3, 5), (3, 3)])
    checks.equal('the right source passes', refused(rp.check_source, patch, io.BytesIO(source)), None)

    # Round trips of patches the tool makes, on made-up bytes.
    base = made_up(200000, b'base')
    changed = bytearray(base)
    for place in (5, 70000, 70001, 150003, 199999):
        changed[place] ^= 0x55
    words = bytearray(base)
    for place in range(40000, 60000, 24):     # as addresses change in code
        words[place:place + 2] = made_up(2, b'word %d' % place)
    cases = {
        'the same file': (base, base, 0),
        'five bytes changed': (base, bytes(changed), 5),
        'forty bytes put in': (base, base[:90001] + bytes(range(40)) + base[90001:], 40),
        'a part taken out': (base, base[:30000] + base[31234:], 0),
        'a part moved': (base, base[:1000] + base[120000:150000] + base[1000:120000] + base[150000:], 0),
        'two bytes changed every 24, and moved': (base, base[:777] + bytes(8) + bytes(words[777:]), 1700),
        'longer, with new bytes at the end': (base, base + made_up(3000, b'end'), 3000),
        'shorter': (base, base[:123457], 0),
        'nothing in common': (bytes(1000), made_up(1000, b'other').replace(b'\x00', b'\x01'), 1000),
        'from nothing': (b'', b'made of nothing', 15),
        'to one byte': (base, b'\x00', 1),
    }
    for name, (old, new, most) in cases.items():
        patch, carried = made(old, new)
        checks.equal(name + ': applied', applied(patch, old) == new, True)
        checks.true(name + ': carries %d bytes, at most %d wanted' % (carried, most), carried <= most)
        checks.true(name + ': smaller than the file or nearly', len(patch) <= len(new) + 64)

    # A patch that is damaged is refused before anything is written.
    old, new = cases['forty bytes put in'][:2]
    good, _ = made(old, new)
    for place in (0, 3, 5, 12, len(good) // 2, len(good) - 13, len(good) - 9, len(good) - 5, len(good) - 1):
        bad = bytearray(good)
        bad[place] ^= 0x10
        checks.true('byte %d changed: refused' % place, refused(rp.Patch, bytes(bad)) is not None)
    checks.true('cut short: refused', refused(rp.Patch, good[:-1]) is not None)
    checks.true('cut to nothing: refused', refused(rp.Patch, good[:10]) is not None)
    checks.true('not a patch: refused', refused(rp.Patch, b'PATCH' + bytes(40)) is not None)

    # One whose own checksum holds and whose content is wrong is refused
    # when it is applied.
    lying = by_hand(source, target, [action(rp.SOURCE_READ, 3), action(rp.TARGET_READ, 21, bytes(21))])
    checks.true('the result is not what the patch promised: refused', refused(applied, lying, source) is not None)
    short = by_hand(source, target, [action(rp.SOURCE_READ, 3)])
    checks.true('the actions end early: refused', refused(applied, short, source) is not None)
    long = by_hand(source, b'abc', [action(rp.SOURCE_READ, 4)])
    checks.true('the actions write too much: refused', refused(applied, long, source) is not None)
    outside = by_hand(source, bytes(8), [action(rp.SOURCE_COPY, 8, 4)])
    checks.true('a copy past the source\'s end: refused', refused(applied, outside, source) is not None)
    before = by_hand(source, bytes(8), [action(rp.SOURCE_COPY, 8, -1)])
    checks.true('a copy from before the source: refused', refused(applied, before, source) is not None)
    ahead = by_hand(source, bytes(4), [action(rp.TARGET_COPY, 4, 0)])
    checks.true('a copy of output not yet written: refused', refused(applied, ahead, source) is not None)

    # The wrong source is refused by its size, then by its checksum; and
    # a patch applied to it all the same does not pass for the result.
    patch = rp.Patch(good)
    checks.true('a source of another size: refused',
                refused(rp.check_source, patch, io.BytesIO(old + b'\x00')) is not None)
    other = bytearray(old)
    other[100] ^= 1
    checks.true('a source with one bit changed: refused',
                refused(rp.check_source, patch, io.BytesIO(bytes(other))) is not None)
    checks.true('applied to it: refused', refused(applied, good, bytes(other)) is not None)
    checks.true('the target is not the source: refused',
                refused(rp.check_source, patch, io.BytesIO(new)) is not None)
