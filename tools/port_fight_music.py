#!/usr/bin/env python3
"""A fight's music chosen on the stage select, driven on the native port
(include/port_fight_music.h; docs/PORT.md, "A fight's music"). Versus the
CPU with --stage-select (and --unlock-all, for the fighter
port_stage_select.py's plan picks) goes to the stage pages and fights on
the second page's fourth place (Abandoned Station, whose versus piece is
track 3, bb2_m1-3), five ways:

- `original` (nothing said, as the console plays): no music, as the
  original has none in versus the CPU;
- `stage` (--fight-music stage, the window's default): the stage's piece;
- `piece`: R1 six times on the page, the row at bb2_m1-5, which nothing
  of the original asks for;
- `none`: L1 once, the row at None: no music, the ambient sounds;
- `staff`: L1 twice, the row at the staff roll's piece (bb2_m2-4).

Both sizes of the port must print the same trace of each plan, each
checkpoint's values must be what the presses chose, and where a piece
plays the drive must have streamed its sectors of XA sound over the
fight, which with none it does not: the run's count of them against the
`original` plan's (the same run up to the fight). The sound is kept
(--wav, FOLDER/PLAN.wav) and the loudness of the fight's last seconds
said.

    python3 tools/port_fight_music.py [--out FOLDER] [--shots FOLDER]
                                      [--only PLAN...]

In WSL, with the programs `make port` builds and the player's disc.
Exit 0 if every check passes, 1 if one fails."""
import argparse
import math
import os
import struct
import sys
import wave

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import port_stage_select  # noqa: E402

# the page's presses for each plan, and the row, the track and what the
# fight's start heard (port_fight_music.h)
PLANS = [
    ('original', [], [], 0, -1, 0),
    ('stage', ['--fight-music', 'stage'], [], 0, 3, 1),
    ('piece', ['--fight-music', 'stage'], ['R1'] * 6, 6, 5, 1),
    ('none', ['--fight-music', 'stage'], ['L1'], 14, -1, 2),
    ('staff', ['--fight-music', 'stage'], ['L1', 'L1'], 13, 12, 1),
]
# the stage: the second page's fourth place
STAGE = 25

TOML = 'about = "A fight\'s music from the stage select (tools/port_fight_music.py)"\n' + \
    port_stage_select.HEAD + '''input = [%(opening)s,
    "menu+30 DOWN", "menu+90 CROSS",
    "select+260 RIGHT", "select+300 CROSS", "select+520 CROSS",
    "stages+20 DOWN", "stages+40 RIGHT", %(music)s"stages+%(cross)d CROSS",
]
compare = ["screen", "stage", "mode", "port_music_row", "port_music_heard", "port_music_track"]
''' + port_stage_select.START + '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
expect = ["port_page_used == 1", "port_music_row == %(start)d"]

[[checkpoint]]
name = "chosen"
after = %(chosen)d
expect = ["port_on_stages == 1", "port_music_row == %(row)d"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE"
at_frame = true
within = 3000
expect = ["mode == 1", "stage == %(stage)d", "port_music_row == %(row)d",
          "port_music_heard == %(heard)d", "port_music_track == %(track)d"]

[[checkpoint]]
name = "listen"
frames = 600
expect = ["stage == %(stage)d", "port_music_track == %(track)d"]
'''

# the fight's ten seconds stream about 370 sectors of XA sound where a
# piece plays (18.75 of a channel's a second at double speed, from the
# fight's loading on); a run with none streams what `original` does, the
# menus' music, to a few sectors
STREAMED_MORE = 200
STREAMED_SAME = 30
LISTEN_SECONDS = 6


def xa_sectors(trace):
    """The run's count of XA sectors given the sound chip (the trace's
    `sound xa_sectors N`)."""
    for line in trace.splitlines():
        word = line.split()
        if len(word) == 3 and word[0] == 'sound' and word[1] == 'xa_sectors':
            return int(word[2])
    return None


def rms_of_end(path, seconds):
    with wave.open(path, 'rb') as w:
        rate = w.getframerate()
        frames = w.getnframes()
        take = min(frames, rate * seconds)
        w.setpos(frames - take)
        data = w.readframes(take)
    count = len(data) // 2
    values = struct.unpack('<%dh' % count, data[:count * 2])
    return math.sqrt(sum(v * v for v in values) / max(count, 1))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'fight-music'))
    parser.add_argument('--shots', default=None)
    parser.add_argument('--only', nargs='*', help='the plans to run (%s)' % ', '.join(p[0] for p in PLANS))
    args = parser.parse_args(argv[1:])
    import playtest
    report = port_stage_select.Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    streamed = {}
    for name, extra, presses, row, track, heard in PLANS:
        if args.only and name not in args.only and name != 'original':
            continue
        music = ''.join('"stages+%d %s", ' % (60 + 20 * i, press) for i, press in enumerate(presses))
        cross = 60 + 20 * len(presses) + 40
        values = {'opening': ', '.join(port_stage_select.OPENING), 'music': music, 'cross': cross,
                  'start': 0, 'chosen': cross - 10, 'row': row,
                  'track': track, 'heard': heard, 'stage': STAGE}
        wav = os.path.join(out, name + '.wav')
        run_extra = ['--mods', 'off', '--stage-select', '--unlock-all', '--wav', os.path.relpath(wav, REPO)] + extra
        traces = port_stage_select.run_plan(playtest, report, out, name, TOML % values, run_extra, args.shots)
        if traces is None:
            return 1
        streamed[name] = xa_sectors(traces[64])
        if not report.check(streamed[name] is not None, '%s: the trace says its XA sectors' % name):
            continue
        more = streamed[name] - streamed['original']
        if name != 'original':
            if track >= 0:
                report.check(more >= STREAMED_MORE, '%s: the fight streamed its piece: %d sectors of XA more '
                             'than with no music' % (name, more))
            else:
                report.check(abs(more) <= STREAMED_SAME, '%s: the fight streamed nothing: %d sectors of XA '
                             'against no music\'s' % (name, more))
        if report.check(os.path.exists(wav), '%s: the sound was kept (%s)' % (name, wav)):
            print('%s: the fight\'s last %d s: RMS %.0f' % (name, LISTEN_SECONDS, rms_of_end(wav, LISTEN_SECONDS)))
    return 1 if report.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
