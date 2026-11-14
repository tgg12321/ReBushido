"""The settings' file as a memory card manager and another program would
read it: Sony's header with a title in Shift-JIS and one icon frame, then
the settings under the CRC-32 of zlib."""
import struct
import unicodedata
import zlib

VALUES = {'default': (0, 0), 'chosen': (1, 1)}


def check(lines, checks):
    checks.equal('files printed', len(lines), 2)
    for line in lines:
        label, text = line.split()
        image = bytes.fromhex(text)
        checks.equal(label + ': size', len(image), 0x200)
        checks.equal(label + ': magic', image[:2], b'SC')
        checks.equal(label + ': icon frames', image[2], 0x11)
        checks.equal(label + ': blocks', image[3], 1)
        title = image[4:0x44].split(b'\0\0')[0].decode('shift_jis')
        # full-width letters and spaces to the plain ones
        plain = unicodedata.normalize('NFKC', title)
        checks.equal(label + ': title', plain, 'ReBushido Settings')
        checks.equal(label + ': reserved bytes', image[0x44:0x60], bytes(28))
        # the icon: its first colour none, so the manager's background
        # shows; no pixel of a colour past the four it has; corners clear
        colors = struct.unpack('<16H', image[0x60:0x80])
        checks.equal(label + ': first colour', colors[0], 0)
        checks.equal(label + ': colours used', sum(1 for c in colors if c), 3)
        pixels = [n for byte in image[0x80:0x100] for n in (byte & 15, byte >> 4)]
        checks.true(label + ': the icon uses its four colours', set(pixels) == {0, 1, 2, 3})
        checks.equal(label + ': the icon\'s corners', [pixels[0], pixels[15], pixels[240], pixels[255]],
                     [0, 0, 0, 0])

        data = image[0x100:]
        checks.equal(label + ': letters', data[:4], b'RBST')
        checks.equal(label + ': version', data[4], 1)
        checks.equal(label + ': values that mean something', data[5], 2)
        checks.equal(label + ': the values', tuple(data[8:10]), VALUES[label])
        checks.equal(label + ': the other values', data[10:0x28], bytes(30))
        checks.equal(label + ': checksum', struct.unpack('<I', data[0x28:0x2C])[0], zlib.crc32(data[:0x28]))
        checks.equal(label + ': the rest', data[0x2C:], bytes(0x100 - 0x2C))
