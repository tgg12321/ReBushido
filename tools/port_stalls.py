#!/usr/bin/env python3
"""The stall hunter: the window's build on the real card through long
sessions, every blank over the budget named (docs/PORT.md, "Stalls").

    python tools/port_stalls.py                    every session, a report
    python tools/port_stalls.py --session game --session fight-lost-bridge
    python tools/port_stalls.py --reuse            the runs already made
    python tools/port_stalls.py --list             the sessions

From Windows' Python, on the PC with the card, as tools/port_bench.py
runs it (--hidden, SDL's dummy sound, settings of its own, no cards, the
five mods, 2560 by 1440, unpaced), each session with --profile (the
parts' times of every blank and the card's passes) and --sample (the
window's thread stopped a thousand times a second and its stack kept:
src/port/window/sampler.c). The sessions: port_game's plan through the
menus, a fight, its replay and the menus after, with the settings menu
opened, a report made and the picture switched on the way; and a fight
on each of a number of stages with other fighters, from --fight, long
enough for a round and what follows it.

A blank's time is every part's but the wait for the clock or the
display (the swap is kept: the card waited for is the swap's). Each
blank over 6.94 ms (144 a second) is a hitch; over --stall-ms (25) a
stall, which is listed with its screen, its parts, the card's passes and
the functions the samples found it in: the innermost function of the
program on each sample's stack, and the module under it (the driver,
the system) where that is not the program. A blank of a load (a screen
`(set-up)` or `fight (loading)`, the blanks a screen changes on, and
the first second of a fight) is listed apart: a load may take its time.

Each session's runs are in build/stalls/<session>/; the report is
build/stalls/report.txt.
"""
import argparse
import bisect
import collections
import os
import re
import shutil
import statistics
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

import port_bench  # noqa: E402
import port_budget  # noqa: E402

OUT_DIR = os.path.join(REPO, 'build', 'stalls')
BUDGET_MS = 1000.0 / 144
STALL_MS = 25.0
# A fight's first blanks are its load's end: the first second.
FIGHT_SETTLE_BLANKS = 60
RUN_SECONDS = 420

# name: (how it starts, blanks, run_ahead, more arguments)
SESSIONS = collections.OrderedDict([
    # port_game's plan, Kannuki against Mikado on Tatara Shrine, through
    # the fight, the menu after and back to the main menu; the settings
    # menu (F1) opened in the second fight and closed, a report made (F9)
    # and a picture kept (F12)
    ('game', ('plan', 5600, 1, ['--report-at', '3400', '--shot-at', '4300', '--menu-at', '4100',
                                '--menu-keys', 'down down down up up up close'])),
    ('fight-lost-bridge', ('fight lost-bridge tsubame red-shadow', 3600, 1, [])),
    ('fight-castle-rooftop', ('fight castle-rooftop utsusemi kannuki:nodachi', 3600, 2, [])),
    ('fight-bamboo-thicket', ('fight bamboo-thicket mikado sazanka', 3600, 0, [])),
    ('fight-kabuki-stage', ('fight kabuki-stage nightstalker tatsumi', 3600, 1, [])),
    ('fight-ferry-dock', ('fight ferry-dock jo hongou', 3600, 1, [])),
    # the picture switched (F2) and back, again and again, in a fight
    ('fight-toggles', ('fight snake-cliff kaun gengoro', 3000, 1, ['--toggle-at', '400'])),
])


def run(name, image, mods, plan, program):
    start, blanks, run_ahead, more = SESSIONS[name]
    folder = os.path.join(OUT_DIR, name)
    os.makedirs(folder, exist_ok=True)
    config = os.path.join(folder, 'rebushido.cfg')
    with open(config, 'w', encoding='utf-8', newline='\n') as f:
        f.write(port_budget.SETTINGS.format(run_ahead=run_ahead))
    cmd = [program, '--hidden', '--image', image, '--config', config, '--mods', mods, '--out', folder,
           '--no-cards', '--fast', '--run-ahead', str(run_ahead), '--window', port_bench.WINDOW,
           '--frames', str(blanks), '--profile', os.path.join(folder, 'profile.txt'),
           '--sample', os.path.join(folder, 'samples.txt'), '--log', os.path.join(folder, 'log.txt')]
    if start == 'plan':
        cmd += ['--plan', plan]
    else:
        cmd += ['--fight'] + start.split()[1:]
    cmd += more
    env = dict(os.environ, SDL_AUDIODRIVER='dummy')
    env.pop('SDL_VIDEODRIVER', None)
    began = time.time()
    with open(os.path.join(folder, 'output.txt'), 'w', encoding='utf-8', errors='replace') as out:
        process = subprocess.Popen(cmd, env=env, stdout=out, stderr=out, stdin=subprocess.DEVNULL)
        try:
            process.wait(timeout=RUN_SECONDS)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print('port-stalls: %s did not end in %d s' % (name, RUN_SECONDS))
    if process.returncode not in (0, 1):
        print('port-stalls: %s ended with %d (%s)' % (name, process.returncode, folder))
    return folder, time.time() - began


def screens(log):
    """[(first blank, screen name)] from the log."""
    out = []
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.search(r'game: screen (\d+), ([^;]*?)(?: \(as the program starts.*\))?; blank (\d+)', line)
            if m:
                out.append((int(m.group(3)), m.group(2)))
    return out


def screen_at(table, blank):
    i = bisect.bisect_right([b for b, _ in table], blank) - 1
    return table[i][1] if i >= 0 else '?'


def is_load(table, blank):
    """A blank of a load (the module's doc)."""
    starts = [b for b, _ in table]
    i = bisect.bisect_right(starts, blank) - 1
    if i < 0:
        return True
    first, name = table[i]
    if '(set-up)' in name or 'loading' in name or name in ('movie',):
        return True
    if blank - first <= 2:
        return True
    if name == 'fight' and blank - first < FIGHT_SETTLE_BLANKS:
        return True
    return False


def read_profile(path):
    parts, gpu_parts, rows, gpu = [], [], {}, {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            words = line.split()
            if not words:
                continue
            if words[0] == 'parts':
                parts = words[1:]
            elif words[0] == 'gpu_parts':
                gpu_parts = words[1:]
            elif words[0] == 'b':
                rows[int(words[1])] = (int(words[2]), [int(w) / 1000.0 for w in words[3:]])
            elif words[0] == 'g':
                gpu[int(words[1])] = [int(w) / 1000.0 for w in words[2:]]
    return parts, gpu_parts, rows, gpu


class Symbols:
    """Names for the samples' addresses: the program's own from its
    symbols (nm), the others as module+offset."""

    def __init__(self, program):
        self.program = os.path.normcase(os.path.abspath(program))
        self.addresses, self.names = [], []
        nm = None
        for tool in (['x86_64-w64-mingw32-nm'], ['wsl', 'x86_64-w64-mingw32-nm']):
            try:
                path = program if tool[0] != 'wsl' else to_wsl(program)
                nm = subprocess.run(tool + ['-n', '-C', path], capture_output=True, text=True,
                                    timeout=120).stdout
                if nm:
                    break
            except (OSError, subprocess.SubprocessError):
                continue
        found = {}
        for line in (nm or '').splitlines():
            words = line.split(None, 2)
            if len(words) == 3 and words[1] in 'tT' and not words[2].startswith('.'):
                found[int(words[0], 16)] = words[2]
        # the program's own list of its functions (port_sdl_build.py):
        # `sym ADDRESS f SIZE NAME`, the game's among them
        if os.path.exists(program + '.sym'):
            with open(program + '.sym', encoding='utf-8', errors='replace') as f:
                for line in f:
                    words = line.split()
                    if len(words) == 5 and words[0] == 'sym' and words[2] == 'f':
                        found[int(words[1], 16)] = words[4]
        for address in sorted(found):
            self.addresses.append(address)
            self.names.append(found[address])
        self.modules = []

    def module(self, pc):
        for base, size, path in self.modules:
            if base <= pc < base + size:
                return base, path
        return None, '?'

    def own(self, pc):
        base, path = self.module(pc)
        return path is not None and os.path.normcase(os.path.abspath(path)) == self.program

    def name(self, pc):
        base, path = self.module(pc)
        if self.own(pc):
            i = bisect.bisect_right(self.addresses, pc) - 1
            return self.names[i] if i >= 0 else '?'
        return os.path.basename(path)

    def outside(self, pc):
        """An address outside the program: module+offset."""
        base, path = self.module(pc)
        if base is None:
            return '%x' % pc
        return '%s+%x' % (os.path.basename(path), pc - base)


# The system's own: a wait, a call into the kernel.
SYSTEM_MODULES = ('ntdll.dll', 'kernelbase.dll', 'kernel32.dll', 'win32u.dll', 'user32.dll', 'gdi32.dll')


def to_wsl(path):
    path = os.path.abspath(path)
    return '/mnt/' + path[0].lower() + path[2:].replace('\\', '/')


def read_samples(path, symbols):
    """{blank: [(part, [pc...])]}, {blank: [(thread, [pc...])]}"""
    by_blank = collections.defaultdict(list)
    others = collections.defaultdict(list)
    parts = []
    if not os.path.exists(path):
        return by_blank, others, parts
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            if line.startswith('m '):
                _, base, size, where = line.rstrip('\n').split(' ', 3)
                symbols.modules.append((int(base, 16), int(size, 16), where))
            elif line.startswith('parts '):
                parts = line.split()[1:]
            elif line.startswith('s '):
                words = line.split()
                by_blank[int(words[1])].append((int(words[2]), [int(w, 16) for w in words[3:]]))
            elif line.startswith('t '):
                words = line.split()
                others[int(words[1])].append((int(words[2]), [int(w, 16) for w in words[3:]]))
    return by_blank, others, parts


def blame(samples, symbols, sample_parts, most=5):
    """The functions a blank's samples were in: the program's innermost
    function on each stack, with the module under it where that is not
    the program; and their parts."""
    counts = collections.Counter()
    for part, stack in samples:
        if not stack:
            continue
        leaf = []
        for pc in stack:
            if symbols.own(pc):
                break
            if not leaf or leaf[-1] != symbols.name(pc):
                leaf.append(symbols.name(pc))
        leaf = '>'.join(reversed(leaf))
        mine = next((symbols.name(pc) for pc in stack if symbols.own(pc)), '?')
        # the caller of the program's innermost, for context
        chain = [symbols.name(pc) for pc in stack if symbols.own(pc)][:3]
        label = ' < '.join(chain) if chain else mine
        if leaf:
            label = '[%s] %s' % (leaf, label)
        counts[label] += 1
    total = sum(counts.values())
    return ['%d%% %s' % (round(100.0 * n / total), what) for what, n in counts.most_common(most)], total


def busy_others(samples, symbols, most=4):
    """The other threads' samples while the window's waited: those not
    themselves in a wait of the system, by where they were (the first
    frames that are not the system's)."""
    counts = collections.Counter()
    for thread, stack in samples:
        if not stack or symbols.name(stack[0]).lower() in SYSTEM_MODULES:
            continue
        frames = []
        for pc in stack:
            text = symbols.name(pc) if symbols.own(pc) else symbols.outside(pc)
            if text.split('+')[0].lower() in SYSTEM_MODULES:
                continue
            frames.append(text)
            if len(frames) == 3:
                break
        counts['thread %d: %s' % (thread, ' < '.join(frames))] += 1
    return ['%d x %s' % (n, what) for what, n in counts.most_common(most)]


def analyse(name, folder, symbols, stall_ms):
    log = os.path.join(folder, 'log.txt')
    profile = os.path.join(folder, 'profile.txt')
    if not os.path.exists(profile):
        return {'name': name, 'missing': True}
    table = screens(log) if os.path.exists(log) else []
    parts, gpu_parts, rows, gpu = read_profile(profile)
    symbols.modules = []
    samples, others, sample_parts = read_samples(os.path.join(folder, 'samples.txt'), symbols)
    work_parts = [i for i, p in enumerate(parts) if p != 'wait']

    def work(values):
        return sum(values[i] for i in work_parts)

    out = {'name': name, 'blanks': len(rows), 'stalls': [], 'load_stalls': [], 'hitches': 0, 'play_blanks': 0}
    play = []
    fight = []
    for b in sorted(rows):
        new, values = rows[b]
        w = work(values)
        load = is_load(table, b)
        screen = screen_at(table, b)
        if not load:
            play.append(w)
            out['play_blanks'] += 1
            if w > BUDGET_MS:
                out['hitches'] += 1
            if screen == 'fight':
                fight.append(w)
        if w > stall_ms:
            top = sorted(((values[i], parts[i]) for i in work_parts), reverse=True)[:4]
            card = gpu.get(b)
            card_top = sorted(zip(card, gpu_parts), reverse=True)[:3] if card else []
            why, n = blame(samples.get(b, []), symbols, sample_parts)
            entry = {'blank': b, 'ms': round(w, 1), 'screen': screen,
                     'parts': ' '.join('%s %.1f' % (p, v) for v, p in top),
                     'card': ' '.join('%s %.1f' % (p, v) for v, p in card_top if v >= 0.5),
                     'samples': n, 'functions': why,
                     'others': busy_others(others.get(b, []), symbols)}
            (out['load_stalls'] if load else out['stalls']).append(entry)
    out['play_p99'] = port_bench.centile(play, 0.99) if play else 0.0
    out['play_worst'] = max(play) if play else 0.0
    out['fight_p50'] = statistics.median(fight) if fight else 0.0
    out['fight_p99'] = port_bench.centile(fight, 0.99) if fight else 0.0
    out['fight_worst'] = max(fight) if fight else 0.0
    out['screens'] = sorted(set(n for _, n in table))
    return out


def report(result, write):
    if result.get('missing'):
        write('%s: no profile' % result['name'])
        return
    write('%s: %d blanks, %d outside loads; hitches (over %.2f ms) %d; play 99%% %.1f ms, worst %.1f; '
          'the fight median %.1f, 99%% %.1f, worst %.1f'
          % (result['name'], result['blanks'], result['play_blanks'], BUDGET_MS, result['hitches'],
             result['play_p99'], result['play_worst'], result['fight_p50'], result['fight_p99'],
             result['fight_worst']))
    for kind, title in (('stalls', 'stalls outside loads'), ('load_stalls', 'stalls in loads')):
        if not result[kind]:
            continue
        write('  %s:' % title)
        for s in result[kind][:40]:
            write('    blank %d (%s) %.1f ms: %s%s' % (s['blank'], s['screen'], s['ms'], s['parts'],
                                                     '; card ' + s['card'] if s['card'] else ''))
            for f in s['functions']:
                write('        %s' % f)
            for f in s.get('others', []):
                write('        other %s' % f)
        if len(result[kind]) > 40:
            write('    ... and %d more' % (len(result[kind]) - 40))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--session', action='append', choices=list(SESSIONS), help='a session (again: more)')
    ap.add_argument('--reuse', action='store_true', help='read the runs already in build/stalls/')
    ap.add_argument('--list', action='store_true', help='the sessions, and stop')
    ap.add_argument('--program', default=port_bench.PROGRAM, help="the window's build to run")
    ap.add_argument('--stall-ms', type=float, default=STALL_MS, help='a blank over this is a stall (25)')
    ap.add_argument('--out', help='where the runs go (build/stalls/)')
    args = ap.parse_args()
    global OUT_DIR
    OUT_DIR = os.path.abspath(args.out or OUT_DIR)
    if args.list:
        for name, (start, blanks, ra, more) in SESSIONS.items():
            print('%-22s %s, %d blanks, run_ahead %d %s' % (name, start, blanks, ra, ' '.join(more)))
        return 0
    program = os.path.abspath(args.program)
    names = args.session or list(SESSIONS)
    main_repo = port_budget.main_checkout()
    image = port_budget.find(os.environ.get('REBUSHIDO_RETAIL'),
                             os.path.join(REPO, 'Bushido Blade 2 (USA).cue'),
                             os.path.join(main_repo, 'Bushido Blade 2 (USA).cue'))
    if not args.reuse:
        card = port_bench.card_name()
        for what, there in (('a graphics card (Windows, NVIDIA, AMD or Intel)', card),
                            ("the window's Windows build", os.path.exists(program) or None),
                            ('the disc image (REBUSHIDO_RETAIL)', image)):
            if there is None:
                print('port-stalls: skipped: there is no %s' % what)
                return 0
        port_bench.PROGRAM = program
        os.makedirs(port_bench.OUT_DIR, exist_ok=True)
        mods, missing = port_bench.make_mods(main_repo)
        if missing:
            print('port-stalls: without the mods %s' % ', '.join(missing))
        plan = os.path.join(OUT_DIR, 'plan.txt')
        os.makedirs(OUT_DIR, exist_ok=True)
        port_bench.FRAMES = SESSIONS['game'][1]
        port_bench.write_plan(plan)
        print('port-stalls: %s, %s' % (card, program), flush=True)
    symbols = Symbols(program)
    lines = []

    def write(text):
        print(text, flush=True)
        lines.append(text)

    for name in names:
        if args.reuse:
            folder = os.path.join(OUT_DIR, name)
        else:
            folder, seconds = run(name, image, mods, plan, program)
            print('port-stalls: %s ran %.0f s' % (name, seconds), flush=True)
        report(analyse(name, folder, symbols, args.stall_ms), write)
    with open(os.path.join(OUT_DIR, 'report.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
