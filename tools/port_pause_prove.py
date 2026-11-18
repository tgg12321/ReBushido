#!/usr/bin/env python3
"""The pause menu on the real graphics card, at every run-ahead
(docs/PORT.md, "The pause and the frames ahead").

    python tools/port_pause_prove.py           every case, a verdict
    python tools/port_pause_prove.py --reuse   the runs already made

From Windows' Python, on the PC with the card, as tools/port_bench.py
runs the window's Windows build (--hidden, SDL's dummy sound, settings of
its own with the counter off, no memory cards, the five mods, unpaced):
straight into a fight (--fight), Start pressed a second in, and the
window's own pixels kept (--tap-window) every TAP_EVERY blanks to the
end, the game held by the pause menu all the while. The cases are the
remastered picture with run_ahead 0, 1 and 2 and the original picture
with 0 and 2.

While the game is held its picture is still: the fight's last frame
under the menu's panel. So each case's last TAPS pictures are held to
one another, to the same picture's case with nothing run ahead, and the
case with nothing run ahead to its picture before the pause, where the
panel's box must be darker. A picture of the pause that changed with the
frames run ahead (the menu not shown with run_ahead 2, 2026-11-18) fails.
Where this is not Windows, or there is no card, no window's build or no
disc image, it says so and passes. The runs are in build/pause/.
"""
import argparse
import os
import re
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

import port_bench  # noqa: E402
import port_budget  # noqa: E402

OUT_DIR = os.path.join(REPO, 'build', 'pause')
WINDOW = '640x360'
FIGHT = ['tatara-shrine', 'kannuki', 'mikado']
# the game's frames into the fight that Start is pressed at
PAUSE_AT = 60
# blanks in all: the fight begins by blank 600, the pause a few seconds
# before the end
BLANKS = 1300
TAP_EVERY = 10
# the pictures at the end, all of the pause
TAPS = 6
RUN_SECONDS = 240
# (name, picture, run_ahead); the first of each picture is the one held
# to the others
CASES = [('remastered-0', 'remastered', 0), ('remastered-1', 'remastered', 1),
         ('remastered-2', 'remastered', 2), ('original-0', 'original', 0),
         ('original-2', 'original', 2)]
# the mean of a pixel's three channels' differences, out of 765, that two
# pictures of one held screen may differ by (the same in practice: 0.0 to
# 0.3); the menu missing was 9 to 12
SAME_MAX = 2.0
# the panel's box (fractions of the picture) and how much darker it is
# under the panel than before the pause, at least
BOX = (0.38, 0.62, 0.42, 0.58)
DARKER = 0.9
TAP_MAGIC_BYTES = 16


def write_plan(out):
    """port_game's plan's header and its fight checkpoint, Start pressed
    PAUSE_AT frames into the fight, and a checkpoint past the run's end."""
    whole = os.path.join(OUT_DIR, 'whole_plan.txt')
    port_bench.write_plan(whole)
    lines = []
    with open(whole, encoding='utf-8') as f:
        for line in f:
            words = line.split()
            if not words or words[0] in ('in', 'poke'):
                continue
            if words[0] == 'cp':
                if words[1] == 'fight':
                    lines.append(line.rstrip('\n'))
                continue
            lines.append(line.rstrip('\n'))
    lines.append('in 1 %d 1 START 3 f' % PAUSE_AT)
    lines.append('cp paused 0 0 frames %d' % (BLANKS * 2))
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def run(name, picture, run_ahead, image, mods, plan):
    folder = os.path.join(OUT_DIR, name)
    taps = os.path.join(folder, 'tap')
    os.makedirs(taps, exist_ok=True)
    for old in os.listdir(taps):
        if old.endswith('.raw'):
            os.remove(os.path.join(taps, old))
    config = os.path.join(folder, 'rebushido.cfg')
    settings = port_budget.SETTINGS.format(run_ahead=run_ahead).replace(
        'graphics = remastered', 'graphics = ' + picture)
    with open(config, 'w', encoding='utf-8', newline='\n') as f:
        f.write(settings + 'fps = off\n')
    cmd = [port_bench.PROGRAM, '--hidden', '--image', image, '--config', config, '--mods', mods,
           '--plan', plan, '--fight'] + FIGHT + [
           '--out', folder, '--no-cards', '--fast', '--run-ahead', str(run_ahead),
           '--window', WINDOW, '--frames', str(BLANKS), '--log', os.path.join(folder, 'log.txt'),
           '--tap-window', taps, '--tap-every', str(TAP_EVERY)]
    env = dict(os.environ, SDL_AUDIODRIVER='dummy')
    env.pop('SDL_VIDEODRIVER', None)
    with open(os.path.join(folder, 'output.txt'), 'w', encoding='utf-8', errors='replace') as out:
        process = subprocess.Popen(cmd, env=env, stdout=out, stderr=out, stdin=subprocess.DEVNULL)
        try:
            process.wait(timeout=RUN_SECONDS)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            sys.exit('port-pause: %s did not end in %d s (%s)' % (name, RUN_SECONDS, folder))
    # 1: ended at --frames
    if process.returncode not in (0, 1):
        sys.exit('port-pause: %s ended with %d (%s)' % (name, process.returncode, folder))
    return folder


def picture(path):
    with open(path, 'rb') as f:
        data = f.read()
    _, w, h, _ = struct.unpack('<4I', data[:TAP_MAGIC_BYTES])
    return w, h, data[TAP_MAGIC_BYTES:]


def taps_of(folder):
    """[(blank, path)] of the run's pictures, in order."""
    taps = os.path.join(folder, 'tap')
    found = []
    for name in os.listdir(taps):
        m = re.fullmatch(r'window-(\d+)\.raw', name)
        if m:
            found.append((int(m.group(1)), os.path.join(taps, name)))
    return sorted(found)


def difference(a, b):
    """The mean of a pixel's three channels' differences, every 7th pixel."""
    if a[:2] != b[:2]:
        return 765.0
    x, y = a[2], b[2]
    step = 4 * 7
    total = 0
    count = 0
    for i in range(0, len(x), step):
        total += abs(x[i] - y[i]) + abs(x[i + 1] - y[i + 1]) + abs(x[i + 2] - y[i + 2])
        count += 1
    return total / count


def box_light(p):
    w, h, data = p
    left, right, top, bottom = BOX
    total = 0
    count = 0
    for y in range(int(h * top), int(h * bottom), 2):
        row = y * w * 4
        for x in range(int(w * left), int(w * right), 2):
            i = row + x * 4
            total += data[i] + data[i + 1] + data[i + 2]
            count += 1
    return total / count


def fight_blank(folder):
    with open(os.path.join(folder, 'log.txt'), encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.search(r'game: screen 1, fight;.*?blank (\d+)', line)
            if m:
                return int(m.group(1))
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--reuse', action='store_true', help='hold the runs already in build/pause/')
    ap.add_argument('--case', action='append', help='only these cases (%s)' % ', '.join(c[0] for c in CASES))
    args = ap.parse_args()
    main_repo = port_budget.main_checkout()
    image = port_budget.find(os.environ.get('REBUSHIDO_RETAIL'),
                             os.path.join(REPO, 'Bushido Blade 2 (USA).cue'),
                             os.path.join(main_repo, 'Bushido Blade 2 (USA).cue'))
    for what, there in (('a graphics card (Windows, NVIDIA, AMD or Intel)', port_bench.card_name()),
                        ("the window's Windows build (make port-sdl-windows)",
                         os.path.exists(port_bench.PROGRAM) or None),
                        ('the disc image (REBUSHIDO_RETAIL)', image)):
        if there is None:
            print('port-pause: skipped: there is no %s' % what)
            return 0
    os.makedirs(OUT_DIR, exist_ok=True)
    mods, missing = port_bench.make_mods(main_repo)
    if missing:
        print('port-pause: without the mods %s (not in tmp/mods/)' % ', '.join(missing))
    plan = os.path.join(OUT_DIR, 'plan.txt')
    write_plan(plan)
    cases = [c for c in CASES if not args.case or c[0] in args.case]
    failures = []
    held = {}
    for name, kind, run_ahead in cases:
        folder = os.path.join(OUT_DIR, name) if args.reuse else run(name, kind, run_ahead, image, mods, plan)
        taps = taps_of(folder)
        first = fight_blank(folder)
        if first is None or len(taps) < TAPS + 1 or taps[-TAPS][0] < first + 2 * PAUSE_AT + 120:
            failures.append('%s: the fight and its pause were not reached (%s)' % (name, folder))
            continue
        last = [picture(path) for _, path in taps[-TAPS:]]
        steady = max(difference(last[0], p) for p in last[1:])
        line = '%s: the pause, blanks %d to %d: %.2f apart at most' % (
            name, taps[-TAPS][0], taps[-1][0], steady)
        if steady > SAME_MAX:
            failures.append('%s: the pause\'s picture changes while the game is held (%.2f)' % (name, steady))
        if kind not in held:
            # the case with nothing run ahead: its panel shown, where the
            # fight was before the pause
            before = [p for b, p in taps if first + 20 <= b < first + 2 * PAUSE_AT - 10]
            if not before:
                failures.append('%s: no picture of the fight before the pause' % name)
                continue
            dark = box_light(last[-1]) / max(1.0, box_light(picture(before[-1])))
            line += '; the panel\'s box %.2f as light as the fight before' % dark
            if dark > DARKER:
                failures.append('%s: no pause menu over the fight (its box %.2f as light)' % (name, dark))
            held[kind] = last[-1]
        else:
            apart = difference(held[kind], last[-1])
            line += '; %.2f from %s with nothing run ahead' % (apart, kind)
            if apart > SAME_MAX:
                failures.append('%s: the pause is not the picture it is with nothing run ahead (%.2f)'
                                % (name, apart))
        print('port-pause: ' + line, flush=True)
    for failure in failures:
        print('port-pause: FAILED: ' + failure)
    if failures:
        return 1
    print('port-pause: OK: the pause menu shown and still in every case')
    return 0


if __name__ == '__main__':
    sys.exit(main())
