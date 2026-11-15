#!/usr/bin/env python3
"""What a ReBushido release is made of (docs/REBUSHIDO.md, "Making a
release"): the patch, a README for players and what applies it.

    python3 tools/rebushido_release.py --retail "Bushido Blade 2 (USA).bin" [--release]

`make rebushido-patch` runs it after the matching build and the ReBushido
disc are made. It reads the retail image and build/play/ReBushido.bin and
writes into build/release/, which is not in git:

    ReBushido-<version>.bps   the patch (tools/rebushido_patch.py makes it)
    README.txt                for players
    rebushido_patch.py        a copy of the tool, for those with Python
    patch-report.txt          what the patch carries; not for the release

beside the players' patcher, which `make patcher` has built there by then
(ReBushidoPatcher.exe for Windows, ReBushidoPatcher for this host).

Then it applies the patch to the retail image, with the tool and with
this host's patcher, and compares each result with the disc, byte for
byte by SHA-1, and removes those copies. Exits 1 if one differs.

A release is no disc image and no program. The patch says "this many
bytes from here in your image" for all it can, and carries the bytes it
cannot say that for. The report counts those and says what they are; the
program's part of it is told from the two builds' maps and symbols.

--release is for the real thing: the version must be a release's (built
with RB_RELEASE=1, so with no -dev) from a tree with nothing uncommitted.
Without it the patch is a trial, named ReBushido-<version>.bps all the
same, -dev and all.
"""
import argparse
import bisect
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_disc  # noqa: E402
import rebushido_patch as rp  # noqa: E402
from elf32 import Elf  # noqa: E402

DISC = "build/play/ReBushido.bin"
EXE, ELF, MAP = ("build/rebushido/rebushido" + e for e in (".exe", ".elf", ".map"))
MATCHING_ELF, MATCHING_MAP = "build/bb2.elf", "build/bb2.map"
PROGRAM = "SLUS_006.63"
PATCHER = "ReBushidoPatcher"
SECTOR, DATA_AT, DATA_END = build_disc.SECTOR, 24, 24 + build_disc.DATA
HEADER = 0x800
VERSION = re.compile(r"^ReBushido (\S+) \((\S+)\)$")
UNIT = re.compile(r"^ (\.[\w.]+)\s+0x([0-9a-f]{8})\s+0x([0-9a-f]+) (\S+\.o)$", re.M)
SOURCE = re.compile(r"(?:^|/)((?:src|asm)/.*)$")
OWN = "src/rebushido/"

# What a carried byte can be, in the order the report lists them.
KINDS = (
    ("ecc", "the sectors' error codes (EDC and ECC), which follow from their contents"),
    ("sector", "the sectors' headers"),
    ("directory", "the disc's directory: the program's new size"),
    ("exe header", "the program's header: its size and entry"),
    ("own", "ReBushido's own code and data (src/rebushido/, and what it adds to the game's files)"),
("changed", "the game's functions and tables that ReBushido changes, as they compile now"),
    ("address", "addresses in the game's code and data, which moved: a byte that is not the original's"),
    ("original", "bytes of the original program, the same as where the retail one has them"),
    ("other", "anything else"),
)


def fail(message):
    sys.exit("rebushido_release.py: " + message)


def version_of(elf, exe):
    """(version, commit) from the text at g_rb_version."""
    at = elf.symbol("g_rb_version")
    load = elf.section(".main")["addr"]
    if at is None:
        fail("%s has no g_rb_version" % elf.path)
    at += HEADER - load
    found = VERSION.match(exe[at:exe.index(b"\0", at)].decode("ascii", "replace"))
    if not found:
        fail("the version text of %s is not `ReBushido <version> (<commit>)`" % EXE)
    return found.groups()


def units_of(path):
    """Every object's sections in the program: (address, size, object,
    section), in order, the object named from src/ or asm/."""
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    units = []
    for section, address, size, obj in UNIT.findall(text):
        address, size = int(address, 16), int(size, 16)
        named = SOURCE.search(obj)
        if size and address >= 0x80000000 and named:
            units.append((address, size, named.group(1), section))
    return sorted(units)


def functions_of(elf):
    return sorted((s["value"], s["size"], s["name"]) for s in elf.syms if s["type"] == 2 and s["size"])


def containing(table, address):
    """The entry of a sorted table of (address, size, ...) that holds it."""
    i = bisect.bisect_right(table, (address, 1 << 40)) - 1
    if i >= 0 and table[i][0] <= address < table[i][0] + table[i][1]:
        return table[i]
    return None


class Program:
    """Tells what a byte of the ReBushido program is, against the
    matching build's."""

    def __init__(self, exe, retail_exe):
        self.exe, self.retail_exe = exe, retail_exe
        elf, matching = Elf(ELF), Elf(MATCHING_ELF)
        self.load = elf.section(".main")["addr"]
        self.units = units_of(MAP)
        self.functions = functions_of(elf)
        self.matching_units = {(obj, section): (address, size)
                               for address, size, obj, section in units_of(MATCHING_MAP)}
        self.matching_functions = {name: (address, size) for address, size, name in functions_of(matching)}
        self.changed, self.changed_data = set(), set()

    def kind(self, offset):
        """The kind of the byte at `offset` of the program's file."""
        if offset < HEADER:
            return "exe header"
        address = self.load + offset - HEADER
        unit = containing(self.units, address)
        if unit is None:
            return "other"
        start, size, obj, section = unit
        if obj.startswith(OWN):
            return "own"
        there = self.matching_units.get((obj, section))
        if there is None:
            return "own"  # a section the game's file did not have
        if section == ".text":
            function = containing(self.functions, address)
            if function is None:
                return "other"
            start, size, name = function
            there = self.matching_functions.get(name)
            if there is None:
                return "own"
            if there[1] != size:
                self.changed.add(name)
                return "changed"
        elif there[1] != size:
            self.changed_data.add(obj + section)
            return "changed"
        retail = there[0] + (address - start) - self.load + HEADER
        return "original" if self.retail_exe[retail] == self.exe[offset] else "address"


def count_carried(placed, program, exe_lba, exe_sectors, directory):
    """How many of the bytes the patch carries are of each kind."""
    counts = dict.fromkeys((name for name, _ in KINDS), 0)
    for kind, at, length, _ in placed:
        if kind != rp.TARGET_READ:
            continue
        for place in range(at, at + length):
            lba, within = divmod(place, SECTOR)
            if within < DATA_AT:
                counts["sector"] += 1
            elif within >= DATA_END:
                counts["ecc"] += 1
            elif exe_lba <= lba < exe_lba + exe_sectors:
                counts[program.kind((lba - exe_lba) * build_disc.DATA + within - DATA_AT)] += 1
            elif lba in directory:
                counts["directory"] += 1
            else:
                counts["other"] += 1
    return counts


def source_url():
    """Where the source is: the repository's own address."""
    got = subprocess.run(["git", "remote", "get-url", "origin"], capture_output=True, text=True)
    url = got.stdout.strip()
    url = re.sub(r"^git@([^:]+):", r"https://\1/", url)
    return re.sub(r"\.git$", "", url) or "the repository this was built from"


def patcher_applied(patcher, patch_path, retail, out_dir):
    """The SHA-1 of what the players' patcher makes of the retail image,
    or None if it does not end well. The copy is removed."""
    trip = os.path.join(out_dir, "roundtrip.bin")
    made = (trip, os.path.splitext(trip)[0] + ".cue", trip + ".part")
    try:
        ran = subprocess.run([patcher, "--patch", patch_path, "--out", trip, "--yes", retail],
                             stdin=subprocess.DEVNULL, capture_output=True, text=True, errors="replace")
        if ran.returncode != 0 or not os.path.exists(trip):
            print((ran.stdout + ran.stderr).strip())
            return None
        with open(trip, "rb") as out:
            return rp.crc_of(out)[1]
    finally:
        for path in made:
            if os.path.exists(path):
                os.remove(path)


README = """\
{title}
{rule}

ReBushido is a mod of Bushido Blade 2 (PlayStation, 1998), built from a
decompilation of the game. This is a patch for your own image of the
disc. It is not the game, and is of no use without that image.

What you need
-------------
Bushido Blade 2, the North American disc (SLUS-00663), as one .bin file
with its .cue, the way most tools dump it:

    size     {source_size:,} bytes
    CRC-32   {source_crc:08X}
    SHA-1    {source_sha}

Another region's disc, another dump or an .iso will not do: the patch
is checked against these and is refused.

Applying the patch
------------------
On Windows, run ReBushidoPatcher.exe, which comes with this. Keep it in
one folder with {patch} and double-click it, or drop
your image on it. It asks for your image, the .bin or its .cue, checks
it, and writes ReBushido.bin and ReBushido.cue beside it; if the image
is not the right one it says what is wrong. Your image is only read.
Windows may warn about a program it does not know ("More info", then
"Run anyway"): the program is built from the source named below.

Or, on any system, with Python 3 (nothing to install) and the file
rebushido_patch.py, or with any patcher that takes BPS files, such as
Floating IPS (then copy your .cue as ReBushido.cue and change the name
in its first line to ReBushido.bin):

    python3 rebushido_patch.py apply --patch {patch} --image "Bushido Blade 2 (USA).bin" --out ReBushido.bin

The result is:

    size     {target_size:,} bytes
    CRC-32   {target_crc:08X}
    SHA-1    {target_sha}

Playing
-------
Load ReBushido.cue in an emulator as you would any disc. A page of
ReBushido's own comes up first, with two settings:

    Bugfixes   Off / On               fifteen fixes of the original's bugs
    Graphics   Original / Remastered  does nothing yet

Up and down choose, left and right change, Start or cross goes on to
the game. With both as they start, the game plays as the original.

Saves
-----
The disc has the retail serial, so it uses the retail game's memory card,
and saves go both ways between the two. ReBushido never touches the
game's save. Its settings are a file of its own on the card in slot 1,
"ReBushido Settings", one block.

What it was tried on
--------------------
The emulator PCSX-Redux with its own open BIOS, and nothing else: no
console, no other emulator, no Sony BIOS.

Source and licence
------------------
{url}

ReBushido is free software under the GNU Affero General Public License,
version 3 or later, and comes with no warranty. Bushido Blade 2 belongs
to its rights holders (Square / Lightweight, 1998); this project is not
affiliated with them or with Sony. The patch holds what ReBushido's
program has that the retail one does not: its own code, the functions
it changes, the addresses that moved, and the error codes of the sectors
it changes. ReBushidoPatcher.exe is built from tools/patcher/patcher.c
of that source, under the same licence.
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--retail", required=True, help="the retail image; only read")
    ap.add_argument("--out-dir", default="build/release")
    ap.add_argument("--release", action="store_true", help="refuse a version that is not a release's")
    args = ap.parse_args()
    for path in (args.retail, DISC, EXE, ELF, MAP, MATCHING_ELF, MATCHING_MAP):
        if not os.path.exists(path):
            fail("no %s" % path)

    with open(EXE, "rb") as f:
        exe = f.read()
    version, commit = version_of(Elf(ELF), exe)
    patcher = os.path.join(args.out_dir, PATCHER)
    if args.release:
        problems = []
        if not os.path.exists(patcher + ".exe"):
            problems.append("no %s.exe, the players' patcher (`make patcher` builds it where mingw-w64 "
                            "is installed)" % patcher)
        if version.endswith("-dev"):
            problems.append("the version is %s: build with RB_RELEASE=1" % version)
        if commit.endswith("-dirty") or commit == "unknown":
            problems.append("the build is of %s, not of a commit" % commit)
        status = subprocess.run(["git", "status", "--porcelain"], capture_output=True, text=True)
        if status.returncode or status.stdout.strip():
            problems.append("the tree has changes or files that are not committed")
        if problems:
            fail("no release from this: " + "; ".join(problems))

    # The disc's program is the build's, and where the two images keep it.
    retail, disc = build_disc.Image(args.retail), build_disc.Image(DISC)
    _, retail_files = build_disc.walk(retail)
    _, disc_files = build_disc.walk(disc)
    old, new = retail_files[PROGRAM], disc_files[PROGRAM]
    old_sectors, new_sectors = build_disc.nsectors(old["size"]), build_disc.nsectors(new["size"])
    retail_exe = retail.data(old["lba"], old_sectors)[:old["size"]]
    if disc.data(new["lba"], new_sectors)[:new["size"]] != exe:
        fail("%s does not hold %s: make the disc again" % (DISC, EXE))
    retail.f.close()
    disc.f.close()

    os.makedirs(args.out_dir, exist_ok=True)
    name = "ReBushido-%s.bps" % version
    patch_path = os.path.join(args.out_dir, name)
    with open(args.retail, "rb") as source, open(DISC, "rb") as target:
        search = [(old["lba"] * SECTOR, (old["lba"] + old_sectors) * SECTOR)]
        data, placed = rp.create_patch(source, target, search)
        source_sha = rp.crc_of(source)[1]
        target_sha = rp.crc_of(target)[1]
    with open(patch_path, "wb") as f:
        f.write(data)
    patch = rp.Patch(data)

    # What it carries.
    program = Program(exe, retail_exe)
    counts = count_carried(placed, program, new["lba"], new_sectors, {16, old["rec"][0]})
    carried = sum(counts.values())
    report = ["%s: %d bytes, for ReBushido %s (%s)" % (patch_path, len(data), version, commit)]
    report += rp.describe(patch)
    report.append("  the %d bytes it carries:" % carried)
    for kind, what in KINDS:
        if counts[kind]:
            report.append("  %8d  %s" % (counts[kind], what))
    report.append("  the program is %d bytes (the retail one %d); %d of the game's functions and %d of "
                  "its files' data are changed" % (len(exe), len(retail_exe), len(program.changed),
                                                   len(program.changed_data)))
    print("\n".join(report))
    with open(os.path.join(args.out_dir, "patch-report.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(report) + "\n")

    # The round trip: the patch on the retail image is the disc.
    trip = os.path.join(args.out_dir, "roundtrip.bin")
    try:
        with open(args.retail, "rb") as source, open(trip, "w+b") as out:
            rp.check_source(patch, source)
            rp.apply_patch(patch, source, out)
            trip_sha = rp.crc_of(out)[1]
    finally:
        if os.path.exists(trip):
            os.remove(trip)
    print("  the retail image   SHA-1 %s" % source_sha)
    print("  the patch applied  SHA-1 %s" % trip_sha)
    print("  %s  SHA-1 %s" % (DISC, target_sha))
    if trip_sha != target_sha:
        os.remove(patch_path)
        fail("the patch applied to the retail image is not %s" % DISC)

    # The same with the players' patcher, as built for this host.
    if os.path.exists(patcher):
        trip_sha = patcher_applied(patcher, patch_path, args.retail, args.out_dir)
        print("  by %s    SHA-1 %s" % (PATCHER, trip_sha))
        if trip_sha != target_sha:
            os.remove(patch_path)
            fail("the patch applied by %s to the retail image is not %s" % (patcher, DISC))
    else:
        print("  no %s: the patcher was not tried (`make patcher` builds it)" % patcher)

    title = "ReBushido %s (%s)" % (version, commit)
    with open(os.path.join(args.out_dir, "README.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(README.format(title=title, rule="=" * len(title), patch=name, url=source_url(),
                              source_size=patch.source_size, source_crc=patch.source_crc, source_sha=source_sha,
                              target_size=patch.target_size, target_crc=patch.target_crc, target_sha=target_sha))
    shutil.copyfile(os.path.join(os.path.dirname(os.path.abspath(__file__)), "rebushido_patch.py"),
                    os.path.join(args.out_dir, "rebushido_patch.py"))
    print("OK: %s%s" % (patch_path, "" if args.release else " (a trial: no release without RB_RELEASE=1)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
