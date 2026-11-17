"""The settings menu's arithmetic: what else it is linked with.

The settings file's reader (src/port/window/keymap.c) has the remastered
picture's words in it (`draw_distance = max`), which the remastered
renderer itself reads (`port_ReadRemasterDistance`), so the test's program
is linked with that file, as tests/port/window.py's is; the checks are all
in tests/port/settings.c.
"""


def sources():
    return ['src/port/remaster.c']
