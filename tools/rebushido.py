#!/usr/bin/env python3
"""The link of the ReBushido build, and whether it fits (docs/REBUSHIDO.md).

    python3 tools/rebushido.py ld --out build/rebushido/rebushido.ld OBJECT...
    python3 tools/rebushido.py fit build/rebushido/rebushido.elf build/rebushido/rebushido.exe

`make rebushido` runs both. Neither touches the matching build.

ld   writes the ReBushido build's linker script from bb2.ld, which stays the
     one place that says in what order the game is linked and is not
     changed. The objects are the ReBushido build's own (bb2.ld's `build/`
     becomes the folder --out is in). Then everything those objects hold
     that bb2.ld does not place is added at the end of its kind:

       .rodata .text .data .sdata .sbss   after the last file's, in .main
       .bss (a file's `static` variables) after the last file's
       a variable no file initialises     after the last small one if it is
       (its own section, maspsx           of eight bytes or fewer, where gp
       --comm-own-sections)               reaches; else after the last large

     That is every section of a new file (src/rebushido/), a section an old
     file did not have before, and a new variable wherever it is defined.
     The linker is given no object but those the script names, so each new
     file is named at its top (INPUT): one that only defines variables
     would otherwise not be read at all. Nothing is left for the script's
     /DISCARD/ to drop silently: a section of a kind not known here stops
     the build, and `fit` checks that every object was read. Each object's section in
     .main is also put on a 4-byte boundary. In the original every one
     already is, so this moves nothing until a file's data changes length,
     and then keeps the file after it from starting off a word. The end of
     each kind in .main is put on one too, for the same reason: the zeroed
     variables begin where the small data ends, and the start-up code
     clears them a word at a time.

     The places in bb2.ld that the additions go to are found by the lines
     in ANCHORS; if bb2.ld loses one the build stops and says which.

fit  reads the linked ELF and the EXE: the program's size, how far its end
     is below the first of the fixed buffers above it (include/sys.h), the
     movie overlay and the stack, how much of gp's reach the small data
     uses, and the version text in the EXE. Exits 1, saying why, if the
     program runs into the first buffer, the text is not there, or the
     link's map (beside the ELF) does not have every object of the script.
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf32 import Elf  # noqa: E402

BASE_SCRIPT = "bb2.ld"
MEMORY_MAP = "include/sys.h"
MATCHING_ELF = "build/bb2.elf"
SHF_ALLOC = 2
SMALL = 8  # bytes: what the original linker kept where gp reaches
GP_REACH = 0x7FFF
VERSION_PREFIX = b"ReBushido "

# The sections of an object that take no room in the program.
NOT_PLACED = {".reginfo", ".MIPS.abiflags", ".pdr", ".note", ".comment", ".gnu.attributes"}
# kind -> the line of bb2.ld its additions go ahead of.
ANCHORS = {
    ".rodata": "main_RODATA_END = .;",
    ".text": "main_TEXT_END = .;",
    ".data": "_gp = .;",
    ".sdata": "main_SBSS_START = .;",
    ".sbss": "main_DATA_END = .;",
    "large": "main_BSS_END = .;",
}
# kind -> the output section whose closing brace its additions go ahead of.
BLOCKS = {
    ".bss": ".main_bss_files (NOLOAD)",
    "small": ".main_bss ADDR(.main)",
}
MAIN_START, MAIN_END = ".main 0x80010000", "main_DATA_END = .;"
OBJECT_LINE = re.compile(r"^(\s*)build/(\S+\.o)\((\.\w+)\);\s*$")
VARIABLE_LINE = re.compile(r"^\s*\*\(\.gnu\.linkonce\.b\.(\w+)\)\s*$")
VARIABLE_SECTION = ".gnu.linkonce.b."


def fail(message):
    sys.exit("rebushido.py: " + message)


def only_line(lines, text):
    """The index of the one line of the script that has `text`."""
    hits = [i for i, line in enumerate(lines) if text in line]
    if len(hits) != 1:
        fail("%s has %d lines with `%s`, not one: the script changed its shape, and "
             "ANCHORS and BLOCKS in tools/rebushido.py have to follow it" % (BASE_SCRIPT, len(hits), text))
    return hits[0]


def write_script(out, objects):
    build_dir = os.path.dirname(out).replace("\\", "/")
    with open(BASE_SCRIPT, encoding="utf-8") as f:
        lines = f.read().split("\n")

    # what bb2.ld places, with the objects' paths made this build's
    placed, variables = set(), set()
    main_from, main_to = only_line(lines, MAIN_START), only_line(lines, MAIN_END)
    script = []
    for i, line in enumerate(lines):
        m = OBJECT_LINE.match(line)
        if m:
            indent, obj, section = m.group(1), "%s/%s" % (build_dir, m.group(2)), m.group(3)
            placed.add((obj, section))
            if main_from < i < main_to and not script[-1].strip().startswith(". = ALIGN("):
                script.append(indent + ". = ALIGN(4);")
            line = "%s%s(%s);" % (indent, obj, section)
        m = VARIABLE_LINE.match(line)
        if m:
            variables.add(m.group(1))
        script.append(line)
    lines = script

    # what the objects hold beyond that
    added = {kind: [] for kind in list(ANCHORS) + list(BLOCKS)}
    known = set(o for o, _ in placed)
    new_objects = []
    for obj in objects:
        obj = obj.replace("\\", "/")
        if obj not in known:
            new_objects.append(obj)
        for sec in Elf(obj).secs:
            name, size = sec["name"], sec["size"]
            if not sec["flags"] & SHF_ALLOC or size == 0 or name in NOT_PLACED:
                continue
            if name.startswith(VARIABLE_SECTION):
                variable = name[len(VARIABLE_SECTION):]
                if variable not in variables:
                    variables.add(variable)
                    added["small" if size <= SMALL else "large"].append("*(%s)" % name)
            elif name in added:
                if (obj, name) not in placed:
                    added[name].append("%s(%s);" % (obj, name))
            else:
                fail("%s has a section %s (%d bytes) and nothing here says where one goes" % (obj, name, size))
    missing = sorted(known - set(o.replace("\\", "/") for o in objects))
    if missing:
        fail("%s places %d objects that were not built, the first %s" % (BASE_SCRIPT, len(missing), missing[0]))

    # each kind's additions, ahead of its place
    places = []
    for kind, text in ANCHORS.items():
        places.append((only_line(lines, text), kind))
    for kind, text in BLOCKS.items():
        start = only_line(lines, text)
        places.append((next(i for i in range(start, len(lines)) if lines[i].strip() == "}"), kind))
    indent = " " * 8
    for at, kind in sorted(places, reverse=True):
        in_main = kind in ANCHORS and kind != "large"
        block = []
        if added[kind]:
            block.append(indent + "/* ReBushido: what %s does not place. */" % BASE_SCRIPT)
        for entry in added[kind]:
            if in_main:
                block.append(indent + ". = ALIGN(4);")
            block.append(indent + entry)
        if added[kind] and kind == "small":
            block.append(indent + ". = ALIGN(8);")
        if in_main:
            # The kind ends on a word as well, whatever its last section's
            # length: the next kind begins there, and the start-up code
            # clears the zeroed variables from main_SBSS_START a word at a
            # time, which the console refuses at an odd address.
            block.append(indent + ". = ALIGN(4);")
        lines[at:at] = block

    head = ["/* Written by tools/rebushido.py from %s for the ReBushido build: do not" % BASE_SCRIPT,
            " * edit. %d sections and variables added, %d of the objects new. */" % (
                sum(len(v) for v in added.values()), len(new_objects))]
    head += ["INPUT(%s)" % obj for obj in new_objects]
    os.makedirs(build_dir, exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(head + lines))
    print("rebushido.py: %s: %d objects, %d of them new; added %s" % (
        out, len(objects), len(new_objects),
        ", ".join("%d %s" % (len(v), k) for k, v in added.items() if v) or "nothing"))
    return 0


def address_of(name):
    """An address include/sys.h gives a name to."""
    with open(MEMORY_MAP, encoding="utf-8") as f:
        m = re.search(r"^#define %s (0x[0-9A-Fa-f]+)\b" % name, f.read(), re.M)
    if not m:
        fail("%s no longer defines %s" % (MEMORY_MAP, name))
    return int(m.group(1), 16)


def room(size):
    return "0x%X" % size if size >= 0 else "none (0x%X over)" % -size


def fit(elf_path, exe_path):
    elf = Elf(elf_path)
    with open(exe_path, "rb") as f:
        exe = f.read()

    def symbol(name):
        value = elf.symbol(name)
        if value is None:
            fail("%s has no symbol %s" % (elf_path, name))
        return value

    load = elf.section(".main")["addr"]
    loaded_end = load + len(exe) - 0x800
    end = symbol("main_BSS_END")
    gp = symbol("_gp")
    small_end = symbol("main_BSS_START")
    first_buffer = address_of("SYS_SOUND_VAB_ADDR")
    overlay = address_of("SYS_PRIM_BUFFER_0")
    stack, = struct.unpack_from("<I", exe, 0x30)

    # the text at g_rb_version, which is not the only one that begins so
    at = (elf.symbol("g_rb_version") or load - 1) - load + 0x800
    version = None
    if 0x800 <= at < len(exe) and exe.startswith(VERSION_PREFIX, at):
        version = exe[at:exe.index(b"\0", at)].decode("ascii", "replace")

    print("%s: %s" % (exe_path, version or "no version text"))
    print("  file     %d bytes; loaded at 0x%08X to 0x%08X" % (len(exe), load, loaded_end))
    print("  program  ends at 0x%08X, its zeroed variables included" % end)
    if os.path.exists(MATCHING_ELF):
        matching = Elf(MATCHING_ELF).symbol("main_BSS_END")
        if matching is not None:
            print("           0x%X bytes more than the matching build (%s)" % (end - matching, MATCHING_ELF))
    print("  room     %s bytes below the first fixed buffer (0x%08X, SYS_SOUND_VAB_ADDR)" % (
        room(first_buffer - end), first_buffer))
    print("           %s below the movie overlay and the primitive buffers (0x%08X)" % (
        room(overlay - end), overlay))
    print("           %s below the stack's top (0x%08X)" % (room(stack - end), stack))
    print("  gp       the small data ends 0x%X from gp, which reaches 0x%X" % (small_end - gp, GP_REACH))

    problems = []
    script, link_map = elf_path[:-4] + ".ld", elf_path[:-4] + ".map"
    with open(script, encoding="utf-8") as f:
        named = set(re.findall(r"[\w/.-]+\.o\b", f.read()))
    with open(link_map, encoding="utf-8", errors="replace") as f:
        loaded = set(re.findall(r"^LOAD (\S+\.o)$", f.read(), re.M))
    for obj in sorted(named - loaded):
        problems.append("the link did not read %s, which %s names" % (obj, script))
    if end > first_buffer:
        problems.append("the program ends at 0x%08X, 0x%X bytes past SYS_SOUND_VAB_ADDR (0x%08X), the first "
                        "of the fixed buffers of %s: make it smaller, or move the buffers"
                        % (end, end - first_buffer, first_buffer, MEMORY_MAP))
    if small_end - gp > GP_REACH:
        problems.append("the small data ends 0x%X from gp, past its reach of 0x%X" % (small_end - gp, GP_REACH))
    if version is None:
        problems.append("the EXE has no text beginning `%s` (src/rebushido/version.c)"
                        % VERSION_PREFIX.decode())
    for problem in problems:
        print("FAIL: " + problem)
    if problems:
        return 1
    print("OK: rebushido fits")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    ld = sub.add_parser("ld", help="write the linker script")
    ld.add_argument("--out", required=True)
    ld.add_argument("objects", nargs="+")
    ft = sub.add_parser("fit", help="report the size and fail if it does not fit")
    ft.add_argument("elf")
    ft.add_argument("exe")
    args = ap.parse_args()
    if args.command == "ld":
        return write_script(args.out, args.objects)
    return fit(args.elf, args.exe)


if __name__ == "__main__":
    sys.exit(main())
