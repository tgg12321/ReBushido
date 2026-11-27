# The CPU

ROADMAP item 27: levels of the CPU that play as a real player would, only
making better decisions. The owner (2026-11-26): "I don't want the AI to
'cheat', ideally they play like real players would, just making better
decisions." This file is first a study of the game's own CPU (what it
decides from, what it reads that a player cannot see, how its level works,
how quickly it answers), then the port's CPU built beside it, and how the
levels were measured.

## 1. How the original CPU decides

All of it is `src/main/cpu.c` (header `include/cpu.h`, tables
`src/main/cpu_data.c`). A fighter the CPU plays is an ordinary fighter with
`Fighter.is_cpu` set; `fighter_ReadPad` (`src/main/fighter_pad.c`) gives it
`cpu_BuildPad`'s pad in place of a pad's. `cpu_Init` sets it up as a fight
begins (`cpu_InitFighters`, `fighter.c`; the port's two on two and its
takeovers call it too, `src/port/duo.c`: `port_DuoTakeOver` sets `is_cpu`
and calls `cpu_Init` at the match's level when an online player of a fight
of four leaves).

Each frame `cpu_BuildPad`:

1. **Looks.** It works out the angle to the opponent (for one of the two
   fighters each frame, turn about), and sets `Fighter.cpu_flags` from the
   two fighters' move kinds (standing, walking, running, down, the
   opponent attacking) and from dice thrown against its level: *eager* (it
   may attack this frame), *impatient*, *alert* (it will answer a threat),
   *sharp* (it will pick the right guard). It counts how long it has been
   off the distance it likes, whether the opponent is above or below, its
   setbacks, and probes the ground round it (`cpu_ProbeGround`, two of
   eight probes a frame).
2. **Threats.** The frame the opponent's move becomes an attack, it
   notices it: which motion, the frame its blade begins to hit
   (`hit_start_frames`), how far it reaches (`cpu_AttackReach`: the
   motion's own reach, the weapon's and the opponent's size), and so
   whether it will land near, mid or not at all. If it has met that motion
   often enough (`cpu_known_attacks`, a count per motion, halved between
   rounds) it *knows* it, and is then alert and sharp. Something thrown, or
   dirt, is a threat while it flies at it.
3. **An opening**: the opponent recovering from an attack that is over and
   has no next move asked for, reloading, hurt, staggered, beaten aside,
   crouching, throwing; an "opportunist" profile takes more.
4. **The buttons**: a pad script that is playing (a few presses and waits:
   guards, steps, the attacks of the motion file's list), or `cpu_Decide`,
   in order of urgency: a threat (the guard pressed the frame before the
   blade, the right button if sharp or known, else the wrong one; a step
   back from one that only just reaches, a side step round one that can be
   stepped round), down or locked or caught, getting to the opponent
   (walking or running, round the stage's obstacles by their chains of
   corners), footwork picked by the profile's weights, an attack that suits
   the distance picked at random by the profile's weights, and standing in
   its stance (turning to face, reloading, striking a fallen opponent,
   changing stance after enough setbacks).

### Its level

`Fighter.cpu_level`, 0 to `CPU_LEVEL_MAX` (0x1000), and a profile per
character (`g_cpu_profiles`: how far into its reach it likes to stand, its
footwork and attack weights, its setback limit). Versus and the port's
`--fight` give `g_fight_config.cpu_levels`, 0x800 by default; versus the
CPU starts at a quarter and rises 3/16 every five opponents to 13/16;
practice rises with the wins; chanbara along its ladder; a story boss
follows the chapter's plan (`g_cpu_story_limits`). The game's own
Difficulty option, Easy (`cpu_IsEasy`), takes the level to 11/16 and stops
it learning attacks and following up. What the level changes, all of it in
`cpu_BuildPad` and `cpu_Decide`:

| Of the level (L of 4096) | |
|---|---|
| eager, a frame | L/8 of 4096 (one frame in eight at the full level) |
| alert, a frame | L of 4096 (every frame at the full level), and always in its first L/8 frames |
| sharp, a frame | L/2 of 4096, and always in its first L/16 frames |
| an attack known after | 17 − L/256 meetings (1 at the full level) |
| a threat guessed wrong though sharp | one in L/256 + 2 |
| an attack picked as if at half its level | one in L/256 + 2 |
| the margin on an attack's distances | 2100 at level 0 down to −400 |
| a blade lock's strike | every 18 − L/256 frames |
| before striking a fallen opponent | 16 − L/256 frames (`cpu_reaction_delay`, its only delay) |
| breaking out of a paired move | L less a sixteenth a wound |

### What it reads that a player cannot see

It never reads the other's pad (only the practice dummy reads the first
pad's Select, which is the player telling the dummy to change stance). But:

1. **The same frame, before it is drawn.** `fight_Update`
   (`src/main/fight.c`) reads the first fighter's pad and moves him, then
   reads the second's and moves him. A CPU in the second place sees the
   first fighter's attack in the frame it begins, before any picture of it
   exists, and can answer in that frame; in the first place it sees the
   second's a frame later. It has no reaction time: from noticing to
   answering is no frames (`cpu_reaction_delay` is used only for a blow at
   a fallen opponent).
2. **The attack's own numbers, at once.** On the attack's first frame it
   knows the exact frame the blade will begin to hit
   (`other->hit_start_frames`, `hit_end_frames`) and presses its guard on
   the frame before it (`other->move_frame + 1 >= cpu_threat_hit_frame`,
   `cpu_Decide`, "1. a threat"). A player learns roughly when a blade
   comes; the CPU knows to the frame from the first.
3. **Which button began the attack.** The right guard is
   `other->attack_kind & 1`, "which of the two attack buttons the attack
   was begun with" (`include/fighter.h`): read the frame the attack
   begins. A player can tell the two apart only from the motion once it
   shows, which takes time.
4. **The move asked for and not begun.** Its test for an opening reads
   `other->queued_move`: whether the opponent, in the middle of a move,
   has already pressed for the next one (a move with `MOVE_QUEUES` keeps
   the command until its last command frame). That is the other's input
   before it shows; the CPU uses it to know whether a recovering attack is
   really open.
5. **Perfect sight.** The exact distance (`g_fight_distance`), the angles,
   every thrown thing's position and the way it moves, the opponent's
   reach in each stance worked out from his motion file, every frame. A
   player sees all of these, but not to the unit and not at once.

Of these, 1 to 4 are what makes it feel unfair: a guard that comes on the
right frame with the right button, for an attack that began a fifth of a
second before.

### How quickly it answers (measured)

`tools/port_cpu_react.py` reads the logs the port writes with
`--cpu-log` (a line a fighter a frame: his move, its motion and frame, the
frame his blade begins to hit, the button that began it, his place, his
wounds, his pad, and the pad script the CPU is playing). An attack is
counted where one fighter's move becomes an attack with the other standing
or walking within 6000; its frame is the frame the attacker's update began
it. The fight runs at 30 frames a second. Two measures were taken: first
the defender's first new press of an attack button or of the direction
away (a guard or a step back, but also any press that happens to fall
there), then, once the log carried it, the frame the CPU *begins* a guard
or a step (its pad script), which is the answer itself. (The game's guard
script is pressed and ended within the frame it is begun, so the second
measure sees only its steps; the first sees its guards.)

The original CPU against itself, 24 fights (`tools/port_cpu_bench.py 0 0
--fights 24 --log`; 188 attacks met):

| | first place | second place |
|---|---|---|
| attacks met, answered within a second | 106, 80 | 82, 67 |
| frames to the answer: least, quartiles | 1; 4 / 8 / 10 | 0; 4 / 7 / 12 |
| answered in the same frame (0), the next (1) | 0, 9 | 5, 5 |
| answered in under a fifth of a second (< 6 frames) | 31 | 29 |
| the attacks' frames to the blade's first | 2 to 20; median 8 | 3 to 19; median 8.5 |
| its guards: frames before the blade's first (quartiles) | 0 / 1 / 1 | 1 / 2 / 2 |

By the second measure, 32 fights: the original's steps out of an
attack's way were begun least 1 frame after the attack began, quartiles
1 / 3 / 5 in the first place and 2 / 3 / 7 in the second; 45 of its 59
steps under a fifth of a second.

So the attacks of this game reach their first frame of cutting a quarter
of a second (eight frames) after they begin; the original CPU sees them in
the frame they begin (the second place in the same frame, the first a
frame later), and puts its guard down one or two frames before the blade,
the right one when it is sharp. A person's reaction to a seen thing is a
fifth to a quarter of a second (six to eight of these frames) before he
can even press: against the quick attacks, a guard of the original's is
one no player could make.

### What it would take to make better decisions without cheating

- **See as a player sees**: what is drawn, a reaction time later; not
  this frame's state. Keep a short record of what was seen each frame and
  decide from the one a reaction time back.
- **Know what a player knows**: the move list (what a motion reaches, when
  its blade comes, which guard meets it), but by sight of the motion, not
  by reading the button; a lesser player misreads more, and less for an
  attack he has met before in the match.
- **Press a pad**: only the buttons a pad has, through the same reader
  as a player's (no pseudo-buttons), with presses and releases as a thumb
  makes them.
- **Better choices, not quicker hands**: stand just off the opponent's
  reach, so his attacks must come from where they can be seen and stepped
  from; strike into what can be seen to be open (an attack that missed and
  has frames to go, a stagger, a reload), with an attack whose own reach
  fits; few blind strikes; learn which of its own attacks land and how
  readily the opponent strikes at a fighter at the edge of his reach, and
  draw him out if he does.
- **Stay in the state**, so a fight against it replays, rolls back and
  plays online: its memory and its dice in the game's state, seeded from
  the game's own as the fight begins.

## 2. The port's CPU

`src/port/cpu.c` (header `include/port_cpu.h`), with `src/port/cpu_run.c`
for the run's own (the seed asked for, the log). The game's CPU stays as it
is and stays the default; the window's `cpu` setting (or `--cpu`) picks one
of four levels instead: **Novice, Adept, Veteran, Master**. With
ReBushido's top menu the select screen asks too, where its opponent is the
CPU: a **CPU** row on Vs. CPU's rule page, and a page of that row alone
before the story's and Slash's grid (`src/port/match.c`, the rule page;
`port_MatchCpuLevel`). The page starts at the setting's level and its
choice holds for the fights that select screen begins; it is the game's
state (`PortMatch.page_cpu`, `cpu_taken`), chosen by the pads, so a replay
and a session (the host's pad) see the same. The original's practice
partner that only stands there is always the game's.

How it is joined to the game, under `RB_PORT`:

- `cpu_Init` ends by calling `port_CpuInit`: if the select screen's page
  or the run asked for a level for that place (`port_MatchCpuLevel`,
  `port_GetCpuLevel`, `src/port/play_switches.c`), the
  port's CPU takes the fighter, its memory set up and its dice seeded from
  the game's `rand()` (and `--cpu-seed`). In a fight of four the game's own
  plays (not yet the port's: its memory is by place, and the duo swaps
  four fighters through two places).
- `fighter_ReadPad` gives such a fighter `port_CpuBuildPad`'s pad in place
  of `cpu_BuildPad`'s.
- `cpu.c` lends it two of its own: the ground probes (`cpu_PortProbeGround`)
  and an attack's reach (`cpu_PortAttackReach`), as a player sees the
  ground and knows the moves.

What it reads, and when:

- **The opponent, a reaction time late.** Each frame it writes down what
  can be seen of the opponent (`PortCpuSeen`: his place, the motion he is
  in and its frame, the motion's reach and blade frames and which button
  meets it, his stance, his wounds) in a ring of 16 frames, and decides
  from the one its reaction time back: 9 frames for the novice down to 6
  for the master (0.3 to 0.2 s), with a frame or so at random, never under
  5. The second place looks a frame further back, so that both places see
  the other's frame only after it was drawn.
- **Itself, now**, as a player knows his own fighter.
- **Never** the other's pad, the move he asked for and has not begun,
  the game's dice, or the opponent's CPU memory.

What it does, each frame (`port_CpuDecide`): what it cannot help (getting
up or rolling away, pushing in a blade lock, struggling when caught); a
threat (an attack seen coming that will reach: a step back if there is
time, otherwise the guard as the blade comes, the button the motion calls
for if it reads the motion right: of 256, `read` plus `read_gain` for every
time it has seen that motion in the match); an opening (an attack seen to
have missed with frames still to go, a stagger, a reload, getting up: a
strike, if one of its attacks fits the distance); the distance it likes
(just past the opponent's reach, by its level's margin and spread, walking
to it and facing him); now and then a strike of its own. Its attacks are
the motion file's list (the moves a player knows, each with its
distances), pressed as the list's pad script; from the adept up, picked by
how each has done in the match. From the veteran up it counts how often
the opponent strikes when it stands within his reach; the master stands at
the edge of a hasty opponent's reach to draw him, and goes in on a patient
one.

Its pad is a pad's: the fighter's own directions and buttons, held,
pressed and released as a thumb does them; the stick buttons as the game's
reader turns them into R1 and the first attack; never Start or Select.

### The state

Everything it keeps (`g_port_cpu`, two `PortCpuMind`s: level, dice, frames,
the ring of what it saw, its script, what it learned) is in a file of the
state group (`config/port_state.txt`), so a load puts it back and a
rollback runs it again alike; the level asked for rides in the gameplay
switches (`port_PlaySwitchBits`, bits 8 to 13), which replays keep in their
setup and a session holds to the host's. `--cpu-seed`, `--cpu-log` and
`--cpu-tune` are the run's own, outside the state.

## 3. Measuring the levels

`tools/port_cpu_bench.py A B --fights N` plays N fights headless, CPU
against CPU, each fight one program run by a plan (the play tests' kind):
on eight open stages, nine fighters with six blades, by the fight's number;
A and B change places every other fight; the fight's seed is `--seed` plus
its number. `--log` keeps each fight's `--cpu-log`; `--tune` tries numbers
of a level (`--cpu-tune`). `--check` holds determinism: one fight twice on
each size of the port, the trace and the log the same byte for byte.

96 fights a pair, 2026-11-26 (a draw is a fight that reached the limit,
24,000 blanks, with both standing; the rate is of the fights decided):

| A against B | A won | B won | draws | A's rate |
|---|---|---|---|---|
| Novice against the original | 33 | 63 | 0 | 34% |
| Adept against the original | 43 | 51 | 2 | 46% |
| Veteran against the original | 58 | 37 | 1 | 61% |
| Master against the original | 64 | 31 | 1 | 67% |
| Adept against Novice | 51 | 45 | 0 | 53% |
| Veteran against Adept | 56 | 37 | 3 | 60% |
| Master against Veteran | 53 | 33 | 10 | 62% |
| Master against Novice | 65 | 24 | 7 | 73% |

Each level beats the one below it, and the veteran and the master beat
the game's own CPU (at its versus level, 0x800) while seeing it a
reaction time late. With 96 fights a rate is good to about five points
either way: the adept's edge over the novice is the thinnest.

How quickly the port's answers, by the same tool (32 fights of the
master against the original): it begins its guards and steps at quartiles
8 / 10 / 10.75 frames after the attack began in the first place and
7 / 8 / 11 in the second (least 7 there: six and the frame the second
place looks further back), and its guards are decided one frame before
the blade (quartiles 1 / 1 / 1), as a player's practised timing; the
novice at 11 / 12 / 13 and 12 / 13 / 14. A few answers in the first place
come sooner than its reaction (least 2): those are answers to what it saw
before the attack (an attack before it, still on its way in what it saw),
which by construction is all it reads.

What decided the numbers (`--tune`, 96 fights each, the master against
the original): backing away while it waits to guard lost (57% with,
67% without); standing off the opponent's farthest attack rather than his
plain ones changed nothing (66%); never striking first, with that, changed nothing
(67%); so the master strikes first rarely and wins by its openings. Early
versions lost most by striking from beyond their attack's reach, and
fixed it by keeping inside it (`slack`) and giving an attack up when the
opponent has stepped out of it before it begins.

The determinism check, `tools/port_cpu_bench.py 4 3 --check`: the fight
(Tatara Shrine, Tatsumi with a katana against Utsusemi with a nodachi, the
master against the veteran) twice on each size, the traces and the
per-frame logs the same byte for byte. And the rollback self-test on such
a fight (`tools/port_rollback.py --test` the bench's plan
`--with=--fight --with=lumberyard ... --with=--cpu --with=master,veteran`):
72 rounds of 8 frames run twice, the same, on both sizes, plainly and
quietly.

## 4. What is left

- The port's CPU in fights of four and for an online takeover: its memory
  is by place, and the duo swaps four fighters through the game's two.
- Getting about: it walks straight to the distance it likes and sidles
  when the way is blocked; the original's route round a stage's obstacles
  and its circling at an edge are not used.
- Dirt, thrown sub-weapons and guns, which it neither uses nor answers
  better than by its guard.
- Levels learned from replays of real players (item 23's files hold every
  input of a fight).
- A person's hands on it: the bench measures CPU against CPU; how each
  level feels to fight is the owner's to say.
