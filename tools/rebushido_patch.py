#!/usr/bin/env python3
"""ReBushido's patch: a BPS file that turns your own image of the retail
disc into the ReBushido disc (docs/REBUSHIDO.md, "Applying the patch").

    python3 rebushido_patch.py apply --patch ReBushido-0.1.bps \\
        --image "Bushido Blade 2 (USA).bin" --out ReBushido.bin
    python3 rebushido_patch.py info ReBushido-0.1.bps
    python3 rebushido_patch.py create --source OLD --target NEW --out X.bps

Plain Python 3, nothing to install; this file is all of it.

apply   checks the image first (its size and CRC-32 are in the patch) and
        says what is wrong with one the patch is not for. Then it writes
        the patched image and, for a disc image, its .cue. It never writes
        over the image it reads, and leaves no half-written file behind.
info    what a patch says about itself, and whether its checksum holds.
create  makes a patch. `make rebushido-patch` does this with the right
        arguments (tools/rebushido_release.py).

The format is BPS, the "beat" patch format that Floating IPS (Flips) and
most patchers apply. A patch is

    "BPS1"
    the source's size, the target's size, the metadata's size (numbers)
    the metadata (none here)
    actions, until 12 bytes before the end
    the CRC-32 of the source, of the target, and of the patch up to here

A number is written seven bits at a time, low bits first, the last byte
with its top bit set, and each byte after the first counts one more than
it says (so that no number has two spellings). An action is a number,
(length - 1) * 4 + kind:

    0 SourceRead   `length` bytes of the source, from where the output is
    1 TargetRead   `length` bytes that follow in the patch
    2 SourceCopy   a number follows, (distance * 2) + (1 if backwards):
                   the source's own cursor moves by it, and `length`
                   bytes are taken from there
    3 TargetCopy   the same with a cursor in the output written so far,
                   which may run into what the action itself writes

Only TargetRead carries bytes. The patch `create` makes holds what the
target has that the source does not have anywhere `create` looked.
"""
import argparse
import hashlib
import io
import os
import re
import sys
import zlib

MAGIC = b"BPS1"
SOURCE_READ, TARGET_READ, SOURCE_COPY, TARGET_COPY = 0, 1, 2, 3
KINDS = ("SourceRead", "TargetRead", "SourceCopy", "TargetCopy")
FOOTER = 12
CHUNK = 1 << 20

# What `apply` says about a wrong image is said for this disc.
DISC = "Bushido Blade 2, the North American disc (SLUS-00663)"
PROGRAM = "SLUS_006.63"
RAW_SECTOR, DATA_SECTOR = 2352, 2048
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


class PatchError(Exception):
    """The patch is not one, is damaged, or is not for this source."""


# ---------------------------------------------------------------------------
# Numbers
# ---------------------------------------------------------------------------
def encode_number(value):
    out = bytearray()
    while True:
        low = value & 0x7F
        value >>= 7
        if value == 0:
            out.append(0x80 | low)
            return bytes(out)
        out.append(low)
        value -= 1


def decode_number(data, at, end):
    """(the number at data[at], the place after it)."""
    value, shift = 0, 1
    while True:
        if at >= end:
            raise PatchError("the patch ends in the middle of a number")
        byte = data[at]
        at += 1
        value += (byte & 0x7F) * shift
        if byte & 0x80:
            return value, at
        shift <<= 7
        value += shift
        if shift > 1 << 70:
            raise PatchError("the patch has a number with no end")


def crc_of(stream, size=None):
    """The CRC-32 and the SHA-1 of a file from its start, and its size."""
    stream.seek(0)
    crc, sha, done = 0, hashlib.sha1(), 0
    while size is None or done < size:
        block = stream.read(CHUNK if size is None else min(CHUNK, size - done))
        if not block:
            break
        crc = zlib.crc32(block, crc)
        sha.update(block)
        done += len(block)
    return crc, sha.hexdigest(), done


# ---------------------------------------------------------------------------
# Reading a patch
# ---------------------------------------------------------------------------
class Patch:
    """A patch's header and checksums. Raises PatchError if it is not a
    patch or its own checksum does not hold."""

    def __init__(self, data):
        self.data = data
        if len(data) < len(MAGIC) + 3 + FOOTER or data[:4] != MAGIC:
            raise PatchError("this is not a BPS patch")
        end = len(data) - FOOTER
        self.source_crc, self.target_crc, self.patch_crc = (
            int.from_bytes(data[end + i:end + i + 4], "little") for i in (0, 4, 8))
        if zlib.crc32(data[:-4]) != self.patch_crc:
            raise PatchError("the patch is damaged: its own checksum does not hold "
                             "(download it again)")
        self.source_size, at = decode_number(data, 4, end)
        self.target_size, at = decode_number(data, at, end)
        metadata_size, at = decode_number(data, at, end)
        if at + metadata_size > end:
            raise PatchError("the patch's metadata runs past its end")
        self.metadata = data[at:at + metadata_size]
        self.actions_at, self.actions_end = at + metadata_size, end

    def actions(self):
        """Each action as (kind, length, the number or bytes it carries)."""
        data, at, end = self.data, self.actions_at, self.actions_end
        while at < end:
            word, at = decode_number(data, at, end)
            kind, length = word & 3, (word >> 2) + 1
            if kind == SOURCE_READ:
                yield kind, length, None
            elif kind == TARGET_READ:
                if at + length > end:
                    raise PatchError("the patch ends in the middle of its bytes")
                yield kind, length, data[at:at + length]
                at += length
            else:
                move, at = decode_number(data, at, end)
                yield kind, length, -(move >> 1) if move & 1 else move >> 1


def apply_patch(patch, source, out):
    """Writes the target to `out`, a file open for reading and writing and
    empty, from `source`, a file open for reading. The source is taken to
    have been checked (check_source). Raises PatchError when the patch
    asks for something that is not there or the result's checksum does
    not hold; what `out` then has is not the target."""
    if not isinstance(patch, Patch):
        patch = Patch(patch)
    written, crc = 0, 0
    source_at, target_at = 0, 0  # the two cursors of the copies

    def put(block):
        nonlocal written, crc
        out.write(block)
        crc = zlib.crc32(block, crc)
        written += len(block)

    def from_source(at, length):
        if at < 0 or at + length > patch.source_size:
            raise PatchError("the patch reads outside the source")
        source.seek(at)
        while length:
            block = source.read(min(CHUNK, length))
            if not block:
                raise PatchError("the source is shorter than the patch says")
            put(block)
            length -= len(block)

    for kind, length, carried in patch.actions():
        if written + length > patch.target_size:
            raise PatchError("the patch writes more than it says the result has")
        if kind == SOURCE_READ:
            from_source(written, length)
        elif kind == TARGET_READ:
            put(carried)
        elif kind == SOURCE_COPY:
            source_at += carried
            from_source(source_at, length)
            source_at += length
        else:
            target_at += carried
            if target_at < 0 or target_at >= written:
                raise PatchError("the patch copies from output it has not written")
            # What is there repeats with this period once the copy runs
            # into its own output.
            period = min(length, written - target_at)
            out.seek(target_at)
            block = out.read(period)
            out.seek(written)
            put((block * (length // period + 1))[:length])
            target_at += length
    if written != patch.target_size:
        raise PatchError("the patch ends before the result is whole")
    if crc != patch.target_crc:
        raise PatchError("the result's checksum does not hold")
    return written


def check_source(patch, source):
    """Raises PatchError unless `source` is the file the patch is for.
    Returns its SHA-1."""
    source.seek(0, 2)
    size = source.tell()
    if size != patch.source_size:
        raise PatchError("the source has %d bytes and the patch is for one of %d"
                         % (size, patch.source_size))
    crc, sha, _ = crc_of(source)
    if crc != patch.source_crc:
        raise PatchError("the source's CRC-32 is %08X and the patch is for %08X"
                         % (crc, patch.source_crc))
    return sha


# ---------------------------------------------------------------------------
# Making a patch
# ---------------------------------------------------------------------------
COMPARE = 1 << 16  # the two files are compared this much at a time
FINE = 64          # and where that differs, this much
JOIN = 2 * RAW_SECTOR  # differing places closer than this are one range
MARGIN = 1 << 16   # how far round a range the source is searched
SEED = 12          # bytes that must agree before another place is tried
STRIDE = 4         # the source's places that are remembered: every fourth
MOST = 8           # places remembered for one seed
RECENT = 4         # earlier copies whose distance is tried again
AGAIN = 2          # bytes that must agree for one of those to be taken


def common(a, ai, b, bi):
    """How many bytes a[ai:] and b[bi:] begin with in common."""
    n = min(len(a) - ai, len(b) - bi)
    if n <= 0 or a[ai] != b[bi]:
        return 0
    low, step = 1, 16
    while low < n:
        high = min(n, low + step)
        if a[ai + low:ai + high] != b[bi + low:bi + high]:
            break
        low, step = high, step * 2
    else:
        return n
    while high - low > 1:
        middle = (low + high) // 2
        if a[ai + low:ai + middle] == b[bi + low:bi + middle]:
            low = middle
        else:
            high = middle
    return low


def differing_ranges(source, target, source_size, target_size):
    """The ranges of the target that are not the source's bytes at the
    same place, as [start, end) pairs, and the two files' CRC-32s."""
    ranges = []

    def add(start, end):
        if ranges and start - ranges[-1][1] < JOIN:
            ranges[-1][1] = end
        else:
            ranges.append([start, end])

    source.seek(0)
    target.seek(0)
    source_crc = target_crc = 0
    at = 0
    while at < max(source_size, target_size):
        a = source.read(COMPARE)
        b = target.read(COMPARE)
        source_crc = zlib.crc32(a, source_crc)
        target_crc = zlib.crc32(b, target_crc)
        if a != b:
            for i in range(0, len(b), FINE):
                if a[i:i + FINE] != b[i:i + FINE]:
                    add(at + i, at + min(i + FINE, len(b)))
        at += COMPARE
    return ranges, source_crc, target_crc


class Windows:
    """Parts of the source held in memory, and where their bytes recur."""

    def __init__(self, source, source_size, wanted):
        spans = []
        for start, end in sorted((max(0, s), min(source_size, e)) for s, e in wanted):
            if start >= end:
                continue
            if spans and start <= spans[-1][1]:
                spans[-1][1] = max(spans[-1][1], end)
            else:
                spans.append([start, end])
        self.parts = []
        self.seeds = {}
        for start, end in spans:
            source.seek(start)
            data = source.read(end - start)
            self.parts.append((start, data))
            first = -start % STRIDE
            for i in range(first, len(data) - SEED + 1, STRIDE):
                places = self.seeds.setdefault(data[i:i + SEED], [])
                if len(places) < MOST:
                    places.append(start + i)

    def part(self, at):
        for start, data in self.parts:
            if start <= at < start + len(data):
                return start, data
        return None

    def match(self, at, data, i):
        """How many bytes the source at `at` has in common with data[i:]."""
        part = self.part(at)
        if part is None:
            return 0
        return common(part[1], at - part[0], data, i)

    def before(self, at, data, i, most):
        """How many bytes before the source's `at` and before data[i] are
        the same, up to `most`."""
        part = self.part(at)
        count = 0
        if part is not None:
            start, held = part
            at -= start
            while count < most and count < at and held[at - count - 1] == data[i - count - 1]:
                count += 1
        return count


def encode_range(windows, data, base, aligned_from=1):
    """The actions that make data, the target's bytes from `base`, out of
    the source: (kind, length, the source's place or the bytes).

    At each byte these places of the source are tried: the same place
    (SourceRead); the place that goes on from the last copy, as moved code
    does after an address that changed; the places as far from here as
    the few copies before that were, which is where a disc's file goes on
    after the gap between two sectors; and, when none gives much, the
    places where the next SEED bytes recur. A byte is carried only when
    none of them has it; `aligned_from` is the least a run at the first
    two must be to be taken."""
    actions = []
    literal_from = 0
    last_source = last_target = None  # the ends of the last copy
    recent = []                       # source - target of the copies before
    i, n = 0, len(data)

    def flush(upto):
        if upto > literal_from:
            actions.append((TARGET_READ, upto - literal_from, data[literal_from:upto]))

    while i < n:
        at = base + i
        place, length = at, windows.match(at, data, i)
        if last_source is not None:
            going_on = last_source + (at - last_target)
            run = windows.match(going_on, data, i)
            if run > length:
                place, length = going_on, run
        back, found = 0, False
        for distance in recent:
            run = windows.match(at + distance, data, i)
            if run >= AGAIN and run > length:
                place, length, found = at + distance, run, True
        if length < SEED and i + SEED <= n:
            for seen in windows.seeds.get(data[i:i + SEED], ()):
                run = windows.match(seen, data, i)
                if run > length:
                    place, length, found = seen, run, True
                    back = windows.before(seen, data, i, i - literal_from)
        if length < aligned_from and not found:
            length = 0
        if length == 0:
            i += 1
            continue
        i -= back
        place -= back
        length += back
        flush(i)
        if place == base + i:
            actions.append((SOURCE_READ, length, None))
        else:
            actions.append((SOURCE_COPY, length, place))
            last_source, last_target = place + length, base + i + length
            distance = last_source - last_target
            if distance in recent:
                recent.remove(distance)
            recent.insert(0, distance)
            del recent[RECENT:]
        i += length
        literal_from = i
    flush(n)
    return actions


def write_patch(actions, source_size, target_size, source_crc, target_crc):
    """The patch's bytes from its actions, which must cover the target in
    order: (kind, length, the source's place or the bytes)."""
    out = bytearray(MAGIC)
    out += encode_number(source_size) + encode_number(target_size) + encode_number(0)
    source_at = 0
    for kind, length, carried in actions:
        out += encode_number((length - 1) << 2 | kind)
        if kind == TARGET_READ:
            out += carried
        elif kind == SOURCE_COPY:
            move = carried - source_at
            out += encode_number(abs(move) << 1 | (move < 0))
            source_at = carried + length
        elif kind == TARGET_COPY:
            raise ValueError("create makes no TargetCopy")
    out += source_crc.to_bytes(4, "little") + target_crc.to_bytes(4, "little")
    out += zlib.crc32(bytes(out)).to_bytes(4, "little")
    return bytes(out)


def create_patch(source, target, search=(), aligned_from=1):
    """(the patch's bytes, its actions as (kind, the target's place,
    length, the source's place or None)) for two files open for reading.
    `search` is more (start, end) ranges of the source to look in, beyond
    the surroundings of each place where the two differ."""
    source.seek(0, 2)
    target.seek(0, 2)
    source_size, target_size = source.tell(), target.tell()
    ranges, source_crc, target_crc = differing_ranges(source, target, source_size, target_size)
    wanted = list(search) + [(start - MARGIN, end + MARGIN) for start, end in ranges]
    windows = Windows(source, source_size, wanted) if ranges else None

    actions, placed = [], []
    at = 0

    def add(kind, length, carried):
        nonlocal at
        if actions and kind == actions[-1][0] and kind in (SOURCE_READ, TARGET_READ):
            last = actions[-1]
            actions[-1] = (kind, last[1] + length, (last[2] + carried) if carried else None)
            placed[-1] = (kind, placed[-1][1], last[1] + length, None)
        else:
            actions.append((kind, length, carried))
            placed.append((kind, at, length, carried if kind == SOURCE_COPY else None))
        at += length

    for start, end in ranges + [[target_size, target_size]]:
        # The same in both up to here; past the source's end nothing is.
        end = min(end, target_size)
        if start > at:
            add(SOURCE_READ, start - at, None)
        if end > start:
            target.seek(start)
            for action in encode_range(windows, target.read(end - start), start, aligned_from):
                add(*action)
    return write_patch(actions, source_size, target_size, source_crc, target_crc), placed


# ---------------------------------------------------------------------------
# The commands
# ---------------------------------------------------------------------------
def describe(patch):
    count = [0] * 4
    total = [0] * 4
    for kind, length, _ in patch.actions():
        count[kind] += 1
        total[kind] += length
    lines = ["  for a file of %d bytes, CRC-32 %08X" % (patch.source_size, patch.source_crc),
             "  makes one of %d bytes, CRC-32 %08X" % (patch.target_size, patch.target_crc)]
    for kind in range(4):
        if count[kind]:
            lines.append("  %-10s %8d actions %12d bytes%s" % (
                KINDS[kind], count[kind], total[kind],
                "  (the bytes the patch carries)" if kind == TARGET_READ else ""))
    return lines


def program_of(image):
    """The name of the program a PlayStation disc image boots, or None."""
    try:
        with open(image, "rb") as f:
            found = re.search(rb"cdrom:\\?([A-Z]{4}_\d{3}\.\d{2})", f.read(4 << 20))
    except OSError:
        return None
    return found.group(1).decode("ascii") if found else None


def wrong_image(patch, image, size, crc):
    """Why the image is not the one the patch is for, as lines to print."""
    lines = ["%s is not the image this patch is for." % image]
    program = program_of(image)
    if crc is not None and size == patch.target_size and crc == patch.target_crc:
        lines.append("It is already patched: this is the image the patch makes.")
        return lines
    if size != patch.source_size:
        lines.append("It has %d bytes; the patch is for one of %d." % (size, patch.source_size))
    else:
        lines.append("It has the right size, but its CRC-32 is %08X and the patch is for %08X: "
                     "some of its bytes are different." % (crc, patch.source_crc))
    if program and program != PROGRAM:
        lines.append("Its program is %s: another release of the game, or another game." % program)
    elif size % RAW_SECTOR and size % DATA_SECTOR == 0:
        lines.append("It looks like an .iso, with sectors of %d bytes. The patch needs the disc "
                     "as a .bin with whole sectors of %d." % (DATA_SECTOR, RAW_SECTOR))
    elif size != patch.source_size and size % RAW_SECTOR == 0:
        lines.append("It is a .bin of whole sectors, but not the same dump: more or fewer "
                     "sectors at its end, or more than one track in the file.")
    elif size == patch.source_size:
        lines.append("It may be an image that was already patched or changed, or a dump with errors.")
    lines.append("The patch is for %s as one .bin of %d bytes, the usual dump of that disc "
                 "(the README beside the patch has its SHA-1)." % (DISC, patch.source_size))
    return lines


def same_file(a, b):
    try:
        return os.path.samefile(a, b)
    except OSError:
        return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))


def command_apply(args):
    out = args.out or os.path.join(os.path.dirname(os.path.abspath(args.image)), "ReBushido.bin")
    cue = os.path.splitext(out)[0] + ".cue"
    for other, what in ((args.image, "the image"), (args.patch, "the patch")):
        if same_file(out, other) or same_file(cue, other):
            sys.exit("error: --out is %s itself, or its .cue would be: give the result "
                     "a name of its own" % what)
    if out.lower().endswith(".cue"):
        sys.exit("error: --out names the .bin; the .cue is written beside it")
    for there in (out, cue):
        if os.path.exists(there) and not args.force:
            sys.exit("error: %s is already there (--force writes over it)" % there)
    try:
        with open(args.patch, "rb") as f:
            patch = Patch(f.read())
    except (OSError, PatchError) as error:
        sys.exit("error: %s: %s" % (args.patch, error))

    try:
        source = open(args.image, "rb")
    except OSError as error:
        sys.exit("error: %s" % error)
    with source:
        source.seek(0, 2)
        size = source.tell()
        crc = None
        if size == patch.source_size or size == patch.target_size:
            print("Checking %s ..." % args.image)
            crc, _, _ = crc_of(source)
        if size != patch.source_size or crc != patch.source_crc:
            sys.exit("error: " + "\n       ".join(wrong_image(patch, args.image, size, crc)))

        print("Writing %s ..." % out)
        part = out + ".part"
        try:
            with open(part, "w+b") as result:
                apply_patch(patch, source, result)
                _, sha, _ = crc_of(result)
                result.seek(0)
                is_disc = result.read(len(SYNC)) == SYNC
            os.replace(part, out)
        except (OSError, PatchError) as error:
            if os.path.exists(part):
                os.remove(part)
            sys.exit("error: %s" % error)
    print("  %d bytes, CRC-32 %08X, SHA-1 %s" % (patch.target_size, patch.target_crc, sha))
    if is_disc:
        with open(cue, "w", encoding="ascii", newline="\r\n") as f:
            f.write('FILE "%s" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n'
                    % os.path.basename(out))
        print("OK: load %s in an emulator (its .bin stays beside it)" % cue)
    else:
        print("OK: %s" % out)
    return 0


def command_info(args):
    try:
        with open(args.patch, "rb") as f:
            patch = Patch(f.read())
        print("%s: a BPS patch of %d bytes; its checksum holds" % (args.patch, len(patch.data)))
        print("\n".join(describe(patch)))
    except (OSError, PatchError) as error:
        sys.exit("error: %s: %s" % (args.patch, error))
    return 0


def command_create(args):
    if same_file(args.out, args.source) or same_file(args.out, args.target):
        sys.exit("error: --out is one of the two files")
    search = []
    for text in args.search:
        start, _, length = text.partition(":")
        search.append((int(start, 0), int(start, 0) + int(length, 0)))
    try:
        with open(args.source, "rb") as source, open(args.target, "rb") as target:
            data, _ = create_patch(source, target, search)
        with open(args.out, "wb") as f:
            f.write(data)
    except OSError as error:
        sys.exit("error: %s" % error)
    print("%s: %d bytes" % (args.out, len(data)))
    print("\n".join(describe(Patch(data))))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="ReBushido's patch (BPS): apply it, look at it, make one.")
    sub = ap.add_subparsers(dest="command", required=True)
    apply = sub.add_parser("apply", help="patch your image of the retail disc")
    apply.add_argument("--patch", required=True, help="the .bps file")
    apply.add_argument("--image", required=True, help="your image of the retail disc (.bin); only read")
    apply.add_argument("--out", help="the patched image to write (ReBushido.bin beside the image)")
    apply.add_argument("--force", action="store_true", help="write over an --out that is already there")
    info = sub.add_parser("info", help="what a patch says about itself")
    info.add_argument("patch")
    create = sub.add_parser("create", help="make a patch from two files")
    create.add_argument("--source", required=True)
    create.add_argument("--target", required=True)
    create.add_argument("--out", required=True)
    create.add_argument("--search", action="append", default=[], metavar="START:LENGTH",
                        help="a part of the source to look in as well")
    args = ap.parse_args(argv)
    return {"apply": command_apply, "info": command_info, "create": command_create}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
