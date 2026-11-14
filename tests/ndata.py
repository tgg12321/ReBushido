"""The NDATA archive's tables against the file map and the disc.

Without the disc: the names and the directory built into the program
against docs/formats/ndata_filemap.csv (which was made from the original
executable and the disc's NDATA.INF), and where each kind of entry starts
against the names. With disc/NDATA/NDATA.INF: the directory against it.
With NDATA.DAT (in disc/NDATA, or inside the disc's image): the models the
game takes out of a chanbara pack against the same models' own entries."""
import csv
import os
import struct

import support

PACKS = 37           # the two chanbara packs' entries
PACKED_MODELS = 30   # the seven models a pack can hold
PACKED_MODEL_COUNT = 7

# The first file of each kind of entry (enum NdataId, include/ndata.h).
KIND_FIRST_FILES = {
    'NDATA_MODELS': 'DATA0\\M000.DAT',
    'NDATA_PACKED_MODELS': 'DATA0\\M301.DAT',
    'NDATA_CHANBARA_PACKS': 'DATA0\\CHANBARA0.DAT',
    'NDATA_STAGES': 'DATA2\\S00.DAT',
    'NDATA_TINY_MODELS': 'DATA5\\T00.DAT',
    'NDATA_TINY_MODELS_GRAY': 'DATA6\\TINYM00.DAT',
    'NDATA_WEAPONS': 'DATA8\\WEP0000.DAT',
    'NDATA_WEAPONS_SET_1': 'DATA9\\WEP0000.DAT',
    'NDATA_MOTIONS': 'DATA7\\MAR00_00.DAT',
}


def disc_inf():
    """The disc's NDATA.INF, or None without the disc's files."""
    path = os.path.join(support.DISC, 'NDATA', 'NDATA.INF')
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        return f.read()


def arguments():
    """With the disc: the two packs and the seven models as files."""
    args = []
    try:
        for entry in [PACKS, PACKS + 1] + list(range(PACKED_MODELS, PACKED_MODELS + PACKED_MODEL_COUNT)):
            args.append(support.work_file('ndata%03d.bin' % entry, support.ndata_entry(entry)))
    except support.Skip:
        return []   # the rest of the test needs no disc
    return args


def check(lines, checks):
    with open(os.path.join(support.ROOT, 'docs', 'formats', 'ndata_filemap.csv'), newline='') as f:
        file_map = {int(row['file_id']): row for row in csv.DictReader(f)}
    inf = disc_inf()

    names = {}
    for line in lines:
        words = line.split()
        if words[0] == 'entry':
            index, start, length, name = int(words[1]), int(words[2]), int(words[3]), words[4]
            names[index] = name
            row = file_map[index]
            # the last two slots of NDATA.INF are leftovers that the
            # program's copy has as zero
            if index < 899:
                checks.equal('entry %d: start' % index, start, int(row['start_sector']))
                checks.equal('entry %d: length' % index, length, int(row['len_sectors']))
                checks.equal('entry %d: name' % index, name, row['filename'])
                if inf:
                    checks.equal('entry %d against the disc\'s NDATA.INF' % index,
                                 (start, length), struct.unpack_from('<HH', inf, index * 4))
            else:
                checks.equal('entry %d' % index, (start, length, name), (0, 0, '-'))
        elif words[0] == 'kind':
            kind, index, name = words[1], int(words[2]), words[3]
            checks.equal('%s (%d)' % (kind, index), name, KIND_FIRST_FILES[kind])
        elif words[0] == 'pack':
            # "pack P model M read", or "... section S bytes N same 0/1"
            pack, model = int(words[1]), int(words[3])
            if words[4] == 'read':
                checks.equal('model %d is in a pack' % model, model, 5)   # M300 is in neither
            elif model in (0, 6):
                # M301 and M306, the last opponents: a pack's first section
                # is one of them (M301 in the first pack, M306 in the
                # second) without its sounds and with fewer textures: five
                # sections where the model's own entry has six, so
                # everything after the header is four bytes nearer
                checks.equal('pack %d: its first section is not entry %d as it is'
                             % (pack, PACKED_MODELS + model), words[9], '0')
                checks.equal('pack %d: its first section\'s count of sections' % pack,
                             int(words[11]), 5)
                if (pack, model) in ((0, 0), (1, 6)):
                    checks.equal('pack %d: its first section is entry %d cut down'
                                 % (pack, PACKED_MODELS + model), int(words[13]), 4)
            else:
                # M302 to M305: the same bytes as their own entries
                checks.equal('pack %d: model %d (%s bytes) is the same as its own entry'
                             % (pack, model, words[7]), words[9], '1')
    checks.equal('entries printed', len(names), 901)
    if not inf:
        checks.note('no disc/NDATA/NDATA.INF: the directory was compared with the file map only')
    if not any(line.startswith('pack') for line in lines):
        checks.note('no NDATA.DAT: the packs were not compared with the models\' own entries')
