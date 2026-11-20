"""The mods made from the player's disc at the first start (tools/port_prepare.py): its runs' settings are the window's, its plans are the developer tools', and it never writes over a mod of the player's

What can be held without the disc: every setting the program's runs play
with is one the window reads (src/port/window/settings_list.h), so a
renamed setting cannot leave a run dumping with the player's own; the art's
plans are port_art's and the stages' port_stage_hd's; the mods are the
three src/port/window/prepare.c asks for; a folder of mods/ with a mod.txt
and no made-from-disc.txt is the player's and is left as it is, one with
the marker is made again, and a mod is moved into place whole. The proof
with the disc, the package's own program and Python against the developer
tools' bytes is tools/port_prepare_prove.py (docs/MODS.md, "Made from your
disc")."""
import os
import re
import shutil

import support
import port_art
import port_prepare
import port_stage_hd

SETTINGS = os.path.join(support.ROOT, 'src', 'port', 'window', 'settings_list.h')
PREPARE_C = os.path.join(support.ROOT, 'src', 'port', 'window', 'prepare.c')


def run(checks):
    with open(SETTINGS, encoding='utf-8') as f:
        names = set(re.findall(r'\.name = "([a-z_0-9]+)"', f.read()))
    for line in port_prepare.CHILD_SETTINGS.splitlines():
        if line.startswith('#') or not line.strip():
            continue
        name = line.split('=')[0].strip()
        checks.true('the runs\' setting %s is one the window reads' % name, name in names)
    with open(PREPARE_C, encoding='utf-8') as f:
        text = f.read()
    asked = re.search(r'g_prepare_mods\[\] = \{([^}]*)\}', text)
    checks.equal('the mods prepare.c asks for', re.findall(r'"([^"]+)"', asked.group(1)) if asked else None,
                 port_prepare.MODS)
    checks.true('prepare.c reads the marker port_prepare.py writes',
                '"%s"' % port_prepare.MARKER in text and 'key = %s' in port_prepare.MARKER_TEXT)
    checks.equal('the art tour\'s limit is port_art\'s', port_prepare.ART_LIMIT,
                 port_art.plan_text.__defaults__[0])
    checks.equal('the stages', port_stage_hd.ALL_STAGES, 38)
    checks.true('every art mode has a row', all(m in port_art.MODE_ROWS for m in port_art.MODES))

    work = os.path.join(support.ROOT, 'build', 'tests', 'port_prepare')
    if os.path.isdir(work):
        shutil.rmtree(work)
    mods = os.path.join(work, 'mods')
    try:
        own = os.path.join(mods, 'art')
        os.makedirs(own)
        with open(os.path.join(own, 'mod.txt'), 'w') as f:
            f.write('name = mine\n')
        checks.true('a mod with a mod.txt and no marker is the player\'s', port_prepare.is_players(own))
        empty = os.path.join(mods, 'stages-hd')
        os.makedirs(empty)
        checks.true('an empty folder is not the player\'s', not port_prepare.is_players(empty))
        made = os.path.join(mods, 'fighters')
        os.makedirs(made)
        with open(os.path.join(made, port_prepare.MARKER), 'w') as f:
            f.write(port_prepare.MARKER_TEXT % 'old')
        with open(os.path.join(made, 'old.txt'), 'w') as f:
            f.write('old\n')
        checks.true('a mod with the marker is made again', not port_prepare.is_players(made))
        staging = os.path.join(work, 'staging')
        os.makedirs(staging)
        with open(os.path.join(staging, 'mod.txt'), 'w') as f:
            f.write('name = new\n')
        port_prepare.put_in_place(staging, made)
        checks.equal('the new mod is in place, whole', sorted(os.listdir(made)), ['mod.txt'])
        checks.true('the staging folder is gone', not os.path.exists(staging))
        checks.true('nothing of the old one is left beside it', not os.path.exists(made + '.old'))
        progress = port_prepare.Progress(port_prepare.MODS)
        checks.equal('the weights add up', progress.total, sum(port_prepare.WEIGHTS.values()))
    finally:
        shutil.rmtree(work)
