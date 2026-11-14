"""The CD file table against the disc's own directory.

The game never reads the disc's directory: cdrom.c has every file's
position and size written into it, with the file's name in a comment. With
the disc's image (see BUILD.md) each entry is compared with the directory's
entry of the name in its comment; without it only the table's shape is
checked."""
import re

import support

# A line of the table: {AT(07, 27, 19),     3604}, /*  1 NDATA/NDATA.INF */
ENTRY_RE = re.compile(r'\{AT\((\d\d), (\d\d), (\d\d)\),\s*(\d+)\}, /\*\s*(\d+) (\S+) \*/')
FILE_COUNT = 159
# The files of the disc that the table leaves out: the program, which the
# console starts, its configuration, and a picture nothing reads.
NOT_IN_TABLE = {'SLUS_006.63', 'SYSTEM.CNF', 'TIM2D/LAS_DD.TIM'}


def check(lines, checks):
    names = [m.group(6) for m in ENTRY_RE.finditer(support.source('src/main/cdrom.c'))]
    checks.equal('entries with a name in their comment', len(names), FILE_COUNT)
    checks.equal('entries printed', len(lines), FILE_COUNT)
    checks.equal('names that come twice', len(names) - len(set(names)), 0)
    try:
        directory = support.image_files(support.disc_image())
    except support.Skip as skip:
        checks.note('%s: the table was not compared with the disc\'s directory' % skip)
        return
    for line, name in zip(lines, names):
        _, index, sector, size = line.split()
        checks.equal('file %s (%s): sector and size' % (index, name),
                     (int(sector), int(size)), directory.get(name))
    checks.equal('the disc\'s files that are not in the table',
                 set(directory) - set(names), NOT_IN_TABLE)
