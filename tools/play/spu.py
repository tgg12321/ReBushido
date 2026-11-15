"""The sound chip of two runs of a play test, compared (tests/play/README.md,
"The sound").

A run writes its sound chip at the checkpoints that say `spu`
(tools/playtest.py): sound memory, 512 KB, and then the chip's 256
registers of 16 bits. The native port writes its own (docs/PORT.md,
"Sound"). The emulator has no way for a script to its sound chip but
one: its whole saved state, which the script writes, 22 MB of it, and of
which `extract` here keeps the chip's two pieces.

What is compared, and what is not:

  - sound memory from 0x1000 on, byte for byte: the waveforms of every
    bank the game has sent, and the reverb's work area. The first 0x1000
    bytes are where the chip keeps the last samples of the disc's sound
    and of two voices, which the port's chip does not write;
  - the registers a program writes: each voice's two volumes, pitch,
    waveform and envelope settings, the main and the reverb's volumes,
    the voices' switches, the reverb's base and settings, the control
    register, the disc's volume. Not the ones the chip itself owns or
    takes away, which the emulator's state holds only as last written:
    a voice's envelope level and loop address (6 and 7 of its eight), the
    keys on and off (0xC4 to 0xC7), the voices that reached an end (0xCE,
    0xCF) and the port for data written by hand (0xD4).

Nothing here knows a test: tools/playtest.py gives it the files.
"""
import struct

RAM_BYTES = 0x80000
REG_COUNT = 0x100
BYTES = RAM_BYTES + REG_COUNT * 2
# The chip's own buffers of recent samples, at the start of sound memory.
CAPTURE_BYTES = 0x1000
VOICES = 24
VOICE_REGS = 8
# Registers that are the chip's own, or that it takes away once read.
CHIP_REGS = frozenset([v * VOICE_REGS + r for v in range(VOICES) for r in (6, 7)]
                      + [0xC4, 0xC5, 0xC6, 0xC7, 0xCE, 0xCF, 0xD4])
# Where the emulator's saved state has its sound chip (a protobuf message:
# the state's field 6), and in that the memory and the registers.
STATE_SPU = 6
STATE_SPU_RAM = 1
STATE_SPU_REGS = 2
REG_NAMES = ("left volume", "right volume", "pitch", "waveform", "envelope, low", "envelope, high",
             "envelope level", "loop")


def varint(data, at):
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, at


def message_fields(data):
    """{number: bytes} of the fields of a protobuf message that are bytes
    or messages themselves; the last of a number that comes twice."""
    found, at = {}, 0
    while at < len(data):
        key, at = varint(data, at)
        number, kind = key >> 3, key & 7
        if kind == 0:
            _, at = varint(data, at)
        elif kind == 1:
            at += 8
        elif kind == 5:
            at += 4
        elif kind == 2:
            size, at = varint(data, at)
            if at + size > len(data):
                raise ValueError("a field runs past its message")
            found[number] = data[at:at + size]
            at += size
        else:
            raise ValueError("not a protobuf message (wire type %d)" % kind)
    return found


def extract(state):
    """The sound chip out of an emulator's saved state: sound memory and
    the registers, as the port writes them."""
    try:
        chip = message_fields(message_fields(state)[STATE_SPU])
        ram, regs = chip[STATE_SPU_RAM], chip[STATE_SPU_REGS]
    except (KeyError, IndexError, ValueError) as error:
        raise ValueError("no sound chip where the emulator's state has it (%s)" % error)
    if len(ram) != RAM_BYTES or len(regs) != REG_COUNT * 2:
        raise ValueError("the state's sound memory is %d bytes and its registers %d" % (len(ram), len(regs)))
    return ram + regs


def read(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != BYTES:
        raise ValueError("%s is not %d bytes of sound memory and registers" % (path, BYTES))
    return data


def register_name(index):
    if index < VOICES * VOICE_REGS:
        return "voice %d's %s" % (index // VOICE_REGS, REG_NAMES[index % VOICE_REGS])
    return "register 0x%02X" % index


class Difference:
    """Two runs' sound chips: the stretches of sound memory that differ
    and the registers that do."""

    def __init__(self, first, second):
        self.stretches = []
        self.bytes = 0
        if first[CAPTURE_BYTES:RAM_BYTES] != second[CAPTURE_BYTES:RAM_BYTES]:
            for at in range(CAPTURE_BYTES, RAM_BYTES):
                if first[at] != second[at]:
                    self.bytes += 1
                    if self.stretches and at - self.stretches[-1][1] <= 16:
                        self.stretches[-1][1] = at
                    else:
                        self.stretches.append([at, at])
        a = struct.unpack_from("<%dH" % REG_COUNT, first, RAM_BYTES)
        b = struct.unpack_from("<%dH" % REG_COUNT, second, RAM_BYTES)
        self.registers = [(i, a[i], b[i]) for i in range(REG_COUNT) if a[i] != b[i] and i not in CHIP_REGS]
        self.count = self.bytes + len(self.registers)

    def summary(self):
        if not self.count:
            return "the same"
        parts = []
        if self.bytes:
            parts.append("%d bytes of sound memory in %d places, at %s%s"
                         % (self.bytes, len(self.stretches),
                            ", ".join("0x%05X" % s[0] for s in self.stretches[:4]),
                            " ..." if len(self.stretches) > 4 else ""))
        if self.registers:
            parts.append("%d registers: %s%s"
                         % (len(self.registers),
                            ", ".join("%s 0x%04X / 0x%04X" % (register_name(i), x, y)
                                      for i, x, y in self.registers[:3]),
                            " ..." if len(self.registers) > 3 else ""))
        return "; ".join(parts)
