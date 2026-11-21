"""The art mod's learned upscaler (tools/port_art.py): where the textures were, the pictures put back together, which kind goes to the model, and the way without it

Held without the disc, the model or a graphics card: the dump's log lines
of where each texture was (`wrote` and `had`) are read into where.txt,
sorted, whatever order the runs said them in; a 4-bit texture's place
across video memory is counted in its own texels; two tiles that go on
from each other are one canvas, two laid side by side that do not (the
top menu's moon) and two that do not touch are not, and two that overlap
and disagree are on canvases of their own; a texture of one ramp of one
hue is kept to today's way and a painting of more is the model's; an
upscaler that fails gives nothing, with why, so that build makes those
textures as it always did; and `off` finds none (docs/MODS.md, "The art
mod")."""
import os
import shutil
import stat

import support
import port_art


def tile(seed, w=8, h=8):
    """An opaque texture of many colours that runs smoothly across: a
    piece of a painting."""
    import numpy as np

    y, x = np.mgrid[0:h, 0:w]
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[:, :, 0] = (seed + x * 7) % 256
    rgba[:, :, 1] = (seed * 3 + y * 5) % 256
    rgba[:, :, 2] = (x * y + seed) % 256
    rgba[:, :, 3] = 255
    return rgba


def run(checks):
    import numpy as np

    log = ('mods: wrote d/64x64-4-00000000000000aa.png (page 832 256, at 64 0, table 0 508)\n'
           'something else\n'
           'mods: had d/64x64-4-00000000000000bb.png (page 832 256, at 0 0, table 0 508)\n')
    lines = port_art.where_lines(log)
    checks.equal('both kinds of line are read', sorted(lines), [
        '64x64-4-00000000000000aa 832 256 64 0 0 508', '64x64-4-00000000000000bb 832 256 0 0 0 508'])

    work = os.path.join(support.ROOT, 'build', 'tests', 'port_art')
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)
    try:
        port_art.add_where(work, {lines.pop()})
        port_art.add_where(work, lines)
        with open(os.path.join(work, port_art.WHERE), encoding='utf-8') as f:
            text = f.read()
        checks.true('where.txt is sorted, whatever order it was said in',
                    text.splitlines()[1:] == sorted(text.splitlines()[1:]) and len(text.splitlines()) == 3)
        where = port_art.read_where([work])
        checks.equal('a 4-bit texture\'s place is in its own texels', where['64x64-4-00000000000000aa'],
                     [(4, 0, 508, 832 * 4 + 64, 256)])

        left = tile(10)
        right = tile(10)
        y, x = np.mgrid[0:8, 0:8]
        right[:, :, 0] = (10 + (x + 8) * 7) % 256
        right[:, :, 2] = ((x + 8) * y + 10) % 256
        textures = {'a': left, 'b': right, 'far': tile(20), 'moon-top': tile(40), 'moon-bottom': 255 - tile(40)}
        where = {'a': [(4, 0, 0, 0, 0)], 'b': [(4, 0, 0, 8, 0)], 'far': [(4, 0, 0, 40, 40)],
                 'moon-top': [(4, 1, 0, 0, 0)], 'moon-bottom': [(4, 1, 0, 8, 0)]}
        laid = port_art.canvases(textures, sorted(textures), where)
        members = sorted(sorted(n for n, _, _ in m) for _, _, _, _, m in laid)
        checks.equal('tiles that go on are one canvas; the others are each their own', members,
                     [['a', 'b'], ['far'], ['moon-bottom'], ['moon-top']])
        joined = [c for c in laid if len(c[4]) == 2][0]
        checks.equal('the canvas of two is both tiles wide', joined[2:4], (16, 8))

        other = tile(99)
        laid = port_art.canvases({'a': left, 'c': other}, ['a', 'c'], {'a': [(4, 0, 0, 0, 0)], 'c': [(4, 0, 0, 4, 0)]})
        checks.equal('two that overlap and disagree are apart', len(laid), 2)

        ramp = np.zeros((32, 32, 4), np.uint8)
        steps = np.linspace(0, 1, 32)[None, :, None] * np.ones((32, 1, 1))
        ramp[:, :, :3] = (np.array([0, 0, 49]) + steps * np.array([120, 160, 121])).astype(np.uint8)
        ramp[:, :, 3] = 255
        checks.equal('a soft shape of one hue is kept to today\'s way', port_art.kind_of('32x32-4-0', ramp),
                     'lettering')
        palette = np.array([[(i * 37) % 256, (i * 91) % 256, (i * 151) % 256, 255] for i in range(12)], np.uint8)
        y, x = np.mgrid[0:32, 0:32]
        painting = palette[(x * 5 + y * 3 + (x * y) % 7) % 12]
        checks.equal('an opaque texture of many hues is a painting', port_art.kind_of('32x32-4-1', painting),
                     'painting')
        checks.equal('a painting falls back to the way it always had', port_art.FALLBACK_OF['painting'], 'lettering')

        checks.equal('off finds no upscaler', port_art.find_upscaler('off'), None)
        fake = os.path.join(work, 'upscaler')
        os.makedirs(os.path.join(fake, 'models'))
        checks.equal('a folder without its models is no upscaler', port_art.find_upscaler(fake), None)
        for model in set(port_art.MODEL_OF.values()):
            open(os.path.join(fake, 'models', model + '.bin'), 'wb').close()
        exe = os.path.join(fake, port_art.UPSCALER + ('.exe' if os.name == 'nt' else ''))
        if os.name != 'nt':
            # a card with no Vulkan: the program says so and writes nothing
            with open(exe, 'w', newline='\n') as f:
                f.write('#!/bin/sh\necho "invalid gpu device" >&2\nexit 255\n')
            os.chmod(exe, os.stat(exe).st_mode | stat.S_IEXEC)
            checks.equal('a folder with the program and its models is found', port_art.find_upscaler(fake),
                         os.path.abspath(fake))
            made, why = port_art.upscale_by_model(fake, {'a': (left, port_art.MODEL_OF['picture'])}, 6,
                                                  os.path.join(work, 'model'))
            checks.true('an upscaler that fails gives nothing, and says why',
                        made == {} and why is not None and 'invalid gpu device' in why)
    finally:
        shutil.rmtree(work)
