#!/usr/bin/env python3
"""The window's processor time for a fight, held to a budget (docs/PORT.md,
"Where the time goes", "The budget"):

    python3 tools/port_budget.py [--keep]

It runs the window's build (build/port-sdl/linux/rebushido, `make
port-sdl`) three times side by side, with the player's defaults and the
HD fighters' mod, through SDL's offscreen driver on Mesa's software
OpenGL: port_game's plan with Kannuki against Mikado on Tatara Shrine (a
`poke`: both wear cloth), with `run_ahead` 0, 1 and 2, each with
`--profile`. Over the fight it takes, for each blank in which the game
drew a frame, the parts of the processor's own time (the game, the
software GPU, the side channel, the sound, the frames ahead, the HD
cloth, the cast shadows: include/port_profile.h), and what the session's
log counts.

It fails where

  - with nothing run ahead, the game, the software GPU, the sound and
    the cloth take more than OWN_MAX milliseconds of a fight frame;
  - run_ahead 1 costs more than run_ahead 0 by more than a bare step of
    the game, the state kept and put back and AHEAD_OVER_MAX (a frame
    ahead draws the picture its step no longer draws, and paints nothing
    into the software GPU's memory);
  - run_ahead 2 costs more than RATIO_MAX times run_ahead 0, or the cloth
    more than CLOTH_SHARE_MAX of it;
  - the cloth is kept out of the body more than HOLDS_A_FRAME_MAX times a
    frame written down, or more than PALETTES_MAX colour tables are sent
    to the card: what is done once and not again for every picture.

The runs share the machine, so a busy machine slows them all; the
milliseconds are those of the owner's PC, with room to spare. Where the window's build, the
disc image or the hd mod is not there, it says so and passes: nothing of
the build needs them. The runs go to build/budget/.
"""
import argparse
import concurrent.futures
import os
import re
import statistics
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

PROGRAM = os.path.join('build', 'port-sdl', 'linux', 'rebushido')
OUT_DIR = os.path.join('build', 'budget')
# The fight of port_game's plan, in blanks: it begins at about 2900.
FIGHT_FROM = 3100
FIGHT_TO = 3500
FRAMES = FIGHT_TO + 60
# The parts that are the processor's own work and not Mesa's drawing.
PARTS = ('game', 'raster', 'remaster', 'sound', 'window', 'ahead', 'cloth', 'shadow')
# Measured (2026-11-17, the owner's PC in WSL, the plan below): 1.5 and
# 11%, with the cloth kept out once a frame; with it kept out once a
# picture and for every frame ahead, as merged at 1e6878df3, BEFORE_RATIO
# and 28%.
RATIO_MAX = 2.0
CLOTH_SHARE_MAX = 0.20
RUN_AHEADS = (0, 1, 2)
# The processor's own parts of a fight frame with nothing run ahead, in
# milliseconds of the window's thread's own processor time (RB_PROFILE_CPU),
# which other programs on the machine do not lengthen (here, 2026-11-18:
# 20.5, of it the cloth 10; with the cloth kept out for every picture, as
# merged at 1e6878df3, over 40). tools/gate.py runs this step alone, after
# every other (ALONE), all the same.
OWN_PARTS = ('game', 'raster', 'sound', 'cloth')
OWN_MAX = 26.0
# What run_ahead 1 may cost over 0 beyond a bare step of the game and the
# state kept and put back (here: about 1).
AHEAD_OVER_MAX = 4.0
# The cloth kept out of the body, for each frame written down for the
# pictures between frames (here: 0.4; once a picture: several), and the
# colour tables sent in the run (here: 4; once a drawing: thousands).
HOLDS_A_FRAME_MAX = 1.0
PALETTES_MAX = 64
# The plan's pokes: the second fighter Mikado (ROSTER_MIKADO) and the
# stage Tatara Shrine (3), from the character select on.
POKES = (('config_roster_p2', 5), ('config_stage', 3), ('stage', 3))
SETTINGS = """graphics = remastered
draw_distance = max
frame_rate = display
stage_mesh = on
stage_props_mesh = on
antialiasing = 2
shadows = cast
fighter_models = on
see_through = on
sky = night
smooth_light = on
effects = smooth
quick_loading = on
low_latency = on
run_ahead = {run_ahead}
"""


def main_checkout():
    """The main checkout of a worktree, where the disc image and the mods
    are kept; this checkout where it is the main one."""
    # an agent's worktree, under the main checkout's .claude/worktrees/
    # (WSL's git cannot read a worktree Windows' git made)
    marker = os.sep + os.path.join('.claude', 'worktrees') + os.sep
    if marker in REPO + os.sep:
        return REPO[:REPO.index(marker)]
    try:
        common = subprocess.run(['git', 'rev-parse', '--git-common-dir'], cwd=REPO,
                                capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return REPO
    return os.path.dirname(os.path.abspath(os.path.join(REPO, common)))


def find(*candidates):
    for path in candidates:
        if path and os.path.exists(path):
            return path
    return None


def write_plan(out):
    import playtest

    playtest.PORT_PROGRAM = os.path.abspath(PROGRAM)
    playtest.PORT_SYMBOLS = playtest.PORT_PROGRAM + '.sym'
    test = playtest.Test(os.path.join('tests', 'play', 'port_game.toml'))
    _, lines = test.plan(playtest.Disc('port'), playtest.header_names(), 1)
    where = {}
    for line in lines:
        words = line.split()
        if words and words[0] == 'var':
            where[words[1]] = (int(words[2]), int(words[3]))
    cps = [line.split()[1] for line in lines if line.startswith('cp ')]
    select = cps.index('select') + 1
    for name, value in POKES:
        address, size = where[name]
        lines.append('poke %d 0 %d %d %d %d' % (select, FRAMES, address, size, value))
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def run(run_ahead, image, mods, plan):
    folder = os.path.join(OUT_DIR, 'ra%d' % run_ahead)
    os.makedirs(folder, exist_ok=True)
    config = os.path.join(folder, 'rebushido.cfg')
    with open(config, 'w', encoding='utf-8', newline='\n') as f:
        f.write(SETTINGS.format(run_ahead=run_ahead))
    # the window's thread's own processor time, which the gate's other
    # runs on the machine do not lengthen (port_profile.h)
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
               LIBGL_ALWAYS_SOFTWARE='1', RB_PROFILE_CPU='1')
    for name in ('RB_HD_CLOTH', 'RB_HD_CLOTH_HOLD'):
        env.pop(name, None)
    profile = os.path.join(folder, 'profile.txt')
    cmd = ['./' + PROGRAM, '--image', image, '--config', config, '--mods', mods,
           '--plan', plan, '--out', folder, '--no-cards', '--fast', '--run-ahead', str(run_ahead),
           '--window', '320x240', '--frames', str(FRAMES), '--profile', profile,
           '--log', os.path.join(folder, 'log.txt')]
    with open(os.path.join(folder, 'output.txt'), 'w', encoding='utf-8', errors='replace') as out:
        done = subprocess.run(cmd, cwd=REPO, env=env, stdout=out, stderr=out)
    # 1: the run ended at --frames, before the plan's last checkpoints
    if done.returncode not in (0, 1):
        sys.exit('port-budget: the run with run_ahead %d ended with %d (%s)'
                 % (run_ahead, done.returncode, os.path.join(folder, 'output.txt')))
    return profile


def read(profile):
    """{part: median ms} over the fight's blanks in which the game drew."""
    parts = []
    rows = []
    with open(profile, encoding='utf-8') as f:
        for line in f:
            words = line.split()
            if words and words[0] == 'parts':
                parts = words[1:]
            elif len(words) > 3 and words[0] == 'b' and words[1].isdigit():
                blank = int(words[1])
                if FIGHT_FROM <= blank <= FIGHT_TO and words[2] == '1':
                    rows.append([int(w) for w in words[3:]])
    if not rows or not parts:
        sys.exit('port-budget: %s has no blank of the fight' % profile)
    return {name: statistics.median(row[i] for row in rows) / 1000.0
            for i, name in enumerate(parts) if i < len(rows[0])}, len(rows)


def counts(output):
    """What the run's log says it did: the frames written down, the cloth's
    holds, the colour tables sent, and the state's share of a boundary."""
    said = {}
    with open(output, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.search(r'(\d+) frames written down', line)
            if m:
                said['frames'] = int(m.group(1))
            m = re.search(r'(\d+) colour tables sent; the HD cloth kept out of the body (\d+) times', line)
            if m:
                said['palettes'] = int(m.group(1))
                said['holds'] = int(m.group(2))
            m = re.search(r'ms a boundary, ([\d.]+) of it the state', line)
            if m:
                said['state'] = float(m.group(1))
    return said


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.parse_args()
    os.chdir(REPO)
    main_repo = main_checkout()
    image = find(os.environ.get('REBUSHIDO_RETAIL'),
                 os.path.join(REPO, 'Bushido Blade 2 (USA).cue'),
                 os.path.join(main_repo, 'Bushido Blade 2 (USA).cue'))
    mods = None
    for root in (os.path.join(REPO, 'mods'), os.path.join(main_repo, 'mods')):
        if os.path.isdir(os.path.join(root, 'hd', 'models')):
            mods = root
            break
    for what, path in (('the window\'s build (make port-sdl)', find(PROGRAM)),
                       ('the disc image (REBUSHIDO_RETAIL)', image), ('the hd mod (mods/hd)', mods)):
        if path is None:
            print('port-budget: skipped: there is no %s' % what)
            return 0
    os.makedirs(OUT_DIR, exist_ok=True)
    plan = os.path.join(OUT_DIR, 'plan.txt')
    write_plan(plan)
    # it times: alone on the machine's card and its processors as far as
    # the other checkouts' benches and provers go (tools/card_lock.py)
    import card_lock

    card_lock.hold(True, 'port-budget')
    with concurrent.futures.ThreadPoolExecutor(len(RUN_AHEADS)) as pool:
        profiles = list(pool.map(lambda r: run(r, image, mods, plan), RUN_AHEADS))
    runs = {}
    for ra, profile in zip(RUN_AHEADS, profiles):
        parts, n = read(profile)
        runs[ra] = (parts, n, sum(parts.get(p, 0.0) for p in PARTS),
                    counts(os.path.join(os.path.dirname(profile), 'output.txt')))
    print('port-budget: a fight frame\'s processor time (median ms, the parts %s)' % ', '.join(PARTS))
    for ra in RUN_AHEADS:
        parts, n, cost, said = runs[ra]
        print('  run_ahead %d %7.2f ms over %d frames: %s' % (
            ra, cost, n, ' '.join('%s %.2f' % (p, parts.get(p, 0.0)) for p in PARTS)))
    zero, _, cost0, said0 = runs[0]
    one, _, cost1, said1 = runs[1]
    two, _, cost2, _ = runs[2]
    checks = []
    # the processor's own work of a fight frame, with nothing run ahead:
    # a number of milliseconds, not a ratio
    own = sum(zero.get(p, 0.0) for p in OWN_PARTS)
    checks.append(('run_ahead 0: the game, the software GPU, the sound and the cloth',
                   own, OWN_MAX, 'ms'))
    # a frame run ahead is the game's step and the state kept and put
    # back: it draws the picture the step after it no longer draws, and
    # paints nothing into the software GPU's memory
    bare = zero.get('game', 0.0) + zero.get('sound', 0.0) + said1.get('state', 0.0)
    checks.append(('run_ahead 1 over 0, less a bare step and the state (%.2f ms)' % bare,
                   cost1 - cost0 - bare, AHEAD_OVER_MAX, 'ms'))
    checks.append(('run_ahead 2 / 0', cost2 / cost0 if cost0 > 0 else 0.0, RATIO_MAX, ''))
    checks.append(('the cloth\'s share of run_ahead 2',
                   two.get('cloth', 0.0) / cost2 if cost2 > 0 else 0.0, CLOTH_SHARE_MAX, ''))
    # what is done once and not again for every picture
    frames = max(said0.get('frames', 0), 1)
    checks.append(('the cloth kept out, a frame written down', said0.get('holds', 0) / frames,
                   HOLDS_A_FRAME_MAX, ''))
    checks.append(('colour tables sent to the card', said0.get('palettes', 0), PALETTES_MAX, ''))
    failed = False
    for what, value, most, unit in checks:
        bad = value > most
        failed = failed or bad
        print('  %-66s %8.2f%s (at most %.2f)%s' % (what, value, unit, most, '  FAILED' if bad else ''))
    print('port-budget: %s' % ('FAILED' if failed else 'OK'))
    return 1 if failed else 0

if __name__ == '__main__':
    sys.exit(main())
