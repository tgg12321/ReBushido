#!/usr/bin/env python3
"""The replay theatre and photo mode held to what they must do (ROADMAP 29;
docs/PORT.md, "The replay theatre and photo mode"):

    python3 tools/port_theatre.py [--out FOLDER] [--only CASE...]   Linux (WSL):
        build/port-sdl/linux/rebushido, offscreen on Mesa's llvmpipe
    python tools/port_theatre.py                                    Windows:
        build/port-sdl/windows/rebushido.exe, on a window never shown

The window's program records a fight of two CPUs as a replay (--fight ...
--fight-cpu --replays), then plays it with --theatre-keys, a test's words
the theatre runs as the player's keys would (sdl_theatre.c): `at:F` waits
for the replay's frame F, `hold` pauses, `step` a frame on, `back` a frame
back, `seek:F`, `free`, `orbit:YAW:PITCH`, `move:F:R:U`, `zoom:P`, `hud`,
`time:T`, `still:PATH`, `hash` (a line with the frame, the input and the
state's hash), `photo`, `clip:FROM:TO:GIF`, `plain` (no look at all, the
playback as before the theatre). The cases:

  look      the picture of a frame paused, every picture a look (the frame
            drawn again from the state before it), is the picture of the
            playback with no look, pixel for pixel; in Original and in
            Remastered
  seek      the state's hash at frames 100, 320 and 500 played through is
            the hash at each reached by a seek: on from frame 10, back from
            500 (to a keyframe and on, and to the start and on); every hash
            the replay recorded held all the while (the playback ends on
            none)
  step      paused, a wait leaves the frame where it is; a step goes one
            frame on, `back` one frame back to the hash it had
  still     a still is a PNG of the window's size; the free camera turned
            makes another picture; the HUD hidden changes a small part of
            it; the state's hash the same with all of it
  gif       a clip of frames 200 to 260 in the theatre's view, made by
            another run (--export OUT.gif --export-from --export-to
            --theatre-view) and by the theatre's own `clip:` (which starts
            that run as G does): a GIF89a that this tool decodes whole,
            640 by 360, one picture a frame of the fight, its times the
            fight's; the first's pictures, laid over one another as a
            viewer shows them, near the window's own pixels at their
            blanks (--tap-window)
  photo     a fight of two CPUs paused in photo mode for a while (the free
            camera, the HUD hidden, the time of day turned, stills taken)
            and gone on: the state's hash at inputs after it the same as a
            run with no photo mode; photo mode refused in a replay

It saves a few stills and clips for the eye in --keep (tmp/theatre/ by
default). It needs the disc's image (REBUSHIDO_RETAIL, the .cue at the
checkout's root, or at the main checkout's). Exit 0 if every check passes,
1 if one fails; with no program or image it says so and passes."""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CUE = 'Bushido Blade 2 (USA).cue'
PROGRAMS = {'nt': 'build/port-sdl/windows/rebushido.exe', 'posix': 'build/port-sdl/linux/rebushido'}
FIGHT = ['--fight', 'tatara-shrine', 'kannuki', 'mikado', '--fight-cpu']
RECORD_FRAMES = '4000'
SIZE = '640x360'
CASES = ['look', 'seek', 'step', 'still', 'gif', 'photo']
SECONDS = 900
# the HUD of a versus fight (the wins' two boxes) is a small part of the picture
HUD_MOST = 0.15
# a camera turned changes most of it
TURNED_LEAST = 0.3
GIF_FROM = 200
# a channel's mean difference, of 255, a look's remastered picture may have
REMASTERED_MOST = 2.0
# ... and a GIF's picture from the window's own at its blank (its 256
# colours and dither, its pixels left as they were)
GIF_MOST = 6.0
TAP_EVERY = 6
GIF_TO = 260
BLANK_HUNDREDTHS = 75218 * 100 / 4500000.0


class Report:
    def __init__(self):
        self.failed = 0

    def check(self, ok, what):
        print('  %s %s' % ('ok  ' if ok else 'FAIL', what), flush=True)
        if not ok:
            self.failed += 1
        return ok


def find_image():
    if os.environ.get('REBUSHIDO_RETAIL'):
        return os.environ['REBUSHIDO_RETAIL']
    here = os.path.join(REPO, CUE)
    if os.path.isfile(here):
        return here
    # a worktree of .claude/worktrees/: the main checkout three folders up
    main = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(REPO))), CUE)
    if os.path.basename(os.path.dirname(REPO)) == 'worktrees' and os.path.isfile(main):
        return main
    try:
        common = subprocess.run(['git', 'rev-parse', '--git-common-dir'], cwd=REPO, capture_output=True,
                                text=True).stdout.strip()
        main = os.path.join(os.path.dirname(os.path.abspath(os.path.join(REPO, common))), CUE)
        if os.path.isfile(main):
            return main
    except OSError:
        pass
    return None


def environment():
    env = dict(os.environ)
    env['SDL_AUDIODRIVER'] = 'dummy'
    if os.name != 'nt':
        env['SDL_VIDEODRIVER'] = 'offscreen'
        env['LIBGL_ALWAYS_SOFTWARE'] = '1'
    return env


# ---- pictures read back: a PNG and a GIF, with nothing but the standard library ----

def read_png(path):
    """(width, height, bytes of RGB) of an 8-bit RGB or RGBA PNG."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('%s: not a PNG' % path)
    at = 8
    idat = b''
    width = height = channels = 0
    while at < len(data):
        length, kind = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color = struct.unpack('>IIBB', body[:10])
            if depth != 8 or color not in (2, 6):
                raise ValueError('%s: not 8-bit RGB(A)' % path)
            channels = 3 if color == 2 else 4
        elif kind == b'IDAT':
            idat += body
        at += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    out = bytearray(width * height * 3)
    previous = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = previous[x]
            c = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + a) & 0xFF
            elif kind == 2:
                line[x] = (line[x] + b) & 0xFF
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        for x in range(width):
            out[(y * width + x) * 3:(y * width + x) * 3 + 3] = line[x * channels:x * channels + 3]
        previous = line
    return width, height, bytes(out)


def differing(a, b):
    """How many pixels of two pictures of one size differ at all."""
    if a[0] != b[0] or a[1] != b[1]:
        return None
    x, y = a[2], b[2]
    count = 0
    for i in range(0, len(x), 3):
        if x[i:i + 3] != y[i:i + 3]:
            count += 1
    return count


def mean_difference(a, b):
    """A channel's mean difference, of 255, between two pictures of one size."""
    x, y = a[2], b[2]
    return sum(abs(x[i] - y[i]) for i in range(len(x))) / float(len(x))


def lzw_decode(data, minimum, count):
    """GIF's LZW: `count` indexes from the bytes of the blocks joined."""
    clear = 1 << minimum
    end = clear + 1
    width = minimum + 1
    table = [bytes([i]) for i in range(clear)] + [b'', b'']
    out = bytearray()
    bits = 0
    held = 0
    at = 0
    previous = None
    while True:
        while held < width:
            if at >= len(data):
                raise ValueError('the LZW ran out before its end code')
            bits |= data[at] << held
            held += 8
            at += 1
        code = bits & ((1 << width) - 1)
        bits >>= width
        held -= width
        if code == clear:
            table = table[:clear + 2]
            width = minimum + 1
            previous = None
            continue
        if code == end:
            break
        if previous is None:
            entry = table[code]
        elif code < len(table):
            entry = table[code]
            if len(table) < 4096:
                table.append(previous + entry[:1])
        elif code == len(table):
            entry = previous + previous[:1]
            table.append(entry)
        else:
            raise ValueError('an LZW code past its table: %d of %d' % (code, len(table)))
        out += entry
        previous = entry
        if len(table) == (1 << width) and width < 12:
            width += 1
    if len(out) != count:
        raise ValueError('%d pixels decoded, not %d' % (len(out), count))
    return out


def read_gif(path):
    """(width, height, [(delay in hundredths, indexes, palette)], loops) of a GIF."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:6] not in (b'GIF89a', b'GIF87a'):
        raise ValueError('%s: not a GIF' % path)
    width, height, flags = struct.unpack('<HHB', data[6:11])
    at = 13
    if flags & 0x80:
        at += 3 * (2 << (flags & 7))
    frames = []
    delay = 0
    loops = False
    while at < len(data):
        kind = data[at]
        if kind == 0x3B:
            return width, height, frames, loops
        if kind == 0x21:
            label = data[at + 1]
            at += 2
            blocks = b''
            while data[at] != 0:
                blocks += data[at + 1:at + 1 + data[at]]
                at += 1 + data[at]
            at += 1
            if label == 0xF9:
                delay = struct.unpack('<H', blocks[1:3])[0]
            elif label == 0xFF and blocks.startswith(b'NETSCAPE2.0'):
                loops = True
            continue
        if kind != 0x2C:
            raise ValueError('%s: a block of kind 0x%02X' % (path, kind))
        x, y, w, h, flags = struct.unpack('<HHHHB', data[at + 1:at + 10])
        at += 10
        palette = b''
        if flags & 0x80:
            palette = data[at:at + 3 * (2 << (flags & 7))]
            at += 3 * (2 << (flags & 7))
        minimum = data[at]
        at += 1
        blocks = bytearray()
        while data[at] != 0:
            blocks += data[at + 1:at + 1 + data[at]]
            at += 1 + data[at]
        at += 1
        frames.append((delay, lzw_decode(bytes(blocks), minimum, w * h), palette))
    raise ValueError('%s: no trailer' % path)


# ---- runs ----

class Runner:
    def __init__(self, program, image, out):
        self.program = program
        self.image = image
        self.out = out
        self.env = environment()
        self.config = os.path.join(out, 'none.cfg')
        open(self.config, 'w').close()

    def run(self, name, words):
        log = os.path.join(self.out, name + '.out')
        started = time.time()
        with open(log, 'wb') as f:
            code = subprocess.run([self.program, '--image', self.image, '--config', self.config, '--no-cards',
                                   '--no-log', '--fast', '--hidden', '--no-fps', '--no-vsync', '--run-ahead', '0',
                                   '--frame-rate', 'original'] + words, stdout=f, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, env=self.env, cwd=REPO, timeout=SECONDS).returncode
        with open(log, encoding='utf-8', errors='replace') as f:
            said = f.read()
        print('    %s: exit %d, %.1f s' % (name, code, time.time() - started), flush=True)
        return code, said

    def play(self, name, replay, keys, graphics='original', size=SIZE):
        return self.run(name, ['--graphics', graphics, '--window', size, '--replay', replay, '--theatre-keys', keys])


def hashes(said):
    """{frame: hash} of a run's `hash` lines (and {input: hash})."""
    by_frame = {}
    by_input = {}
    for frame, given, value in re.findall(r'theatre: frame (\d+) input (\d+) hash ([0-9A-F]{16})', said):
        by_frame[int(frame)] = value
        by_input[int(given)] = value
    return by_frame, by_input


def ended_well(said):
    return 'DESYNC' not in said and 'went otherwise' not in said


def case_look(runner, replay, report, keep):
    for graphics in ('original', 'remastered'):
        a = os.path.join(runner.out, 'look-plain-%s.png' % graphics)
        b = os.path.join(runner.out, 'look-look-%s.png' % graphics)
        code_a, said_a = runner.play('look-plain-' + graphics, replay,
                                     'plain at:200 hold wait:3 still:%s quit' % a, graphics)
        code_b, said_b = runner.play('look-look-' + graphics, replay, 'at:200 hold wait:3 still:%s quit' % b,
                                     graphics)
        if not report.check(code_a == 0 and code_b == 0 and os.path.isfile(a) and os.path.isfile(b),
                            '%s: both played and stilled (exit %d, %d)' % (graphics, code_a, code_b)):
            continue
        pa, pb = read_png(a), read_png(b)
        if graphics == 'original':
            report.check(differing(pa, pb) == 0, 'original: the frame paused, drawn again by a look, is the '
                         'playback\'s own picture pixel for pixel (%s of %d differ)'
                         % (differing(pa, pb), pa[0] * pa[1]))
        else:
            # the remastered picture keeps a history of its own across the
            # frames the card draws (the screen's reflections and the
            # occlusion of the frame before), which a look draws again
            mean = mean_difference(pa, pb)
            report.check(mean <= REMASTERED_MOST, 'remastered: the frame paused, drawn again by a look, is the '
                         'playback\'s own picture (a channel\'s mean difference %.2f of 255, %.1f at most)'
                         % (mean, REMASTERED_MOST))
        if graphics == 'remastered' and keep:
            shutil.copy(b, os.path.join(keep, 'frame-200-remastered.png'))


def case_seek(runner, replay, report):
    code_a, said_a = runner.play('seek-through', replay, 'at:99 hold wait:2 hash hold pause at:319 hold wait:2 hash '
                                 'pause at:459 hold wait:2 hash quit')
    through, _ = hashes(said_a)
    code_b, said_b = runner.play('seek-on', replay, 'at:10 hold seek:100 wait:2 hash seek:320 wait:2 hash '
                                 'seek:460 wait:2 hash quit')
    on, _ = hashes(said_b)
    code_c, said_c = runner.play('seek-back', replay, 'at:469 hold wait:2 hash seek:320 wait:2 hash seek:100 wait:2 '
                                 'hash seek:460 wait:2 hash quit')
    back, _ = hashes(said_c)
    report.check(code_a == 0 and code_b == 0 and code_c == 0 and all(ended_well(s) for s in (said_a, said_b, said_c)),
                 'three playbacks, every recorded hash held (exit %d, %d, %d)' % (code_a, code_b, code_c))
    for frame in (100, 320, 460):
        report.check(frame in through and through.get(frame) == on.get(frame) == back.get(frame),
                     'frame %d: played through %s, sought on %s, sought back %s'
                     % (frame, through.get(frame), on.get(frame), back.get(frame)))


def case_step(runner, replay, report):
    code, said = runner.play('step', replay, 'at:199 hold wait:2 hash wait:20 hash step wait:2 hash step wait:2 hash '
                             'back wait:2 hash back wait:2 hash quit')
    lines = re.findall(r'theatre: frame (\d+) input \d+ hash ([0-9A-F]{16}) looks \d+ failed \d+ paused (\d)', said)
    frames = [int(f) for f, _, _ in lines]
    values = [h for _, h, _ in lines]
    report.check(code == 0 and ended_well(said) and len(lines) == 6, 'played and paused (exit %d, %d hashes)'
                 % (code, len(lines)))
    if len(lines) != 6:
        return
    report.check(frames[0] == frames[1] and values[0] == values[1], 'a wait paused stays at frame %d' % frames[0])
    report.check(frames[2] == frames[0] + 1 and frames[3] == frames[0] + 2, 'a step goes a frame on: %s' % frames[:4])
    report.check(frames[4] == frames[2] and values[4] == values[2] and frames[5] == frames[0] and
                 values[5] == values[0], 'back goes a frame back, to the hash it had: %s' % frames)
    report.check(all(p == '1' for _, _, p in lines), 'paused all the while')


def case_still(runner, replay, report, keep):
    plain = os.path.join(runner.out, 'still-game.png')
    turned = os.path.join(runner.out, 'still-free.png')
    bare = os.path.join(runner.out, 'still-free-nohud.png')
    code, said = runner.play('still', replay, 'at:240 hold wait:2 hash still:%s orbit:700:150 zoom:140 wait:2 '
                             'still:%s hud wait:2 still:%s hash quit' % (plain, turned, bare))
    by_frame, _ = hashes(said)
    if not report.check(code == 0 and all(os.path.isfile(p) for p in (plain, turned, bare)),
                        'three stills written (exit %d)' % code):
        return
    width, height = (int(n) for n in SIZE.split('x'))
    pictures = [read_png(p) for p in (plain, turned, bare)]
    report.check(all(p[0] == width and p[1] == height for p in pictures),
                 'each a PNG of the window\'s size, %dx%d' % (width, height))
    area = width * height
    moved = differing(pictures[0], pictures[1])
    report.check(moved is not None and moved > TURNED_LEAST * area,
                 'the free camera turned: another picture (%d of %d pixels differ)' % (moved or 0, area))
    hud = differing(pictures[1], pictures[2])
    report.check(hud is not None and 0 < hud < HUD_MOST * area,
                 'the HUD hidden: a small part of it changed (%d pixels)' % (hud or 0))
    report.check(len(set(by_frame.values())) == 1 and len(re.findall('hash', said)) >= 2,
                 'the state\'s hash the same before and after them')
    if keep:
        for path in (turned, bare):
            shutil.copy(path, os.path.join(keep, os.path.basename(path)))


def composite_gif(width, height, frames):
    """Each picture of a GIF as it is shown (bytes of RGB): its pixels over
    the one before, the transparent index (255 after the first) left as
    it was."""
    shown = bytearray(width * height * 3)
    for k, (_, indexes, palette) in enumerate(frames):
        for i, index in enumerate(indexes):
            if k > 0 and index == 255:
                continue
            shown[i * 3:i * 3 + 3] = palette[index * 3:index * 3 + 3]
        yield bytes(shown)


def read_tap(path):
    """(width, height, bytes of RGB) of a --tap-window file."""
    with open(path, 'rb') as f:
        data = f.read()
    _, w, h, _ = struct.unpack('<4I', data[:16])
    pixels = data[16:]
    rgb = bytearray(w * h * 3)
    rgb[0::3] = pixels[2::4]
    rgb[1::3] = pixels[1::4]
    rgb[2::3] = pixels[0::4]
    return w, h, bytes(rgb)


def halved(picture):
    """A picture taken down by two each way, each pixel the mean of four."""
    w, h, rgb = picture
    out = bytearray((w // 2) * (h // 2) * 3)
    for y in range(h // 2):
        for x in range(w // 2):
            for c in range(3):
                a = ((2 * y) * w + 2 * x) * 3 + c
                b = ((2 * y + 1) * w + 2 * x) * 3 + c
                out[(y * (w // 2) + x) * 3 + c] = (rgb[a] + rgb[a + 3] + rgb[b] + rgb[b + 3]) // 4
    return w // 2, h // 2, bytes(out)


def check_gif(report, path, frames_wanted, what, taps=None):
    """`taps`: (folder of --tap-window's files, the blank the film began
    at), whose pictures the GIF's must be near."""
    try:
        width, height, frames, loops = read_gif(path)
    except (OSError, ValueError) as e:
        report.check(False, '%s: decoded (%s)' % (what, e))
        return
    report.check((width, height) == (640, 360), '%s: 640 by 360 (%dx%d)' % (what, width, height))
    report.check(abs(len(frames) - frames_wanted) <= 2 and loops,
                 '%s: %d pictures, a frame of the fight each (%d wanted), looping' % (what, len(frames), frames_wanted))
    total = sum(d for d, _, _ in frames)
    report.check(abs(total - len(frames) * 2 * BLANK_HUNDREDTHS) <= 1.0,
                 '%s: %d hundredths in all, the fight\'s time' % (what, total))
    report.check(len(set(bytes(p) for _, p, _ in frames)) > len(frames) // 2, '%s: the pictures move' % what)
    if taps is None:
        return
    folder, first = taps
    wanted = {}
    for name in os.listdir(folder):
        blank = int(name[7:13])
        if (blank - first) % 2 == 0 and 0 <= (blank - first) // 2 < len(frames):
            wanted[(blank - first) // 2] = os.path.join(folder, name)
    worst = 0.0
    for k, shown in enumerate(composite_gif(width, height, frames)):
        if k in wanted:
            worst = max(worst, mean_difference((width, height, shown), halved(read_tap(wanted[k]))))
    report.check(wanted and worst <= GIF_MOST, '%s: %d of its pictures, as a viewer shows them, near the window\'s '
                 'own at their blanks (a channel\'s mean difference %.2f of 255 at most, %.1f allowed)'
                 % (what, len(wanted), worst, GIF_MOST))


def case_gif(runner, replay, report, keep):
    gif = os.path.join(runner.out, 'clip.gif')
    tap = os.path.join(runner.out, 'gif-tap')
    os.makedirs(tap)
    view = '1,0,-1200,0,300,700,0,5000,1,-1'
    code, said = runner.run('gif', ['--graphics', 'original', '--window', '1280x720', '--replay', replay,
                                    '--export', gif, '--export-from', str(GIF_FROM), '--export-to', str(GIF_TO),
                                    '--theatre-view', view, '--tap-window', tap, '--tap-every', str(TAP_EVERY)])
    began = re.search(r'film: .*begins at blank (\d+)', said)
    if report.check(code == 0 and os.path.isfile(gif) and began, 'the clip\'s run (exit %d)' % code):
        check_gif(report, gif, (GIF_TO - GIF_FROM + 1), 'the clip of --export', (tap, int(began.group(1))))
        if keep:
            shutil.copy(gif, os.path.join(keep, 'clip-free-camera.gif'))
    # the theatre's own: G's clip, made by another run beside the playback
    code, said = runner.play('gif-key', replay, 'at:150 hold orbit:-500:100 hud wait:2 clip:%d:%d:1 clip-wait quit'
                             % (GIF_FROM, GIF_TO))
    made = re.search(r'film: (\S.*?\.gif), \d+ frames', said)
    if report.check(code == 0 and made is not None and os.path.isfile(made.group(1)),
                    'the theatre\'s clip made by another run beside the playback (exit %d)' % code):
        check_gif(report, made.group(1), (GIF_TO - GIF_FROM + 1), 'the theatre\'s clip')
        if keep:
            shutil.copy(made.group(1), os.path.join(keep, 'clip-theatre.gif'))
        # (made in films/ beside the program, as a player's would be)
        os.remove(made.group(1))


def case_photo(runner, report, keep):
    photo_dir = os.path.join(runner.out, 'photo')
    os.makedirs(photo_dir, exist_ok=True)
    shots = ' '.join('time:%d wait:2 still:%s' % (t, os.path.join(photo_dir, 'photo-%s.png' % n))
                     for t, n in ((1, 'dawn'), (2, 'day'), (3, 'dusk'), (4, 'night')))
    fight = ['--graphics', 'remastered', '--window', SIZE, '--frames', '2600'] + FIGHT
    code_a, said_a = runner.run('photo-none', fight + ['--theatre-keys', 'input:300 hash input:600 hash input:900 '
                                                       'hash'])
    code_b, said_b = runner.run('photo', fight + ['--theatre-keys', 'input:300 hash photo photo-on orbit:600:120 '
                                                  'zoom:130 hud %s wait:30 photo input:600 hash input:900 hash'
                                                  % shots])
    _, none = hashes(said_a)
    _, with_photo = hashes(said_b)
    report.check(code_a == 0 and code_b == 0 and 'photo mode: the fight waits' in said_b and
                 'photo mode left' in said_b, 'a fight with photo mode and one without (exit %d, %d)'
                 % (code_a, code_b))
    for given in (300, 600, 900):
        report.check(given in none and none.get(given) == with_photo.get(given),
                     'input %d: the state\'s hash %s without photo mode, %s with it'
                     % (given, none.get(given), with_photo.get(given)))
    stills = sorted(os.listdir(photo_dir))
    if report.check(len(stills) == 4, 'four stills in photo mode, one a time of day (%d)' % len(stills)):
        pictures = [read_png(os.path.join(photo_dir, s)) for s in stills]
        report.check(all(differing(pictures[0], p) > TURNED_LEAST * p[0] * p[1] for p in pictures[1:]),
                     'each time of day another picture')
        if keep:
            for s in stills:
                shutil.copy(os.path.join(photo_dir, s), os.path.join(keep, s))
    # never in a replay: the theatre is there
    replay_dir = os.path.join(runner.out, 'replays')
    replay = os.path.join(replay_dir, sorted(os.listdir(replay_dir))[0])
    code, said = runner.play('photo-replay', replay, 'at:100 photo wait:2 quit')
    report.check('no photo mode: a replay plays' in said, 'photo mode refused in a replay')


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'theatre'))
    ap.add_argument('--keep', default=os.path.join(REPO, 'tmp', 'theatre'),
                    help='where the stills and clips for the eye go ("" for none)')
    ap.add_argument('--only', nargs='*', choices=CASES)
    args = ap.parse_args(argv)
    program = os.path.join(REPO, PROGRAMS[os.name])
    image = find_image()
    if not os.path.isfile(program) or image is None:
        print('port_theatre: no %s or no disc image: nothing checked' % program)
        return 0
    # the words name files relative to the checkout: a path with a space
    # would be two words
    os.chdir(REPO)
    out = os.path.relpath(os.path.abspath(args.out), REPO)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, 'replays'))
    keep = os.path.abspath(args.keep) if args.keep else None
    if keep:
        os.makedirs(keep, exist_ok=True)
    runner = Runner(program, image, out)
    report = Report()
    print('port_theatre: %s' % os.path.relpath(program, REPO), flush=True)
    code, said = runner.run('record', ['--graphics', 'original', '--window', '320x240', '--frames', RECORD_FRAMES,
                                       '--replays', os.path.join(out, 'replays')] + FIGHT)
    replays = sorted(os.listdir(os.path.join(out, 'replays')))
    if not report.check(code == 0 and len(replays) == 1, 'a fight of two CPUs recorded (exit %d, %d replays)'
                        % (code, len(replays))):
        print(said[-2000:])
        return 1
    replay = os.path.join(out, 'replays', replays[0])
    for case in args.only or CASES:
        print('%s:' % case, flush=True)
        if case == 'look':
            case_look(runner, replay, report, keep)
        elif case == 'seek':
            case_seek(runner, replay, report)
        elif case == 'step':
            case_step(runner, replay, report)
        elif case == 'still':
            case_still(runner, replay, report, keep)
        elif case == 'gif':
            case_gif(runner, replay, report, keep)
        elif case == 'photo':
            case_photo(runner, report, keep)
    print('port_theatre: %s' % ('%d checks failed' % report.failed if report.failed else 'every check passed'))
    return 1 if report.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
