"""The players' patcher (tools/patcher/patcher.c) against the patch tool: the same results, the same refusals

The patcher is C for the player's machine, not the game's, so the test
builds it with the host's compiler as a release does and runs it as a
player's shell would. The patches are made by tools/rebushido_patch.py or
by hand, and what the patcher writes is compared with what that tool's
own applier writes. Every byte is made up here; nothing of the game is
read."""
import contextlib
import hashlib
import io
import os
import shutil
import subprocess
import zlib

import rebushido_patch as rp
import support

SOURCE = os.path.join(support.ROOT, 'tools', 'patcher', 'patcher.c')
WORK = os.path.join(support.ROOT, 'build', 'tests', 'patcher')
PROGRAM = os.path.join(WORK, 'ReBushidoPatcher')
# As `make patcher` builds it.
FLAGS = ['-std=c99', '-O2', '-Wall', '-Wextra']
# How the program ends (the enum at the top of patcher.c).
OK, FAILED, USAGE, PATCH, IMAGE, EXISTS = 0, 1, 2, 3, 4, 5
SYNC = rp.SYNC


def made_up(count, name):
    """`count` bytes with no pattern, the same on every run."""
    blocks = (hashlib.sha256(b'%s %d' % (name, i)).digest() for i in range(count // 32 + 1))
    return b''.join(blocks)[:count]


def number(value):
    return rp.encode_number(value)


def action(kind, length, carried=b''):
    """One action as the format has it (as tests/rebushido/patch.py)."""
    out = number((length - 1) << 2 | kind)
    if kind == rp.TARGET_READ:
        return out + carried
    if kind in (rp.SOURCE_COPY, rp.TARGET_COPY):
        return out + number(abs(carried) << 1 | (carried < 0))
    return out


def by_hand(source, target, actions, source_size=None, target_size=None):
    """A patch put together here, byte by byte, not by the tool."""
    body = (rp.MAGIC + number(len(source) if source_size is None else source_size)
            + number(len(target) if target_size is None else target_size) + number(0) + b''.join(actions))
    body += zlib.crc32(source).to_bytes(4, 'little') + zlib.crc32(target).to_bytes(4, 'little')
    return body + zlib.crc32(body).to_bytes(4, 'little')


def python_applied(patch, source):
    out = io.BytesIO()
    rp.apply_patch(patch, io.BytesIO(source), out)
    return out.getvalue()


def write(path, data):
    with open(path, 'wb') as f:
        f.write(data)
    return path


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def folder(name):
    """A new, empty folder of the test's."""
    path = os.path.join(WORK, name)
    if os.path.isdir(path):
        shutil.rmtree(path)
    os.makedirs(path)
    return path


def patcher(*args, program=PROGRAM):
    """Runs the patcher: (how it ended, what it said)."""
    done = subprocess.run([program] + list(args), stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
    return done.returncode, (done.stdout + done.stderr).decode('utf-8', 'replace')


def nothing_left(where):
    """No result and no half-written file in a folder."""
    return not [f for f in os.listdir(where) if f.endswith(('.part', '.out', '.cue')) or f == 'ReBushido.bin']


def run(checks):
    compiler = shutil.which('cc') or shutil.which('gcc')
    if compiler is None:
        raise support.Skip('no C compiler for this host')
    os.makedirs(WORK, exist_ok=True)
    built = subprocess.run([compiler] + FLAGS + ['-o', PROGRAM, SOURCE], capture_output=True, text=True,
                           env=dict(os.environ, TMPDIR=WORK, LC_ALL='C'))
    checks.equal('the patcher builds for this host', built.returncode, 0)
    checks.equal('with no warning', built.stderr.strip(), '')
    if built.returncode:
        return

    # Round trips: the patches the tool makes, applied by the patcher.
    base = made_up(200000, b'base')
    changed = bytearray(base)
    for place in (5, 70000, 70001, 150003, 199999):
        changed[place] ^= 0x55
    cases = {
        'same': (base, base),
        'changed': (base, bytes(changed)),
        'put-in': (base, base[:90001] + bytes(range(40)) + base[90001:]),
        'taken-out': (base, base[:30000] + base[31234:]),
        'moved': (base, base[:1000] + base[120000:150000] + base[1000:120000] + base[150000:]),
        'longer': (base, base + made_up(3000, b'end')),
        'shorter': (base, base[:123457]),
        'nothing-in-common': (bytes(1000), made_up(1000, b'other')),
        'from-nothing': (b'', b'made of nothing'),
        'to-one-byte': (base, b'\x00'),
        # more than the patcher holds at a time, moved and changed
        'three-megabytes': (made_up(3 << 20, b'big'),
                            made_up(3 << 20, b'big')[1500000:] + b'new' + made_up(3 << 20, b'big')[:1500000]),
    }
    for name, (old, new) in cases.items():
        where = folder(name)
        patch, _ = rp.create_patch(io.BytesIO(old), io.BytesIO(new))
        source = write(os.path.join(where, 'source.bin'), old)
        result = os.path.join(where, 'result.out')
        code, said = patcher('--patch', write(os.path.join(where, 'p.bps'), patch), '--out', result, source)
        checks.equal(name + ': the patcher ends well (%s)' % said.strip()[-200:], code, OK)
        checks.true(name + ': its result is the tool\'s', code == OK and read(result) == python_applied(patch, old))
        checks.true(name + ': and the target', code == OK and read(result) == new)
        checks.true(name + ': no temporary file, no .cue for what is no disc',
                    sorted(os.listdir(where)) == ['p.bps', 'result.out', 'source.bin'])
        checks.equal(name + ': the source is as it was', read(source), old)

    # A patch written by hand with every kind of action, among them copies
    # of the result that run into what they write: one byte again and
    # again, and a period longer than the patcher's block.
    source = made_up(2200000, b'hand')
    target = bytearray(source[:2200000])
    target += b'XY'
    target += source[5:8]
    target += source[1:3]
    for _ in range(4500000):                      # from 50,000 on, running
        target.append(target[len(target) - 2150007])  # into itself
    target += b'Z'
    target += b'Z' * 2500000
    target += target[0:3]
    target = bytes(target)
    hand = by_hand(source, target, [
        action(rp.SOURCE_READ, 2200000),
        action(rp.TARGET_READ, 2, b'XY'),
        action(rp.SOURCE_COPY, 3, 5),              # the cursor from 0 to 5
        action(rp.SOURCE_COPY, 2, -7),             # from 8 back to 1
        action(rp.TARGET_COPY, 4500000, 50000),    # the cursor from 0
        action(rp.TARGET_READ, 1, b'Z'),
        action(rp.TARGET_COPY, 2500000, 2150007),  # the Z, to 6,700,007
        action(rp.TARGET_COPY, 3, -9200007),       # from 9,200,007 back to 0
    ])
    where = folder('by-hand')
    checks.true('the patch made by hand is what it means to be', python_applied(hand, source) == target)
    result = os.path.join(where, 'result.out')
    code, said = patcher('--patch', write(os.path.join(where, 'p.bps'), hand), '--out', result,
                         write(os.path.join(where, 'source.bin'), source))
    checks.equal('by hand: the patcher ends well (%s)' % said.strip()[-200:], code, OK)
    checks.true('by hand: its result is the tool\'s', code == OK and read(result) == target)

    # A disc: the patch beside the program, the image named by its .cue,
    # the result and its .cue beside the image, as the Python tool writes
    # them.
    old = SYNC + made_up(2352 * 40 - 12, b'disc')
    new = old[:2352 * 20] + made_up(2352, b'sector') + old[2352 * 21:]
    patch, _ = rp.create_patch(io.BytesIO(old), io.BytesIO(new))
    release = folder('release')
    program = os.path.join(release, 'ReBushidoPatcher')
    shutil.copy(PROGRAM, program)
    disc = folder('disc')
    image = write(os.path.join(disc, 'Made Up (USA).bin'), old)
    cue = write(os.path.join(disc, 'Made Up (USA).cue'),
                b'FILE "Made Up (USA).bin" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n')
    code, said = patcher(image, program=program)
    checks.equal('no patch beside the program: refused', code, PATCH)
    write(os.path.join(release, 'ReBushido-0.0-test.bps'), patch)
    code, said = patcher(cue, program=program)
    checks.equal('a disc, by its .cue, the patch found beside the program (%s)' % said.strip()[-200:], code, OK)
    theirs = folder('disc-python')
    with contextlib.redirect_stdout(io.StringIO()):
        rp.main(['apply', '--patch', os.path.join(release, 'ReBushido-0.0-test.bps'), '--image', image,
                 '--out', os.path.join(theirs, 'ReBushido.bin')])
    for name in ('ReBushido.bin', 'ReBushido.cue'):
        checks.true('its %s is the Python tool\'s' % name,
                    code == OK and read(os.path.join(disc, name)) == read(os.path.join(theirs, name)))
    checks.equal('the folder has the image, the result and nothing else', sorted(os.listdir(disc)),
                 ['Made Up (USA).bin', 'Made Up (USA).cue', 'ReBushido.bin', 'ReBushido.cue'])
    checks.true('it says where the result is and how to load it', 'ReBushido.cue' in said and 'emulator' in said)

    # The result is already there: left alone, then replaced when told.
    before = write(os.path.join(disc, 'ReBushido.bin'), b'what was there')
    code, said = patcher(image, program=program)
    checks.equal('the result is already there: refused', code, EXISTS)
    checks.equal('and left as it was', read(before), b'what was there')
    os.remove(os.path.join(disc, 'ReBushido.cue'))
    write(os.path.join(disc, 'ReBushido.bin'), new)
    write(os.path.join(disc, 'ReBushido.cue'), b'another')
    code, said = patcher(image, program=program)
    checks.equal('only its .cue is already there: refused', code, EXISTS)
    code, said = patcher('--yes', image, program=program)
    checks.equal('--yes writes over it', code, OK)
    checks.true('with the result', read(before) == new and read(os.path.join(disc, 'ReBushido.cue')) != b'another')
    code, said = patcher(os.path.join(disc, 'ReBushido.bin'), program=program)
    checks.equal('the image that is already patched: refused', code, IMAGE)
    checks.true('and said to be', 'already patched' in said)

    # More than one patch beside the program, and a .cue of several files.
    write(os.path.join(release, 'ReBushido-0.1.bps'), patch)
    code, said = patcher('--yes', image, program=program)
    checks.equal('two patches beside the program: refused', code, PATCH)
    tracks = write(os.path.join(disc, 'tracks.cue'),
                   b'FILE "Made Up (USA).bin" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n'
                   b'FILE "Track 2.bin" BINARY\r\n  TRACK 02 AUDIO\r\n    INDEX 01 00:00:00\r\n')
    code, said = patcher('--patch', os.path.join(release, 'ReBushido-0.1.bps'), '--yes', tracks, program=program)
    checks.equal('a .cue of two files: refused', code, IMAGE)

    # The arguments, and files that are not there.
    where = folder('refused')
    source = write(os.path.join(where, 'source.bin'), old)
    good = write(os.path.join(where, 'good.bps'), patch)
    checks.equal('no image named: the usage', patcher('--patch', good)[0], USAGE)
    checks.equal('two images named: the usage', patcher('--patch', good, source, source)[0], USAGE)
    checks.equal('an option it does not have: the usage', patcher('--force', source)[0], USAGE)
    checks.equal('--help', patcher('--help')[0], OK)
    checks.equal('the result named as the image: refused', patcher('--patch', good, '--out', source, source)[0], USAGE)
    checks.equal('the result named as the patch: refused', patcher('--patch', good, '--out', good, source)[0], USAGE)
    checks.equal('the result named as a .cue: refused',
                 patcher('--patch', good, '--out', os.path.join(where, 'x.cue'), source)[0], USAGE)
    checks.equal('no such image: refused',
                 patcher('--patch', good, '--out', os.path.join(where, 'r.out'), os.path.join(where, 'none.bin'))[0],
                 FAILED)
    checks.equal('no such patch: refused',
                 patcher('--patch', os.path.join(where, 'none.bps'), '--out', os.path.join(where, 'r.out'), source)[0],
                 PATCH)
    checks.equal('no such folder for the result: refused',
                 patcher('--patch', good, '--out', os.path.join(where, 'none', 'r.out'), source)[0], FAILED)
    checks.equal('the image and the patch are as they were', (read(source), read(good)), (old, patch))

    def refused(what, data, image_data, code_wanted, says=None):
        """The patcher refuses this patch for this image, with this code,
        and writes nothing."""
        bad = write(os.path.join(where, 'bad.bps'), data)
        image = write(os.path.join(where, 'image.bin'), image_data)
        code, said = patcher('--patch', bad, '--out', os.path.join(where, 'r.out'), image)
        checks.equal(what + ': refused (%s)' % said.strip()[-120:], code, code_wanted)
        checks.true(what + ': nothing is written', nothing_left(where))
        if says is not None:
            checks.true(what + ': it says "%s"' % says, says in said)

    # A damaged patch.
    for place in (0, 3, 5, 12, len(patch) // 2, len(patch) - 13, len(patch) - 9, len(patch) - 5, len(patch) - 1):
        bad = bytearray(patch)
        bad[place] ^= 0x10
        refused('byte %d of the patch changed' % place, bytes(bad), old, PATCH)
    refused('the patch cut short', patch[:-1], old, PATCH)
    refused('the patch cut to nothing', patch[:10], old, PATCH)
    refused('an empty file for a patch', b'', old, PATCH)
    refused('not a patch', b'PATCH' + bytes(40), old, PATCH)

    # The wrong image: by its size, then by its checksum.
    refused('an image of another size', patch, old + b'\x00', IMAGE, 'Expected: 94,080 bytes')
    other = bytearray(old)
    other[100] ^= 1
    refused('an image with one bit changed', patch, bytes(other), IMAGE, 'CRC32 %08X' % rp.Patch(patch).source_crc)
    refused('an .iso', patch, bytes(2048 * 37), IMAGE, '.iso')
    region = bytearray(old + bytes(2352))
    region[2352 * 3:2352 * 3 + 25] = b'BOOT = cdrom:\\SLPS_012.34'
    refused('another disc', patch, bytes(region), IMAGE, 'SLPS_012.34')

    # Patches whose own checksum holds and whose content is wrong.
    small, small_target = b'abcdefgh', b'abc' + bytes(21)
    refused('a copy past the source\'s end',
            by_hand(small, bytes(8), [action(rp.SOURCE_COPY, 8, 4)]), small, PATCH, 'outside')
    refused('a copy from before the source',
            by_hand(small, bytes(8), [action(rp.SOURCE_COPY, 8, -1)]), small, PATCH, 'outside')
    refused('a read past the source\'s end',
            by_hand(small, bytes(12), [action(rp.SOURCE_READ, 12)]), small, PATCH, 'outside')
    refused('a copy from far past four gigabytes',
            by_hand(small, bytes(8), [action(rp.SOURCE_COPY, 8, (1 << 40) + 2)]), small, PATCH, 'outside')
    refused('a copy from far before the source',
            by_hand(small, bytes(8), [action(rp.SOURCE_COPY, 8, -(1 << 40))]), small, PATCH, 'outside')
    refused('a copy of output not yet written',
            by_hand(small, bytes(4), [action(rp.TARGET_COPY, 4, 0)]), small, PATCH, 'not written')
    refused('a copy from before the output',
            by_hand(small, bytes(8), [action(rp.SOURCE_READ, 4), action(rp.TARGET_COPY, 4, -1)]), small, PATCH,
            'not written')
    refused('a copy from past the output\'s end',
            by_hand(small, bytes(8), [action(rp.SOURCE_READ, 4), action(rp.TARGET_COPY, 4, 4)]), small, PATCH,
            'not written')
    refused('a result that is not what the patch promised',
            by_hand(small, small_target + b'!', [action(rp.SOURCE_READ, 3), action(rp.TARGET_READ, 22, bytes(22))]),
            small, PATCH, 'checksum')
    refused('actions that end early', by_hand(small, small_target, [action(rp.SOURCE_READ, 3)]), small, PATCH)
    refused('actions that write too much', by_hand(small, b'abc', [action(rp.SOURCE_READ, 4)]), small, PATCH)
    refused('bytes the patch does not have',
            by_hand(small, bytes(40), [number((40 - 1) << 2 | rp.TARGET_READ) + bytes(5)]), small, PATCH)
    refused('a number with no end',
            by_hand(small, bytes(8), [action(rp.SOURCE_READ, 4), bytes(11) + b'\x80']), small, PATCH, 'number')

    # Sizes past four gigabytes are read as they are written.
    refused('a patch for an image of five gigabytes',
            by_hand(small, bytes(8), [action(rp.SOURCE_READ, 8)], source_size=5 << 30), small, IMAGE,
            'Expected: 5,368,709,120 bytes')
