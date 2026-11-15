"""The window's arithmetic: what else it is linked with.

The settings file has the remastered picture's lines in it
(`draw_distance = max`), whose words the remastered renderer itself
reads (`port_ReadRemasterDistance`), so that the names are said in one
place. So the test's program is linked with that file; the checks are
all in tests/port/window.c.
"""


def sources():
    return ['src/port/remaster.c']
