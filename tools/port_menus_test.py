#!/usr/bin/env python3
"""The window's pages in the game's look, held to their sound (docs/PORT.md,
"The window's pages in the game's look"; docs/PLAYING.md, "The settings
menu", "Playing online").

    python tools/port_menus_test.py [--image CUE] [--only CASE,...]

On Windows (Windows' own Python), after `make port-sdl-windows`; from WSL
or Linux it runs the Linux window's program (`make port-sdl`). Each case
runs the window's program as a test runs it (no window shown, no sound:
SDL's dummy drivers, --hidden, --fast), with ReBushido's main menu
(--menu rebushido) and a plan of presses, keeps its sound (--wav) and
reads what it did from its log, its output and the WAV:

  menu     the main menu: Singleplayer and back (the game's own confirm
           and cancel), the cursor down to Online Play (the game's own
           cursor), and Online Play: the online pages open beside the
           game, which runs on under them. On them down, down, up, Host a
           room, back, back; then Options: the settings menu, beside the
           game too, down, down, up, a group, back, close. Then Online
           Play again, Join by address, Host: the meeting, where the game
           is stopped at its boundary and its sound goes on
           (port_RunStoppedSound); given up after a while, and the message
           read. Held: the pages and the menu were open beside the game
           for the blanks their presses took; every press played the
           game's own sound -- the cursor's, the confirm's, the cancel's,
           the same voices the game's menus asked for with the same
           programs and tones -- and only those; the music never stopped
           (no silence of 0.1 s from the main menu's music on to the
           meeting's end) and was as loud under the pages as before them;
           the meeting's sound was made while the game stood still.
  pause    a versus fight paused, and the settings menu opened over the
           pause (beside the game, as at the main menu): down, down, up, a
           group, back, close; the fight resumed and fought to a kill.
           Held: its presses sounded, played by the game as it took its
           input (port_TakeInput); the fight was kept as a replay with the
           sounds in it (SOND), and `--replay-check` plays it back with
           every hash the same.

The disc image: --image, REBUSHIDO_RETAIL, or the .cue at the repository's
root. Exits 1 if a case fails. The emulator is not used.
"""
import argparse
import glob
import os
import re
import struct
import subprocess
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playtest  # noqa: E402

WORK = os.path.join(ROOT, 'build', 'tests', 'port_menus')
RATE = 44100
PER_BLANK = 737 + 341 / 2500.0
RUN_SECONDS = 600
# a sample this small either side is silence; this long of it is a gap
QUIET = 8
GAP_SECONDS = 0.1
NO_CONTROLLERS = {'SDL_JOYSTICK_HIDAPI': '0', 'SDL_JOYSTICK_RAWINPUT': '0', 'SDL_XINPUT_ENABLED': '0',
                  'SDL_JOYSTICK_WGI': '0', 'SDL_DIRECTINPUT_ENABLED': '0', 'SDL_JOYSTICK_LINUX_CLASSIC': '0',
                  'SDL_JOYSTICK_DEVICE': ''}
CASES = ['menu', 'pause']

TOP = '''
[[checkpoint]]
name = "movie"
when = "screen == SCREEN_MOVIE"
within = 3000
shot = false

[[checkpoint]]
name = "menu"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_TOP && screen_frames >= 60"
within = 4000
shot = false
'''
FIGHT = '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 3000
shot = false

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 6000
shot = false

[[checkpoint]]
name = "kill"
when = "winner != FIGHT_NOBODY"
at_frame = true
within = 20000
shot = false
'''
BOOT = ["movie+300 START", "movie+600 CROSS", "movie+900 START"]
# the game's own sounds first: Singleplayer and back (confirm, cancel),
# the cursor down three rows to Online Play (the cursor), and Online Play
# (confirm); after the pages, down to Options; after the menu, up to
# Online Play again
MENU_PRESSES = ["menu+20 CROSS", "menu+50 TRIANGLE", "menu+80 DOWN", "menu+100 DOWN", "menu+120 DOWN",
                "menu+160 CROSS", "menu+300 DOWN", "menu+340 CROSS", "menu+480 UP", "menu+520 CROSS"]
# the game's own presses' sounds: (press's offset, sound)
GAME_SOUNDS = [(20, 'confirm'), (50, 'cancel'), (80, 'cursor'), (100, 'cursor'), (120, 'cursor'), (160, 'confirm')]
# on the pages: down, down, up (the cursor), Host a room (confirm), back,
# back (cancel); then, opened again, down to Join by address and Host:
# the meeting, given up after 300 of its turns, and the message read
ONLINE_KEYS = ('wait:30 down down up accept wait:10 back wait:10 back '
               'down down down accept wait:5 down down accept wait:300 back wait:20 back')
PAGES_SOUNDS = {'cursor': 3, 'confirm': 1, 'cancel': 2}
MENU_KEYS = 'wait:30 down down up accept wait:10 back wait:10 close'
MENU_SOUNDS = {'cursor': 3, 'confirm': 1, 'cancel': 2}
# the pause: versus, both take what is under the cursor; the fight of
# port_game (tests/play/port_game.toml); Start at its 120th frame, the menu
# over the pause (--menu-at), and Escape (the pause's Triangle) to go on,
# counted in blanks: the pause's frames are none of the plan's
VERSUS = ["menu+60 DOWN", "menu+100 CROSS", "select+280 CROSS", "select+320 P2 CROSS", "select+580 CROSS",
          "select+620 P2 CROSS", "select+1180 CROSS"]
STRIKES = ["fight+50f RIGHT hold 20f", "fight+50f P2 LEFT hold 10f"] + \
    ["fight+%df %s" % (72 + 30 * i, 'CIRCLE' if i % 2 == 0 else 'CROSS') for i in range(28)] + \
    ["fight+%df P2 CIRCLE" % (65 + 35 * i) for i in range(24)]
PAUSE_AT_FRAME = 120
MENU_AFTER_PAUSE = 120   # blanks after the pause began
RESUME_AFTER_PAUSE = 300


def program_path():
    if os.name == 'nt':
        return os.path.join(ROOT, 'build', 'port-sdl', 'windows', 'rebushido.exe')
    return os.path.join(ROOT, 'build', 'port-sdl', 'linux', 'rebushido')


def write_plan(program, folder, presses, checks, stay):
    toml = os.path.join(folder, 'plan.toml')
    with open(toml, 'w', encoding='utf-8', newline='\n') as f:
        f.write('about = "port_menus"\ndiscs = ["port"]\nlimit = 40000\nseconds = %d\n' % RUN_SECONDS)
        f.write('input = [\n%s\n]\n' % ',\n'.join('    "%s"' % p for p in presses))
        f.write(checks)
        f.write('\n[[checkpoint]]\nname = "stay"\nafter = %d\nshot = false\n' % stay)
    playtest.PORT_PROGRAM = program
    playtest.PORT_SYMBOLS = program + '.sym'
    test = playtest.Test(toml)
    disc = playtest.Disc('port')
    test.set_discs([disc])
    _, lines = test.plan(disc, playtest.header_names(), 1)
    plan = os.path.join(folder, 'plan.txt')
    with open(plan, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return plan


def run_window(program, image, folder, plan, more, settings=''):
    """The window's program on the plan: (exit code, its log, its output)."""
    cfg = os.path.join(folder, 'rebushido.cfg')
    with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
        # no lobby server: nothing of a case's is asked of the project's own
        f.write('fullscreen = off\nupdates = off\nfps = off\nnet_listen = local\nnet_portmap = off\n'
                'net_lobby = 127.0.0.1:9\n' + settings)
    log = os.path.join(folder, 'run.log')
    cmd = [program, '--hidden', '--image', image, '--config', cfg, '--windowed', '--plan', plan,
           '--menu', 'rebushido', '--fast', '--log', log, '--wav', os.path.join(folder, 'sound.wav'),
           '--card1', os.path.join(folder, 'card1.mcd'), '--card2', os.path.join(folder, 'card2.mcd'),
           '--quick-loading', 'on'] + more
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', **NO_CONTROLLERS)
    with open(os.path.join(folder, 'out.txt'), 'wb') as out, open(os.path.join(folder, 'err.txt'), 'wb') as err:
        try:
            code = subprocess.call(cmd, stdout=out, stderr=err, stdin=subprocess.DEVNULL, env=env,
                                   timeout=RUN_SECONDS)
        except subprocess.TimeoutExpired:
            code = None
    text = open(log, encoding='utf-8', errors='replace').read() if os.path.exists(log) else ''
    output = open(os.path.join(folder, 'out.txt'), encoding='utf-8', errors='replace').read()
    return code, text, output


def checkpoints(output):
    found = {}
    for line in output.splitlines():
        m = re.match(r'cp (\S+) (\d+) ', line)
        if m:
            found.setdefault(m.group(1), int(m.group(2)))
    return found


def asks(output):
    """{blank: set of (vab, program, tone, note)} of the voices the game's
    sound library was asked for (the run's `sound ask` lines)."""
    found = {}
    for line in output.splitlines():
        m = re.match(r'sound ask (\d+) voice \d+ vab (-?\d+) program (-?\d+) tone (-?\d+) note (-?\d+)', line)
        if m:
            found.setdefault(int(m.group(1)), set()).add(tuple(int(m.group(i)) for i in range(2, 6)))
    return found


def sound_near(found, blank, reach=8):
    """The voices asked for at the first blank from `blank` on that has
    any, within `reach`."""
    for b in range(blank, blank + reach):
        if b in found:
            return frozenset(found[b])
    return None


def count_sounds(found, first, last, signatures):
    """How many times each sound was asked for in blanks first..last, and
    the blanks of any other."""
    counts = {name: 0 for name in signatures}
    others = []
    for blank in sorted(found):
        if first <= blank <= last:
            names = [n for n, s in signatures.items() if frozenset(found[blank]) == s]
            if names:
                counts[names[0]] += 1
            else:
                others.append(blank)
    return counts, others


def read_wav(path):
    w = wave.open(path, 'rb')
    frames = w.getnframes()
    data = struct.unpack('<%dh' % (frames * 2), w.readframes(frames))
    return data[0::2], data[1::2]


def longest_quiet(left, right, first, last):
    """The longest run of silence, in samples, in first..last."""
    longest = run = 0
    for i in range(max(0, first), min(len(left), last)):
        if abs(left[i]) < QUIET and abs(right[i]) < QUIET:
            run += 1
            longest = max(longest, run)
        else:
            run = 0
    return longest


def rms(left, right, first, last):
    first = max(0, first)
    last = min(len(left), last)
    if last <= first:
        return 0.0
    total = sum(left[i] * left[i] + right[i] * right[i] for i in range(first, last))
    return (total / (2.0 * (last - first))) ** 0.5


def case_menu(program, image, folder):
    problems = []
    said = []
    plan = write_plan(program, folder, BOOT + MENU_PRESSES, TOP, 2400)
    code, log, output = run_window(program, image, folder, plan,
                                   ['--online-keys', ONLINE_KEYS, '--menu-keys', MENU_KEYS],
                                   # (above the ports of port-net and its
                                   # Windows cases, which run beside it)
                                   'net_port = %d\n' % (58000 + os.getpid() % 1000))
    if code != 0:
        problems.append('the program ended %s' % code)
    cps = checkpoints(output)
    if 'menu' not in cps:
        return problems + ['the main menu was not reached'], said
    menu = cps['menu']
    found = asks(output)
    signatures = {}
    for offset, name in GAME_SOUNDS:
        heard = sound_near(found, menu + offset)
        if heard is None:
            problems.append('the game played no sound for its own %s at the main menu (blank %d)'
                            % (name, menu + offset))
        elif name in signatures and signatures[name] != heard:
            problems.append('the game\'s own %s sounded two ways' % name)
        else:
            signatures[name] = heard
    if len(signatures) != 3:
        return problems, said
    said.append('the game\'s own sounds: %s' % ', '.join('%s %s' % (n, sorted(s)) for n, s in signatures.items()))
    opened = [int(b) for b in re.findall(r'online pages: open beside the game, which runs on under them; blank (\d+)',
                                         log)]
    closed = [int(b) for b in re.findall(r'online pages: closed after \d+ ms at blank (\d+)', log)]
    m_open = re.search(r'settings menu: open \(\d+ times now\) beside the game, which runs on under it; blank (\d+)',
                       log)
    m_close = re.search(r'settings menu: closed after \d+ ms; the game goes on from blank (\d+)', log)
    if not opened or not closed or not m_open or not m_close:
        return problems + ['the pages or the menu were not open beside the game (the log: %d opened, %d closed, '
                           'menu %s)' % (len(opened), len(closed), bool(m_open))], said
    spans = [('the online pages', opened[0], closed[0], PAGES_SOUNDS),
             ('the settings menu', int(m_open.group(1)), int(m_close.group(1)), MENU_SOUNDS)]
    for what, first, last, wanted in spans:
        if last - first < 40:
            problems.append('%s were open for %d blanks only: the game did not run beside them' % (what, last - first))
        # (the game's own confirm of the row that opened them sounds as they
        # open: their own presses begin after a wait)
        counts, others = count_sounds(found, first + 8, last + 6, signatures)
        if counts != wanted or others:
            problems.append('%s played %s and %d other sounds (blanks %s); wanted %s'
                            % (what, counts, len(others), others[:5], wanted))
        else:
            said.append('%s, beside the game for blanks %d to %d: %s, the game\'s own' % (what, first, last, counts))
    # the music: from the main menu's on to the end, never a gap, and under
    # the pages as loud as before them
    left, right = read_wav(os.path.join(folder, 'sound.wav'))
    start = int((menu + 30) * PER_BLANK)
    end = len(left)
    gap = longest_quiet(left, right, start, end)
    if gap >= GAP_SECONDS * RATE:
        problems.append('the music stopped: %.2f s of silence after the main menu came up' % (gap / RATE))
    before = rms(left, right, start, int((menu + 150) * PER_BLANK))
    for what, first, last, _ in spans:
        under = rms(left, right, int(first * PER_BLANK), int(last * PER_BLANK))
        if before <= 0 or under < 0.3 * before:
            problems.append('the music under %s was %.0f RMS, before them %.0f' % (what, under, before))
        else:
            said.append('the music under %s: %.0f RMS, before them %.0f' % (what, under, before))
    said.append('the longest silence from the main menu\'s music on: %.3f s' % (gap / RATE))
    # the meeting: sound made with the game stopped
    if 'online pages: open beside the game' not in log.split('netplay:')[-1] and len(opened) < 2:
        problems.append('the online pages did not open beside the game again after the meeting')
    last_blank = max(closed + [int(m_close.group(1))])
    extra = len(left) - int(last_blank * PER_BLANK)
    m_end = re.search(r'done (\d+)', output)
    if m_end:
        extra = len(left) - int(int(m_end.group(1)) * PER_BLANK)
    if 'could not play online' not in log and 'netplay:' not in log:
        problems.append('the meeting never began')
    elif extra < 200 * PER_BLANK:
        problems.append('the meeting made %.0f blanks of sound beyond the game\'s blanks: wanted 200 and more'
                        % (extra / PER_BLANK))
    else:
        said.append('the meeting: %.0f blanks of sound made with the game stopped at its boundary'
                    % (extra / PER_BLANK))
    return problems, said


def case_pause(program, image, folder):
    problems = []
    said = []
    presses = BOOT + VERSUS + STRIKES + ["fight+%df START" % PAUSE_AT_FRAME]
    # a first run for the blank the pause begins at: to the fight, and on
    # into the pause
    plan = write_plan(program, folder, presses, TOP + FIGHT.split('[[checkpoint]]\nname = "kill"')[0], 600)
    code, log, output = run_window(program, image, folder, plan, [])
    m = re.search(r'game: the pause menu is up; blank (\d+)', log)
    if code != 0 or m is None:
        return ['the first run ended %s, the pause %s' % (code, 'up' if m else 'never up')], said
    paused = int(m.group(1))
    plan = write_plan(program, folder, presses, TOP + FIGHT, 900)
    replays = os.path.join(folder, 'replays')
    os.makedirs(replays, exist_ok=True)
    for old in glob.glob(os.path.join(replays, '*')):
        os.remove(old)
    code, log, output = run_window(program, image, folder, plan,
                                   ['--menu-at', str(paused + MENU_AFTER_PAUSE), '--menu-keys', MENU_KEYS,
                                    '--escape-at', str(paused + RESUME_AFTER_PAUSE), '--replays', replays])
    if code != 0:
        problems.append('the program ended %s' % code)
    m_open = re.search(r'settings menu: open \(\d+ times now\) beside the game, which runs on under it; blank (\d+)',
                       log)
    m_close = re.search(r'settings menu: closed after \d+ ms; the game goes on from blank (\d+)', log)
    if not m_open or not m_close:
        return problems + ['the settings menu did not open beside the paused game'], said
    found = asks(output)
    first, last = int(m_open.group(1)), int(m_close.group(1))
    heard = [b for b in found if first + 8 <= b <= last + 6]
    if len(heard) != sum(MENU_SOUNDS.values()):
        problems.append('the menu over the pause played %d sounds (blanks %s); wanted %d'
                        % (len(heard), heard, sum(MENU_SOUNDS.values())))
    else:
        said.append('the menu over the pause, blanks %d to %d: %d sounds' % (first, last, len(heard)))
    if 'kill' not in checkpoints(output):
        problems.append('the fight did not go on to its kill after the pause')
    # the fight with the pause in it, which keeps the menu's sounds (the
    # next is the second round's, as far as the run went)
    kept = [path for path in sorted(glob.glob(os.path.join(replays, '*.rbr')))
            if chunk_of(open(path, 'rb').read(), b'SOND') is not None]
    if not kept:
        return problems + ['no replay of the fight keeps the menu\'s sounds (SOND)'], said
    data = open(kept[0], 'rb').read()
    said.append('the replay %s keeps %d menu sounds (SOND)'
                % (os.path.basename(kept[0]), struct.unpack('<I', chunk_of(data, b'SOND')[:4])[0]))
    code = replay_check(program, image, os.path.join(folder, 'check'), kept[0])
    if code != 0:
        problems.append('--replay-check of %s ended %s' % (os.path.basename(kept[0]), code))
    else:
        said.append('it plays back with every hash the same')
    # and without them it would not: the same file with SOND taken out
    # differs from its recorded hashes, which hold the sound chip's state
    bare = os.path.join(folder, 'without-sond.rbr')
    with open(bare, 'wb') as f:
        f.write(without_chunk(data, b'SOND'))
    code = replay_check(program, image, os.path.join(folder, 'check-bare'), bare)
    if code == 0:
        problems.append('the replay without its SOND plays back the same: the menu\'s sounds were not held')
    else:
        said.append('without SOND the playback differs (ended %s): the sounds are what keeps it the same' % code)
    return problems, said


def chunk_of(data, tag):
    """The bytes of a replay's chunk `tag`, or None (docs/PORT.md,
    "Replays": a tag, a size and the bytes, after the 12 of the head)."""
    at = 12
    while at + 8 <= len(data):
        size = struct.unpack('<I', data[at + 4:at + 8])[0]
        if data[at:at + 4] == tag:
            return data[at + 8:at + 8 + size]
        at += 8 + size
    return None


def without_chunk(data, tag):
    out = [data[:12]]
    at = 12
    while at + 8 <= len(data):
        size = struct.unpack('<I', data[at + 4:at + 8])[0]
        if data[at:at + 4] != tag:
            out.append(data[at:at + 8 + size])
        at += 8 + size
    return b''.join(out)


def replay_check(program, image, folder, path):
    os.makedirs(folder, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', **NO_CONTROLLERS)
    with open(os.path.join(folder, 'out.txt'), 'wb') as out, open(os.path.join(folder, 'err.txt'), 'wb') as err:
        return subprocess.call([program, '--hidden', '--image', image, '--no-cards', '--fast', '--log',
                                os.path.join(folder, 'run.log'), '--replay-check', path],
                               stdout=out, stderr=err, stdin=subprocess.DEVNULL, env=env, timeout=RUN_SECONDS)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--image', default=playtest.RETAIL_CUE)
    parser.add_argument('--only', default=None)
    parser.add_argument('--program', default=program_path())
    args = parser.parse_args()
    if not os.path.exists(args.program):
        print('port_menus: %s is not built' % args.program)
        return 1
    if not os.path.exists(args.image):
        print('port_menus: no disc image (%s)' % args.image)
        return 1
    names = args.only.split(',') if args.only else CASES
    failed = 0
    for name in names:
        folder = os.path.join(WORK, name)
        os.makedirs(folder, exist_ok=True)
        problems, said = {'menu': case_menu, 'pause': case_pause}[name](args.program, args.image, folder)
        for line in said:
            print('port_menus: %s: %s' % (name, line), flush=True)
        if problems:
            failed += 1
            for line in problems:
                print('port_menus: %s: FAIL: %s' % (name, line), flush=True)
        else:
            print('port_menus: %s: ok' % name, flush=True)
    print('port_menus: %d of %d cases passed' % (len(names) - failed, len(names)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
