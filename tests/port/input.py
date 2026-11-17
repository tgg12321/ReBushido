"""The port's one input point: what else it is linked with.

The settings file has the remastered picture's lines in it
(`draw_distance = max`), whose words the remastered renderer itself
reads (`port_ReadRemasterDistance`), so the test's program is linked with
that file, as tests/port/window.c is; the checks are all in
tests/port/input.c.
"""


def sources():
    return ['src/port/remaster.c']
