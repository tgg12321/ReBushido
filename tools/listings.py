#!/usr/bin/env python3
"""Check the assembly listings against the names the build links with, or
write the listing of any function from the build.

    python3 tools/listings.py                  report what disagrees
    python3 tools/listings.py --write NAME...  disassemble those functions
                                               into build/reference/<NAME>.s

Run in WSL at the repo root after a build (it reads build/bb2.elf with nm,
and with objdump for --write).

asm/funcs holds the listings the build includes (INCLUDE_ASM): the functions
that were assembly in the original. Every other function is C, and its
listing is whatever the build makes of it, which --write shows. A listing of
asm/funcs is in the form the splitter the project began with wrote: each
instruction line carries its file offset, address and bytes,

    /* 7077C 8007FF7C E8FFBD27 */  addiu      $sp, $sp, -0x18

so the address a `jal`, a %hi/%lo pair or a %gp_rel means can be read from
the bytes and compared with the address the name has in the ELF. A name is
right when it is

  - a symbol of the ELF at that address, or `symbol + 0xN` inside one;
  - `D_<address>` or `jtbl_<address>` for an address that has no name in the
    source (a string literal, a switch table, a hardware register).

Checked as well: every label is a symbol at the address it stands at, a file
is named after one of its labels, no instruction is in two listings, and
asm/funcs holds exactly the listings the source includes.
"""
import argparse
import bisect
import glob
import os
import re
import subprocess
import sys

ELF = "build/bb2.elf"
BUILT_DIR = "asm/funcs"
WRITE_DIR = "build/reference"
GP = 0x800A30CC
LOAD_ADDRESS, HEADER_SIZE = 0x80010000, 0x800

INSTRUCTION = re.compile(
    r"^(\s*/\* \S+ ([0-9A-F]{8}) ([0-9A-F]{8}) \*/\s+)(\S+)(.*)$")
LABEL = re.compile(r"^\s*(glabel|alabel|dlabel)\s+(\S+)")
RELOC = re.compile(r"%(hi|lo|gp_rel)\(([^)]*)\)")
PLACEHOLDER = re.compile(r"^(?:D|jtbl)_([0-9A-F]{8})$")
EXPRESSION = re.compile(r"^([A-Za-z_.$][\w.$]*)(?:\s*([+-])\s*(0x[0-9A-Fa-f]+|\d+))?$")
COMPILER_MARKS = ("gcc2_compiled.", "__gnu_compiled_c")


class Symbols:
    def __init__(self, elf):
        out = subprocess.run(["mipsel-linux-gnu-nm", "-S", elf], check=True,
                             capture_output=True, text=True).stdout
        self.addresses = {}  # name -> set of addresses (a static name can repeat)
        self.at = {}         # address -> [(name, size, kind)]
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 4:
                address, size, kind, name = parts
            elif len(parts) == 3:
                address, kind, name = parts
                size = "0"
            else:
                continue
            if kind == "a" or name in COMPILER_MARKS or name.startswith((".L", "$")):
                continue
            address, size = int(address, 16), int(size, 16)
            self.addresses.setdefault(name, set()).add(address)
            self.at.setdefault(address, []).append((name, size, kind))
        self.sorted = sorted(self.at)

    def value(self, expression):
        """The addresses an operand expression can mean, or an empty set."""
        m = EXPRESSION.match(expression.strip())
        if not m:
            return set()
        offset = int(m.group(3), 0) if m.group(3) else 0
        if m.group(2) == "-":
            offset = -offset
        return {a + offset for a in self.addresses.get(m.group(1), ())}

    def name(self, address):
        """The source's name for an address: a symbol, symbol + 0xN, or None."""
        here = self.at.get(address)
        if here:
            # a sized global first: the object itself rather than an alias of it
            return sorted(here, key=lambda s: (not s[2].isupper(), s[1] == 0, s[0]))[0][0]
        i = bisect.bisect_right(self.sorted, address) - 1
        while i >= 0 and address - self.sorted[i] < 0x10000:
            inside = [s for s in self.at[self.sorted[i]] if self.sorted[i] + s[1] > address]
            inside = [s for s in inside if not PLACEHOLDER.match(s[0])]
            if inside:
                best = sorted(inside, key=lambda s: (not s[2].isupper(), s[0]))[0]
                return "%s + 0x%X" % (best[0], address - self.sorted[i])
            i -= 1
        return None

    def right_name(self, expression, address):
        """`expression` if it already names `address` properly, else the name to use."""
        expression = expression.strip()
        if address in self.value(expression):
            return expression
        name = self.name(address)
        if name:
            return name
        m = PLACEHOLDER.match(expression)
        if m and int(m.group(1), 16) == address:
            return expression
        return "D_%08X" % address


def signed16(word):
    return (word & 0x7FFF) - (word & 0x8000)


def references(lines):
    """{(kind, expression): address} for every symbol operand of a listing.

    kind is "pair" (%hi and %lo), "gp" or "jump"; address is None when the
    listing does not give one. A %lo alone is an offset from a register that
    already holds the upper half, as the scratchpad's address is kept: it is
    read as right when it is the lower half of a D_<address> name."""
    high, low, found = {}, {}, {}
    for line in lines:
        m = INSTRUCTION.match(line)
        if not m:
            continue
        word = int.from_bytes(bytes.fromhex(m.group(3)), "little")
        operands = re.sub(r"/\*.*?\*/", "", m.group(5))
        for kind, expression in RELOC.findall(operands):
            if kind == "hi":
                high.setdefault(expression, set()).add(word & 0xFFFF)
            elif kind == "lo":
                low.setdefault(expression, set()).add(signed16(word))
            else:
                found[("gp", expression)] = GP + signed16(word)
        target = operands.strip()
        if m.group(4) in ("jal", "j") and target and not target.startswith(".L"):
            address = (int(m.group(2), 16) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
            found[("jump", target)] = address
    for expression in set(high) | set(low):
        his, los = high.get(expression, set()), low.get(expression, set())
        placeholder = PLACEHOLDER.match(expression.strip())
        if len(his) == 1 and len(los) == 1:
            found[("pair", expression)] = (next(iter(his)) << 16) + next(iter(los))
        elif not his and placeholder and los == {signed16(int(placeholder.group(1), 16))}:
            found[("pair", expression)] = int(placeholder.group(1), 16)
        else:
            found[("pair", expression)] = None
    return found


def labels(lines):
    """[(name, address or None)] for the glabel / alabel / dlabel lines."""
    out = []
    pending = []
    for line in lines:
        m = LABEL.match(line)
        if m:
            pending.append(m.group(2))
            continue
        m = INSTRUCTION.match(line)
        if m and pending:
            out += [(name, int(m.group(2), 16)) for name in pending]
            pending = []
    return out + [(name, None) for name in pending]


def write_listings(names):
    """Disassemble each named function of the ELF into build/reference/."""
    os.makedirs(WRITE_DIR, exist_ok=True)
    failed = 0
    for name in names:
        out = subprocess.run(["mipsel-linux-gnu-objdump", "-d", "--disassemble=" + name, ELF],
                             capture_output=True, text=True).stdout
        start = out.find("<%s>:" % name)
        if start < 0:
            print("%s: not a function of %s" % (name, ELF))
            failed = 1
            continue
        path = "%s/%s.s" % (WRITE_DIR, name)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(out[out.rfind("\n", 0, start) + 1:].rstrip("\n") + "\n")
        print(path)
    return failed


def included_listings():
    out = subprocess.run(["grep", "-rhoE", r'INCLUDE_ASM\("asm/funcs", *\w+', "src"],
                         capture_output=True, text=True).stdout
    return set(re.findall(r", *(\w+)", out))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--write", nargs="+", metavar="NAME",
                        help="write those functions' listings into " + WRITE_DIR)
    args = parser.parse_args()
    if args.write:
        return write_listings(args.write)

    symbols = Symbols(ELF)
    problems = []
    listed = {}  # instruction address -> the listing it is in
    files = sorted(glob.glob(BUILT_DIR + "/*.s"))
    text_start = min(symbols.addresses["main_TEXT_START"])
    text_end = min(symbols.addresses["main_TEXT_END"])
    for path in files:
        with open(path, encoding="utf-8", newline="") as f:
            text = f.read()
        lines = text.split("\n")
        stem = os.path.basename(path)[:-2]

        for line in lines:
            m = INSTRUCTION.match(line)
            if not m:
                continue
            if line.split()[1] != "%X" % (int(m.group(2), 16) - LOAD_ADDRESS + HEADER_SIZE):
                problems.append("%s: file offset %s does not go with address %s"
                                % (path, line.split()[1], m.group(2)))
            if not text_start <= int(m.group(2), 16) < text_end:
                problems.append("%s: %s is not in the code" % (path, m.group(2)))
            if listed.setdefault(m.group(2), path) != path:
                problems.append("%s: %s is in %s as well"
                                % (path, m.group(2), listed[m.group(2)]))

        here = labels(lines)
        for name, address in here:
            known = symbols.addresses.get(name)
            if not known:
                problems.append("%s: label %s is not a symbol of the build" % (path, name))
            elif address is not None and address not in known:
                problems.append("%s: label %s stands at %08X, the build has it at %s"
                                % (path, name, address,
                                   ", ".join("%08X" % a for a in sorted(known))))
        if here and stem not in [name for name, _ in here]:
            problems.append("%s: named for none of its labels (%s)" % (path, here[0][0]))

        for (kind, expression), address in sorted(references(lines).items()):
            if address is None:
                problems.append("%s: cannot read the address of %s" % (path, expression))
                continue
            right = symbols.right_name(expression, address)
            if right != expression.strip():
                problems.append("%s: %s should be %s" % (path, expression, right))

    included = included_listings()
    built = {os.path.basename(p)[:-2] for p in glob.glob(BUILT_DIR + "/*.s")}
    for name in sorted(included - built):
        problems.append("%s/%s.s: included by the source but missing" % (BUILT_DIR, name))
    for name in sorted(built - included):
        problems.append("%s/%s.s: not included by the source" % (BUILT_DIR, name))

    for problem in problems:
        print(problem)
    print("%d listings, %d problems" % (len(files), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
