#!/usr/bin/env python3
"""The window's processor time for a fight, held to a budget (docs/PORT.md,
"Where the time goes", "The budget"):

    python3 tools/port_budget.py [--keep]

It runs the window's build (build/port-sdl/linux/rebushido, `make
port-sdl`) twice side by side, with the player's defaults and the HD
fighters' mod, through SDL's offscreen driver on Mesa's software OpenGL:
port_game's plan with Kannuki against Mikado on Tatara Shrine (a `poke`:
both wear cloth), once with `run_ahead 0` and once with `run_ahead 2`,
each with `--profile`. Over the fight it adds up, for each blank in which
the game drew a frame, the parts of the processor's own time that do not
depend on the software OpenGL (the game, the software GPU, the side
channel, the sound, the frames ahead, the HD cloth, the cast shadows:
include/port_profile.h).

It fails where

  - run-ahead 2 costs more than RATIO_MAX times run-ahead 0 (two frames
    ahead are two more steps of the game and of the side channel; what a
    picture costs, the cloth above all, may not be paid again for them);
  - the HD cloth costs more than CLOTH_SHARE_MAX of that time (it is kept
    out of the body once a frame, not once a picture).

The two runs share the machine, so a busy machine slows both; the
measure is their ratio, not milliseconds. Where the window's build, the
disc image or the hd mod is not there, it says so and passes: nothing of
the build needs them. The runs go to build/budget/.
"""
import argparse
import concurrent.futures
import os
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
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
               LIBGL_ALWAYS_SOFTWARE='1')
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
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        profiles = list(pool.map(lambda r: run(r, image, mods, plan), (0, 2)))
    (zero, n0), (two, n2) = read(profiles[0]), read(profiles[1])
    cost0 = sum(zero.get(p, 0.0) for p in PARTS)
    cost2 = sum(two.get(p, 0.0) for p in PARTS)
    print('port-budget: a fight frame\'s processor time (median ms, the parts %s)' % ', '.join(PARTS))
    for name, parts, n, cost in (('run_ahead 0', zero, n0, cost0), ('run_ahead 2', two, n2, cost2)):
        print('  %-12s %7.2f ms over %d frames: %s' % (
            name, cost, n, ' '.join('%s %.2f' % (p, parts.get(p, 0.0)) for p in PARTS)))
    ratio = cost2 / cost0 if cost0 > 0 else 0.0
    cloth = two.get('cloth', 0.0) / cost2 if cost2 > 0 else 0.0
    print('  run_ahead 2 / 0: %.2f (at most %.2f); the cloth %.0f%% of run_ahead 2 (at most %.0f%%)'
          % (ratio, RATIO_MAX, 100 * cloth, 100 * CLOTH_SHARE_MAX))
    failed = ratio > RATIO_MAX or cloth > CLOTH_SHARE_MAX
    print('port-budget: %s' % ('FAILED' if failed else 'OK'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
