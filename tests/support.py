"""What the Python side of a test uses (tests/README.md)."""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import extract_iso  # noqa: E402  (tools/extract_iso.py: the image's directory)

# The disc's files, extracted (tools/extract_iso.py). Not in git. BB2_DISC
# names another place for them.
DISC = os.environ.get('BB2_DISC') or os.path.join(ROOT, 'disc')
# Where a test may write a file for its program to read (the programs
# themselves are in build/tests).
WORK = os.path.join(ROOT, 'build', 'tests', 'data')


def work_file(name, contents):
    """Writes a file for the test's program and returns its path."""
    os.makedirs(WORK, exist_ok=True)
    path = os.path.join(WORK, name)
    with open(path, 'wb') as f:
        f.write(contents)
    return path


class Skip(Exception):
    """Raised by a test that cannot run here; the reason is printed."""


def disc_file(*parts):
    """The path of one of the disc's files; the test is skipped without it."""
    path = os.path.join(DISC, *parts)
    if not os.path.exists(path):
        raise Skip('no disc/' + '/'.join(parts))
    return path


# The disc's image, as BUILD.md names it. Not in git either. It is only ever
# read. BB2_DISC_IMAGE names another.
IMAGE = os.environ.get('BB2_DISC_IMAGE') or os.path.join(ROOT, 'Bushido Blade 2 (USA).bin')
SECTOR = 0x800


def disc_image():
    """The path of the disc's image; the test is skipped without it."""
    if not os.path.exists(IMAGE):
        raise Skip('no disc image (%s)' % os.path.basename(IMAGE))
    return IMAGE


def image_files(image):
    """Every file of the image's directory: 'NDATA/NDATA.DAT' -> (the sector
    it starts at, its size in bytes)."""
    files = {}

    def walk(f, extent, size, prefix):
        for record in extract_iso.read_directory(f, extent, size):
            path = prefix + record['name']
            if record['is_directory']:
                walk(f, record['extent'], record['size'], path + '/')
            else:
                files[path] = (record['extent'], record['size'])

    with open(image, 'rb') as f:
        # the volume descriptor is sector 16; the root directory's record
        # is 156 bytes into it
        root = extract_iso.parse_directory_record(extract_iso.read_sector(f, 16), 156)
        walk(f, root['extent'], root['size'], '')
    return files


_NDATA = {}


def ndata_entry(index):
    """The bytes of one entry of the NDATA archive: from disc/NDATA if
    NDATA.DAT was extracted there, else out of the disc's image. The test is
    skipped if there is neither."""
    if not _NDATA:
        dat = os.path.join(DISC, 'NDATA', 'NDATA.DAT')
        if os.path.exists(dat):
            _NDATA['read'] = _file_reader(dat)
            inf_path = os.path.join(DISC, 'NDATA', 'NDATA.INF')
            with open(inf_path, 'rb') as f:
                _NDATA['inf'] = f.read()
        else:
            image = disc_image()
            files = image_files(image)
            dat_sector = files['NDATA/NDATA.DAT'][0]
            inf_sector, inf_size = files['NDATA/NDATA.INF']

            def read(sector, sectors):
                with open(image, 'rb') as f:
                    return b''.join(extract_iso.read_sector(f, dat_sector + sector + i)
                                    for i in range(sectors))

            _NDATA['read'] = read
            with open(image, 'rb') as f:
                _NDATA['inf'] = b''.join(extract_iso.read_sector(f, inf_sector + i)
                                         for i in range(2))[:inf_size]
    start, length = struct.unpack_from('<HH', _NDATA['inf'], index * 4)
    return _NDATA['read'](start, length)


def _file_reader(path):
    def read(sector, sectors):
        with open(path, 'rb') as f:
            f.seek(sector * SECTOR)
            return f.read(sectors * SECTOR)
    return read


def source(path):
    """The text of one of the repository's files."""
    with open(os.path.join(ROOT, path), encoding='utf-8') as f:
        return f.read()


class Checks:
    """Counts a test's comparisons and keeps what failed."""

    def __init__(self):
        self.count = 0
        self.failures = []
        self.notes = []

    def note(self, text):
        """Something the reader of the results should know: a part of the
        test that could not be done here."""
        self.notes.append(text)

    def true(self, what, condition):
        self.count += 1
        if not condition:
            self.failures.append(what)

    def equal(self, what, got, want):
        self.count += 1
        if got != want:
            self.failures.append('%s is %r, not %r' % (what, got, want))

    def near(self, what, got, want, tolerance):
        self.count += 1
        if abs(got - want) > tolerance:
            self.failures.append('%s is %r, not within %r of %r' % (what, got, tolerance, want))


def numbers(line):
    """The integers of a line the program printed."""
    return [int(word, 0) for word in line.split()]
