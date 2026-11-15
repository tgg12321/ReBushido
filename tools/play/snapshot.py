"""Memory of two runs of a play test, compared (tests/play/README.md,
"Memory at a checkpoint").

A run writes pieces of the game's memory at its checkpoints
(tools/playtest.py, `[snapshot]`). Two programs do not have their variables
at the same addresses: the ReBushido disc is not the retail disc, and the
native port (docs/PORT.md) is a PC program. So memory is compared here by
name:

  - a piece named by a symbol is the same structure in both, wherever each
    program has it, and is compared byte for byte from its start;
  - the piece `variables` is every variable both programs have, each
    compared with the one of its name;
  - a word that differs is still the same if it is an address and both
    words point at the same place of the same name: a pointer to a
    variable or a function of the program.

Nothing here reads a file or knows a test: tools/playtest.py and
tools/port_compare.py give it the bytes and the tables of names.
"""
import bisect
import struct

# How far past a name's address a pointer may point and still be "that
# name and an offset".
REACH = 0x10000


class Names:
    """A program's names by address, for finding what an address points at.
    `layouts` is the variables the program lays out otherwise than the
    PlayStation does (a 64-bit port's, tools/playtest.py): a place inside
    one is counted from the variable's start as the PlayStation has it."""

    def __init__(self, symbols, layouts=None):
        self.symbols = symbols
        self.layouts = layouts or {}
        self.by_address = {}
        for name, address in symbols.items():
            self.by_address.setdefault(address, []).append(name)
        self.addresses = sorted(self.by_address)

    def at(self, address):
        """(names, offset) of the last names at or before `address`; ([], 0)
        if there is none within reach."""
        i = bisect.bisect_right(self.addresses, address) - 1
        if i < 0 or address - self.addresses[i] >= REACH:
            return [], 0
        return self.by_address[self.addresses[i]], address - self.addresses[i]

    def console(self, name, offset):
        """An offset into the variable `name` as the PlayStation has the
        variable: itself, but in a variable this program lays out
        otherwise, where it is the offset of the same member there. A place
        that is in no member (past the end, or between two) is given as it
        is from the end of the last member before it."""
        if name not in self.layouts:
            return offset
        size, runs = self.layouts[name]
        before = None
        for there, here, count in runs:
            if here <= offset < here + count:
                return there + offset - here
            if here + count <= offset:
                before = (there + count, here + count)
        if before is None:
            return offset
        return before[0] + offset - before[1] if offset - before[1] < 8 else size + offset

    def label(self, address):
        names, offset = self.at(address)
        if not names:
            return "0x%08X" % address
        return "%s+0x%X" % (sorted(names)[0], offset) if offset else sorted(names)[0]


def same_place(word_a, word_b, names_a, names_b):
    """Whether two words that differ are one pointer: the same offset from
    a name both programs have."""
    names, offset = names_a.at(word_a)
    for name in names:
        if name in names_b.symbols and names_b.symbols[name] + offset == word_b:
            return True
    # the same member of a variable one of the two lays out otherwise
    others, other_offset = names_b.at(word_b)
    for name in names:
        if name in others and (name in names_a.layouts or name in names_b.layouts) and \
                names_a.console(name, offset) == names_b.console(name, other_offset):
            return True
    return False


def differences(bytes_a, bytes_b, base_a, base_b, names_a, names_b):
    """The places two pieces of memory differ, as (offset, length) runs of
    bytes, after the words that are one pointer in both are taken out. The
    pieces are the same thing at `base_a` in one program and `base_b` in the
    other; words are looked for where the first program has them."""
    size = min(len(bytes_a), len(bytes_b))
    if bytes_a[:size] == bytes_b[:size]:
        return [(size, abs(len(bytes_a) - len(bytes_b)))] if len(bytes_a) != len(bytes_b) else []
    differing = bytearray(size)
    # word by word where both are whole words of the program
    first = (-base_a) & 3
    aligned = (base_a - base_b) & 3 == 0
    at = 0
    while at < size:
        whole = aligned and at >= first and (at - first) % 4 == 0 and at + 4 <= size
        if whole:
            if bytes_a[at:at + 4] != bytes_b[at:at + 4]:
                word_a, = struct.unpack_from("<I", bytes_a, at)
                word_b, = struct.unpack_from("<I", bytes_b, at)
                if not same_place(word_a, word_b, names_a, names_b):
                    for i in range(4):
                        differing[at + i] = bytes_a[at + i] != bytes_b[at + i]
            at += 4
        else:
            differing[at] = bytes_a[at] != bytes_b[at]
            at += 1
    runs = []
    at = differing.find(1)
    while at >= 0:
        # a run goes on over a gap of up to three bytes that agree
        if runs and at - (runs[-1][0] + runs[-1][1]) <= 3:
            runs[-1] = (runs[-1][0], at + 1 - runs[-1][0])
        else:
            runs.append((at, 1))
        at = differing.find(1, at + 1)
    if len(bytes_a) != len(bytes_b):
        runs.append((size, abs(len(bytes_a) - len(bytes_b))))
    return runs


def without(bytes_a, bytes_b, holes):
    """The second piece with the first's bytes in each of `holes`, (offset,
    length) from the piece's start: what a test says is not compared."""
    out = bytearray(bytes_b)
    for offset, length in holes:
        out[offset:offset + length] = bytes_a[offset:offset + length]
    return bytes(out)


def common_variables(tables, sizes, starts):
    """The variables every program has, as {name: size}: the names of
    `sizes` (one program's variables with their sizes) that are in every
    table of `tables`, at or after that program's first variable
    (`starts`, an address a table; None for no such limit)."""
    found = {}
    for name, size in sizes.items():
        if size <= 0:
            continue
        if all(name in table and (start is None or table[name] >= start) for table, start in zip(tables, starts)):
            found[name] = size
    return found


def variables_range(table, variables):
    """(address, bytes) of the memory that holds every one of `variables`
    in the program whose names are `table`."""
    low = min(table[name] for name in variables)
    high = max(table[name] + size for name, size in variables.items())
    return low, high - low


def variable_differences(bytes_a, bytes_b, base_a, base_b, names_a, names_b, variables, as_console=None,
                         sizes=None):
    """As `differences`, for the piece `variables`: each variable against
    the one of its name. Returns (name, offset, length) runs, in the order
    of the first program's addresses. `variables` has each one's size on
    the PlayStation. A program that lays a variable out otherwise (the
    64-bit port, where a pointer is 8 bytes) has its own size for it in
    its table of `sizes`, and its function of `as_console` gives the
    variable's bytes as the PlayStation would have them."""
    found = []
    for name in sorted(variables, key=lambda n: (names_a.symbols[n], n)):
        size = variables[name]
        at_a, at_b = names_a.symbols[name] - base_a, names_b.symbols[name] - base_b
        size_a, size_b = size, size
        if sizes is not None:
            size_a, size_b = max(size, sizes[0].get(name, size)), max(size, sizes[1].get(name, size))
        if at_a < 0 or at_b < 0 or at_a + size_a > len(bytes_a) or at_b + size_b > len(bytes_b):
            continue
        piece_a, piece_b = bytes_a[at_a:at_a + size_a], bytes_b[at_b:at_b + size_b]
        if as_console is not None:
            piece_a, piece_b = as_console[0](name, piece_a)[:size], as_console[1](name, piece_b)[:size]
        if piece_a == piece_b:
            continue
        for offset, length in differences(piece_a, piece_b, names_a.symbols[name], names_b.symbols[name],
                                          names_a, names_b):
            found.append((name, offset, length))
    return found


def show(piece, offset, length, most=16):
    """The bytes of a run, as hexadecimal; cut after `most`."""
    text = piece[offset:offset + min(length, most)].hex()
    return text + ("..." if length > most else "")
