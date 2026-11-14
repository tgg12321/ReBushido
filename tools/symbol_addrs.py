#!/usr/bin/env python3
"""Write config/symbol_addrs.txt, the table of every name in the game and its address.

    python3 tools/symbol_addrs.py           write the table
    python3 tools/symbol_addrs.py --check   say whether the table is current

Run in WSL at the repo root after a build. The table is read off the linked
ELF (build/bb2.elf), so it has the names the source has: one line per symbol,
`name = 0xADDRESS;`, by address, with `// type:func` after a function's. A
name that is static in more than one file has a line for each. Nothing in the
build reads the table; it is there to be read or loaded into a debugger, and
tools/count.py counts the functions from it.
"""
import re
import subprocess
import sys

ELF = "build/bb2.elf"
TABLE = "config/symbol_addrs.txt"
HEADER = """\
// Bushido Blade 2 (SLUS-00663): every symbol of the executable, by address.
// Written by tools/symbol_addrs.py from the linked ELF; do not edit by hand.
"""
COMPILER_MARKS = ("gcc2_compiled.", "__gnu_compiled_c")
LINKER_SYMBOL = re.compile(r"^((main|header)_[A-Za-z_]+|__romPos)$")


def table():
    out = subprocess.run(["mipsel-linux-gnu-nm", ELF], check=True,
                         capture_output=True, text=True).stdout
    symbols = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        address, kind, name = parts
        if (kind == "a" or name in COMPILER_MARKS or name.startswith((".L", "$"))
                or LINKER_SYMBOL.match(name)):
            continue
        symbols.add((int(address, 16), name))
    functions = function_symbols()
    return HEADER + "".join(
        "%s = 0x%08X;%s\n" % (name, address,
                              " // type:func" if (address, name) in functions else "")
        for address, name in sorted(symbols))


def function_symbols():
    """{(address, name)} of the symbols the ELF types as functions."""
    out = subprocess.run(["mipsel-linux-gnu-readelf", "-sW", ELF], check=True,
                         capture_output=True, text=True).stdout
    found = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 8 and parts[3] == "FUNC":
            found.add((int(parts[1], 16), parts[7]))
    return found


def main():
    text = table()
    if "--check" in sys.argv[1:]:
        with open(TABLE, encoding="utf-8", newline="") as f:
            current = f.read() == text
        print("%s is %s" % (TABLE, "current" if current else
                            "out of date: run tools/symbol_addrs.py"))
        return 0 if current else 1
    with open(TABLE, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("%s: %d symbols" % (TABLE, text.count("\n") - HEADER.count("\n")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
