"""The play tests' reader of a sound chip (tools/play/spu.py): a made-up saved state, and what it calls a difference

The emulator's saved state is a protobuf message, and the play tests cut
its sound chip out of it. Here a state is made up by hand, the format's
bytes written out, with the chip where the emulator has it and other
things around it; and two chips are compared that differ in what is
compared and in what is not. Nothing of the game or the emulator is
read."""
import os
import struct
import sys

import support

sys.path.insert(0, os.path.join(support.ROOT, 'tools', 'play'))
import spu  # noqa: E402


def varint(value):
    out = b''
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out += bytes([byte | 0x80])
        else:
            return out + bytes([byte])


def field(number, payload):
    """A field of bytes, or of a message: wire type 2."""
    return varint(number << 3 | 2) + varint(len(payload)) + payload


def number(index, value):
    """A field that is a number: wire type 0."""
    return varint(index << 3) + varint(value)


def run(checks):
    ram = bytes((i * 7 + (i >> 9)) & 0xFF for i in range(spu.RAM_BYTES))
    regs = struct.pack('<256H', *[(i * 257 + 3) & 0xFFFF for i in range(256)])
    # by hand: 300 is 0xAC 0x02; field 6 of bytes is the key 0x32
    checks.equal('a number of two bytes', varint(300), b'\xac\x02')
    checks.equal('the key of field 6, of bytes', field(6, b'ab'), b'\x32\x02ab')
    chip = (number(3, 44100) + field(1, ram) + field(2, regs) + field(14, b'\x00' * 0x8000)
            + varint(7 << 3 | 5) + b'four' + varint(9 << 3 | 1) + b'eightbyt')
    state = (field(1, field(1, b'a version')) + field(3, b'\x55' * 0x30000) + number(2, 7)
             + field(6, chip) + field(15, b'after it'))
    got = spu.extract(state)
    checks.equal('the chip out of a made-up state: its size', len(got), spu.BYTES)
    checks.true('its sound memory', got[:spu.RAM_BYTES] == ram)
    checks.true('its registers', got[spu.RAM_BYTES:] == regs)

    def refused(what, data):
        try:
            spu.extract(data)
        except ValueError:
            checks.true(what + ' is refused', True)
        else:
            checks.true(what + ' is refused', False)

    refused('a state with no chip', field(3, b'\x55' * 64) + field(15, b'x'))
    refused('a chip with half its memory', field(6, field(1, ram[:0x40000]) + field(2, regs)))
    refused('a chip with no registers', field(6, field(1, ram)))
    refused('a state cut short', state[:len(state) // 2])
    refused('what is no message at all', b'\x07\x07\x07\x07')

    # ---- what is a difference ----
    same = spu.Difference(got, got)
    checks.equal('a chip against itself', (same.count, same.summary()), (0, 'the same'))

    def changed(at, registers=()):
        data = bytearray(got)
        for i in at:
            data[i] ^= 0xFF
        for i in registers:
            data[spu.RAM_BYTES + 2 * i] ^= 0xFF
        return bytes(data)

    # the chip's own first 4 KB of sound memory are not compared
    found = spu.Difference(got, changed([0, 0x3FF, 0x800, 0xFFF]))
    checks.equal("the chip's own buffers of recent samples", found.count, 0)
    found = spu.Difference(got, changed([0x1000, 0x1001, 0x1005, 0x40000, 0x7FFFF]))
    checks.equal('bytes of sound memory', found.bytes, 5)
    checks.equal('in three places', [tuple(s) for s in found.stretches],
                 [(0x1000, 0x1005), (0x40000, 0x40000), (0x7FFFF, 0x7FFFF)])
    # the chip's own registers are not compared: a voice's envelope level
    # and loop address, the keys, the ended bits, the port for data by hand
    own = [6, 7, 8 * 23 + 6, 8 * 23 + 7, 0xC4, 0xC5, 0xC6, 0xC7, 0xCE, 0xCF, 0xD4]
    found = spu.Difference(got, changed([], own))
    checks.equal("the chip's own registers", found.count, 0)
    checks.equal('how many those are', len(spu.CHIP_REGS), 24 * 2 + 7)
    # every other one is: a voice's volume, pitch, waveform and envelope
    # settings, the main volume, the switches, the control, the reverb's
    written = [0, 1, 2, 3, 4, 5, 8 * 23 + 5, 0xC0, 0xC2, 0xC8, 0xCA, 0xCC, 0xD1, 0xD5, 0xD8, 0xE0, 0xFF]
    found = spu.Difference(got, changed([], written))
    checks.equal('the registers a program writes', [i for i, a, b in found.registers], written)
    checks.true('and it says which', "voice 0's left volume" in found.summary())
    every = spu.Difference(got, changed([], range(256)))
    checks.equal('of all 256, those compared', len(every.registers), 256 - 55)
