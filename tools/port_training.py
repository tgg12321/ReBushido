#!/usr/bin/env python3
"""The training mode driven on the native port (docs/PORT.md, "Training";
ROADMAP 26; include/port_training.h): plans of the play tests' kind on
both sizes of the port (build/port/, build/port32/), with ReBushido's
main menu (--menu rebushido), whose traces must be the same line for line
and whose checkpoints must hold what training did.

- train: the main menu's Training, its page's Training; both sides'
  fighters and weapons picked by the first pad (the dummy's after its
  own), no rule page, the stage page; a fight of versus with training on.
  With Select held: Cross records the dummy, the first pad driving it (a
  change of stance, a step), Cross again stops; L1 keeps the moment;
  Circle plays the slot back, the dummy (which stands) given the same
  buttons at the same frames of the slot; Triangle freezes (no frame of
  the fight while held), Square steps one frame, Triangle runs on; L2
  slow motion (half the frames in the same time), L2 again; R1 puts the
  moment back (the fight's count of frames where it was kept, the
  recording kept, the plan going on). Then the first fighter strikes
  until the dummy falls: two seconds on both start again, still in the
  fight, with no replay and nobody fallen, the count of restarts one;
  the pause menu's Return to Main Menu leaves, training off.
- train_guard: --training-dummy guard: the first fighter's strikes are
  answered, the dummy given the guard's button (Circle or Cross) as the
  blow comes.
- train_playback: --training-dummy playback: a recording, then the dummy
  plays it round and round by itself, the slot's frames counted again
  from the start each time round.

    python3 tools/port_training.py [--out FOLDER] [--shots FOLDER]
                                   [--only PLAN...] [--rollback]

With --rollback, `train` is run by tools/port_rollback.py instead (make
port-rollback): the state saved, put back and run again every 24 frames
across the whole of it, on both sizes, the moments kept and put back by
training among them.

In WSL, with the programs `make port` builds and the player's disc (as
tools/port_stage_select.py finds them). Exit 0 if every check passes."""
import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))
from port_stage_select import HEAD, Report, fight_presses, run_plan  # noqa: E402
from port_match import RB_ARGS, RB_OPENING, RB_START  # noqa: E402

COMPARE = 'compare = ["screen", "stage", "mode", "winner", "port_training_on", "port_training_frames", ' \
          '"port_training_restarts", "port_training_frozen", "port_training_slow", "port_training_recording", ' \
          '"port_training_playing", "port_training_record_at", "port_training_play_at", "port_training_holds", ' \
          '"port_training_p1_shown", "port_training_p2_shown", "p1_defeated", "p2_defeated"]\n'

# The main menu's third row, Training, and its page's first, Training;
# then the first pad picks its fighter and weapon and the dummy's, each
# under its cursor, and the stage under the stage page's.
MENU = '''"menu+30 DOWN", "menu+60 DOWN", "menu+90 CROSS", "menu+150 CROSS",
    "select+300 CROSS", "select+340 CROSS", "select+520 CROSS", "select+560 CROSS",
    "stages+40 CROSS"'''

START = RB_START + '''
[[checkpoint]]
name = "select"
when = "menu_page == MENU_PAGE_SELECT"
within = 1500
shot = false
expect = ["port_training_on == 1", "port_match_rule_page == 0"]

[[checkpoint]]
name = "stages"
when = "port_on_stages == 1"
within = 1500
shot = false
expect = ["port_training_on == 1"]

[[checkpoint]]
name = "fight"
when = "screen == SCREEN_FIGHT_UPDATE && winner == FIGHT_NOBODY"
at_frame = true
within = 3000
expect = ["mode == 4", "port_training_on == 1", "port_match_rule == 0", "p2_cpu == 0",
          "port_training_frames <= 2"]
'''

# Select held, and a button under it: one of training's presses.
def chord(at, button):
    """Select held from `at` (a checkpoint and its offset, blanks or `f`
    frames), and `button` pressed a little after."""
    point, _, offset = at.partition('+')
    frames = offset.endswith('f')
    number = int(offset.rstrip('f'))
    unit = 'f' if frames else ''
    return '"%s+%d%s SELECT hold %s", "%s+%d%s %s"' % (
        point, number, unit, '6f' if frames else '14', point, number + (1 if frames else 3), unit, button)


SQUARE = 0x80
CIRCLE = 0x20

TRAIN = 'about = "Training: record, keep the moment, play back, freeze, step, slow, put back, a kill, leave ' \
    '(tools/port_training.py)"\n' + HEAD.replace('limit = 9000', 'limit = 16000') + '''input = [%(opening)s,
    %(menu)s,
    %(record)s, "fight+40f SQUARE", "fight+60f LEFT hold 10f", "fight+80f SQUARE", %(record_stop)s,
    %(keep)s,
    %(play)s,
    %(freeze)s,
    %(step)s,
    %(run_on)s,
    %(slow)s,
    %(slow_off)s,
    %(put_back)s,
    %(strikes)s,
    "fresh+20 START", "fresh+50 DOWN", "fresh+70 DOWN", "fresh+100 CROSS",
]
''' + COMPARE + START + '''
[[checkpoint]]
name = "rec"
when = "port_training_recording == 1"
at_frame = true
within = 300
shot = false
expect = ["port_training_record_at <= 1"]

[[checkpoint]]
name = "rec_square"
when = "port_training_recording == 1 && port_training_p2_shown == %(square)d"
at_frame = true
within = 300
shot = false
expect = ["port_training_p1_shown == 0"]

[[checkpoint]]
name = "recorded"
when = "port_training_recording == 0"
at_frame = true
within = 600
shot = false
expect = ["port_training_record_at > 60"]

[[checkpoint]]
name = "kept"
frames = 30
shot = false

[[checkpoint]]
name = "playing"
when = "port_training_playing == 1"
at_frame = true
within = 600
shot = false
expect = ["port_training_played_slot == 0", "port_training_dummy == 2"]

[[checkpoint]]
name = "play_square"
when = "port_training_playing == 1 && port_training_p2_shown == %(square)d"
at_frame = true
within = 600
shot = false

[[checkpoint]]
name = "played"
when = "port_training_playing == 0"
at_frame = true
within = 600
shot = false
expect = ["port_training_dummy == 0"]

[[checkpoint]]
name = "frozen"
when = "port_training_frozen == 1 && port_training_holding == 1"
within = 600

[[checkpoint]]
name = "frozen2"
after = 40
shot = false
expect = ["port_training_holding == 1"]

[[checkpoint]]
name = "stepped"
after = 40
shot = false
expect = ["port_training_frozen == 1", "port_training_holding == 1"]

[[checkpoint]]
name = "running"
when = "port_training_frozen == 0 && port_training_holding == 0"
within = 200
shot = false

[[checkpoint]]
name = "normal"
after = 120
shot = false

[[checkpoint]]
name = "slow"
when = "port_training_slow == 2"
within = 200
shot = false

[[checkpoint]]
name = "slow2"
after = 120
shot = false

[[checkpoint]]
name = "unslow"
when = "port_training_slow == 0"
within = 300
shot = false

[[checkpoint]]
name = "back"
when = "port_training_frames < 200"
at_frame = true
expect = ["port_training_recording == 0", "port_training_playing == 0", "port_training_slow == 0",
          "port_training_frozen == 0"]

[[checkpoint]]
name = "fell"
when = "p2_defeated != 0"
within = 9000

[[checkpoint]]
name = "restart"
when = "port_training_restarts == 1 && screen == SCREEN_FIGHT_UPDATE"
within = 400
expect = ["mode == 4", "port_training_on == 1"]

[[checkpoint]]
name = "fresh"
frames = 30
expect = ["screen == SCREEN_FIGHT_UPDATE", "p1_defeated == 0", "p2_defeated == 0", "port_training_restarts == 1",
          "winner == FIGHT_NOBODY"]

[[checkpoint]]
name = "top"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_TOP"
within = 1500
expect = ["port_training_on == 0"]
'''

GUARD = 'about = "Training: the dummy guards (tools/port_training.py)"\n' + HEAD + '''input = [%(opening)s,
    %(menu)s,
    %(strikes_fight)s,
]
''' + COMPARE + START + '''
[[checkpoint]]
name = "guard"
when = "port_training_p2_shown != 0"
at_frame = true
within = 3000
expect = ["port_training_dummy == 1", "port_training_p2_shown == 32 || port_training_p2_shown == 64"]
'''

PLAYBACK = 'about = "Training: the dummy plays its slot back round and round (tools/port_training.py)"\n' + \
    HEAD + '''input = [%(opening)s,
    %(menu)s,
    %(record)s, "fight+40f SQUARE", %(record_stop)s,
]
''' + COMPARE + START + '''
[[checkpoint]]
name = "recorded"
when = "port_training_recording == 0 && port_training_record_at > 0"
at_frame = true
within = 600
shot = false
expect = ["port_training_record_at == 30"]

[[checkpoint]]
name = "round1"
when = "port_training_playing == 1 && port_training_play_at == 29"
at_frame = true
within = 300
shot = false
expect = ["port_training_dummy == 2"]

[[checkpoint]]
name = "round2"
when = "port_training_playing == 1 && port_training_play_at == 2"
at_frame = true
within = 300
shot = false
expect = ["port_training_dummy == 2", "port_training_played_slot == 0"]

[[checkpoint]]
name = "round2_square"
when = "port_training_p2_shown == %(square)d"
at_frame = true
within = 300
shot = false
'''


def values():
    return dict(opening=', '.join(RB_OPENING), menu=MENU, square=SQUARE,
                record=chord('fight+30f', 'CROSS'), record_stop=chord('fight+100f', 'CROSS'),
                keep=chord('fight+130f', 'L1'), play=chord('fight+160f', 'CIRCLE'),
                freeze=chord('played+20f', 'TRIANGLE'), step=chord('frozen2+4f', 'SQUARE'),
                run_on=chord('stepped+4f', 'TRIANGLE'), slow=chord('normal+4f', 'L2'),
                slow_off=chord('slow2+4f', 'L2'), put_back=chord('unslow+10f', 'R1'),
                strikes=fight_presses().replace('"fight+', '"back+'), strikes_fight=fight_presses())


PLANS = [
    ('train', TRAIN, RB_ARGS + ['--match', 'single']),
    ('train_guard', GUARD, RB_ARGS + ['--match', 'single', '--training-dummy', 'guard']),
    ('train_playback', PLAYBACK.replace('%(record_stop)s', '%(record_stop_short)s'),
     RB_ARGS + ['--match', 'single', '--training-dummy', 'playback']),
]


def reached(out, name, bits, trace):
    """Each checkpoint's values, by name, as the trace has them."""
    with open(os.path.join(out, '%s-plan%d.txt' % (name, bits)), encoding='utf-8') as f:
        order = [line.split()[1] for line in f if line.startswith('var ')]
    found = {}
    for line in trace.splitlines():
        word = line.split()
        if len(word) > 3 and word[0] == 'cp' and word[1] != '(failure)':
            found[word[1]] = dict(zip(order, (int(v) for v in word[3:])))
    return found


def check_train(report, cps):
    """What `train`'s checkpoints hold beside one another."""
    need = ['rec_square', 'play_square', 'frozen', 'frozen2', 'stepped', 'running', 'normal', 'slow', 'slow2',
            'kept', 'unslow', 'back']
    if not all(n in cps for n in need):
        report.check(False, 'train: every checkpoint for the checks beside one another reached')
        return
    frames = lambda n: cps[n]['port_training_frames']  # noqa: E731
    report.check(cps['play_square']['port_training_play_at'] == cps['rec_square']['port_training_record_at'],
                 'train: the dummy was given Square at the frame of the slot it was recorded at (%d, %d)'
                 % (cps['play_square']['port_training_play_at'], cps['rec_square']['port_training_record_at']))
    report.check(frames('frozen2') == frames('frozen'),
                 'train: no frame of the fight while frozen (%d, %d)' % (frames('frozen'), frames('frozen2')))
    report.check(cps['frozen2']['port_training_holds'] > cps['frozen']['port_training_holds'] + 10,
                 'train: the frozen fight held frame after frame (%d, %d)'
                 % (cps['frozen']['port_training_holds'], cps['frozen2']['port_training_holds']))
    report.check(frames('stepped') == frames('frozen2') + 1,
                 'train: a step is one frame (%d, %d)' % (frames('frozen2'), frames('stepped')))
    normal = frames('normal') - frames('running')
    slow = frames('slow2') - frames('slow')
    report.check(normal >= 55 and abs(2 * slow - normal) <= 4,
                 'train: slow motion has half the frames in the same time (%d, then %d in 120 blanks)'
                 % (normal, slow))
    report.check(abs(frames('back') - frames('kept')) <= 30 and frames('back') < frames('unslow'),
                 'train: the moment put back takes the fight back to where it was kept (%d kept near %d, '
                 'back to %d from %d)' % (frames('kept') - 30, frames('kept'), frames('back'), frames('unslow')))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'training'))
    parser.add_argument('--shots', default=None)
    parser.add_argument('--only', nargs='*', help='the plans to run (train, train_guard, train_playback)')
    parser.add_argument('--rollback', action='store_true',
                        help='train through tools/port_rollback.py instead, rolled back every 24 frames')
    args = parser.parse_args(argv[1:])
    import playtest
    report = Report()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    vals = dict(values(), record_stop_short=chord('fight+60f', 'CROSS'))
    plans = PLANS
    if args.only:
        plans = [plan for plan in plans if plan[0] in args.only]
    if args.rollback:
        code = 0
        for name, text, extra in plans[:1]:
            toml = os.path.join(out, '%s-rollback.toml' % name)
            with open(toml, 'w', encoding='utf-8', newline='\n') as f:
                text = re.sub(r'^shot = .*\n', '', text % vals, flags=re.M)
                f.write(re.sub(r'^(name = .*)$', r'\1\nshot = false', text, flags=re.M))
            code |= subprocess.run([sys.executable, os.path.join(REPO, 'tools', 'port_rollback.py'), '--test', toml,
                                    '--out', os.path.join(out, 'rollback-' + name)]
                                   + ['--with=' + arg for arg in extra], cwd=REPO).returncode
        return code
    for name, text, extra in plans:
        traces = run_plan(playtest, report, out, name, text % vals, extra, args.shots)
        if traces is None:
            return 1
        if name == 'train':
            for bits in (64, 32):
                check_train(report, reached(out, name, bits, traces[bits]))
    return 1 if report.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
