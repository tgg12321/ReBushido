"""The cloths of the disc's 37 models: does any point have more springs than
it has room for?

cloth_AddSpring (cloth.c) counts nothing against the eight springs from a
point and the eight to it that a point can hold. This reads every model
file of the archive (docs/formats/NDATA.md, "the cloth stream") its own way:
which meshes are cloth, which points are held, the faces; and joins the
points of each face as the game does. That gives the springs at each point,
whichever way they run. The game's own code makes the same cloths, with the
bone turned eight ways, and says which way each spring runs.

Skipped when neither disc/NDATA/NDATA.DAT nor the disc's image is there."""
import struct

import support

MODEL_COUNT = 37
POINT_SPRINGS = 8      # CLOTH_POINT_SPRINGS: from a point, and to it
SCRATCHPAD_SIZE = 0x400
LOOSE = -256           # CLOTH_PIN_LOOSE: what a point no group names gets
# How many halfwords a face takes and where its corners are, by the kind of
# face (include/mesh.h): triangle, quad, and each with a normal a corner.
FACE_HALFWORDS = (12, 12, 18, 22)
FACE_CORNERS_AT = (6, 7, 6, 8)


def halfwords(data, offset):
    return struct.unpack_from('<%dh' % ((len(data) - offset) // 2), data, offset)


def cloth_meshes(data):
    """{mesh: {point: pin}} from the model file's cloth section: a record
    for each mesh, -2 for a mesh that is not cloth, -3 at the end."""
    stream = halfwords(data, struct.unpack_from('<I', data, 12)[0])
    meshes = {}
    at = 0
    mesh = 0
    while stream[at] != -3:
        if stream[at] != -2:
            pins = {}
            while stream[at] >= 0:       # count, pin, that many points
                count, pin = stream[at], stream[at + 1]
                for point in stream[at + 2:at + 2 + count]:
                    pins[point] = pin
                at += 2 + count
            if stream[at] == -1:         # the ellipsoids: eight halfwords each
                at += 1
                while stream[at] != -2:
                    at += 8
            meshes[mesh] = pins
        at += 1
        mesh += 1
    return meshes


def mesh_faces(data, mesh):
    """(the number of vertices, the groups of faces, each face a list of
    vertex numbers) of one mesh of the file's table."""
    table = struct.unpack_from('<I', data, 4)[0]
    at = table + struct.unpack_from('<I', data, table + 4 + 4 * mesh)[0]
    vertex_count = struct.unpack_from('<H', data, at)[0]
    at += 4 + 6 * vertex_count
    at += at & 2                         # on to a whole word
    groups = []
    while True:
        count, flags = struct.unpack_from('<HH', data, at)
        if count == 0:
            break
        faces = []
        groups.append(faces)
        at += 4
        kind = (flags >> 3) & 3
        corners = 4 if kind & 1 else 3
        for _ in range(count):
            # the corners are a byte each
            faces.append(list(data[at + 2 * FACE_CORNERS_AT[kind]:][:corners]))
            at += 2 * FACE_HALFWORDS[kind]
    return vertex_count, groups


def springs(pins, faces):
    """The pairs of points the faces join: every two corners of a face,
    unless both are held or they are joined already."""
    joined = set()
    for face in faces:
        for i in range(len(face) - 1):
            for j in range(i + 1, len(face)):
                a, b = face[i], face[j]
                if a == b or (pins.get(a, LOOSE) >= 0 and pins.get(b, LOOSE) >= 0):
                    continue
                joined.add(frozenset((a, b)))
    return joined


MODELS = []


def arguments():
    for index in range(MODEL_COUNT):
        MODELS.append(support.ndata_entry(index))
    return [support.work_file('model%02d.bin' % index, data) for index, data in enumerate(MODELS)]


def check(lines, checks):
    expected = {}        # (model, mesh) -> (vertex count, pins, springs)
    longest_list = 0
    many_groups = 0
    for model, data in enumerate(MODELS):
        for mesh, pins in cloth_meshes(data).items():
            vertex_count, groups = mesh_faces(data, mesh)
            faces = [face for group in groups for face in group]
            # cloth_CreatePiece ends its list of faces after every group
            # of the mesh, and cloth_Build stops at the first end: only the
            # first group's faces are joined
            expected[(model, mesh)] = (vertex_count, pins, springs(pins, groups[0]))
            name = 'model %d mesh %d' % (model, mesh)
            # which loses nothing: where a mesh has a second group (14 of
            # the 34 have), its faces join no two points the first's do not
            checks.equal(name + ': the springs of all its groups of faces',
                         springs(pins, faces), springs(pins, groups[0]))
            many_groups += len(groups) > 1
            # the list is made in the scratchpad: a count, a kind and the
            # corners of each face, in halfwords, and an end for each group
            list_bytes = 2 * (sum(2 + len(face) for face in faces) + len(groups))
            checks.true('%s: its list of faces is %d bytes, and the scratchpad %d'
                        % (name, list_bytes, SCRATCHPAD_SIZE), list_bytes <= SCRATCHPAD_SIZE)
            longest_list = max(longest_list, list_bytes)

    seen = set()
    most_out = most_in = most_at_a_point = 0
    cloth = None
    for line in lines:
        words = line.split()
        if words[0] == 'model':          # "model M none": it has no cloth
            checks.true('model %s has no cloth' % words[1],
                        not any(key[0] == int(words[1]) for key in expected))
        elif words[0] == 'cloth':
            model, mesh, pose, points, spring_count = [int(w) for w in words[1:]]
            cloth = (model, mesh)
            name = 'model %d mesh %d' % cloth
            seen.add(cloth)
            checks.true(name + ' is cloth', cloth in expected)
            if cloth not in expected:
                cloth = None
                continue
            vertex_count, pins, joined = expected[cloth]
            checks.equal(name + ': points', points, vertex_count)
            checks.equal(name + ': springs (pose %d)' % pose, spring_count, len(joined))
            made = set()
        elif cloth and words[0] == 'point':
            point, pin, out_count, in_count = [int(w) for w in words[1:]]
            checks.equal('%s point %d: pin' % (name, point), pin, pins.get(point, LOOSE))
            at_point = sum(1 for pair in joined if point in pair)
            checks.equal('%s point %d: springs from it and to it' % (name, point),
                         out_count + in_count, at_point)
            # the question: no more than it has room for, either way
            checks.true('%s point %d (pose %d) has %d springs from it and %d to it'
                        % (name, point, pose, out_count, in_count),
                        out_count <= POINT_SPRINGS and in_count <= POINT_SPRINGS)
            most_out = max(most_out, out_count)
            most_in = max(most_in, in_count)
            most_at_a_point = max(most_at_a_point, at_point)
        elif cloth and words[0] == 'spring':
            made.add(frozenset((int(words[1]), int(words[2]))))
            if len(made) == len(joined):
                checks.equal(name + ': the springs', made, joined)
    checks.equal('the cloth meshes made', seen, set(expected))
    checks.note('%d cloths; the most springs at one point: %d from it, %d to it, %d in all; '
                'the longest list of faces: %d bytes; %d meshes have a second group of faces'
                % (len(expected), most_out, most_in, most_at_a_point, longest_list, many_groups))
