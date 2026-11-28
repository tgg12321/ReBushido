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
  the game's `rand()` (and `--cpu-seed`). Its memory is each fighter's,
  not each place's: in a fight of four (`src/port/duo.c`) the duo passes
  the four fighters through the game's two places, and the port's CPU
  keeps a `PortCpuMind` for each of the four (`port_DuoInSlot` says who
  is in a place), so it plays any of them: the CPU's partners, the CPU's
  team, and a player's fighter it takes over online. A fighter stepped
  after the one he faces (the second place; of the four, the higher
  number) looks a frame further back, as the second place does in a fight
  of two.
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
to it and facing him; where one of the stage's obstacles lies across the
way to where it saw him, round it by its corners, the way the game's CPU
plans: `cpu_PortFindObstacle`, `cpu_PortPlanDetour` and
`cpu_PortSteerTo`, lent by `src/main/cpu.c`, the obstacles a player
sees); now and then a strike of its own. Its attacks are
the motion file's list (the moves a player knows, each with its
distances), pressed as the list's pad script; from the adept up, picked by
how each has done in the match. From the veteran up it counts how often
the opponent strikes when it stands within his reach; the master stands at
the edge of a hasty opponent's reach to draw him, and goes in on a patient
one.

### Committing (2026-11-28)

Before this, two of the port's CPUs could stand off each other for the
whole fight: each stood just past the other's reach and struck first
only inside its own, so where neither came in nothing happened (a
master's first strike was 2 in 1,024 a frame). Measured (below,
"Fights to the clock"), a fight of two masters went past three minutes
in 23 of 48, and the tournament's judges (PORT.md, "A local tournament")
decided 11 of its 34 matches of two CPUs. The logs
(`--cpu-log`) showed three ways it stalled: both standing still, each in
the place it liked, for minutes; each standing "in its reach" by the
motion's numbers (`attack_reach`) where no attack of its list was meant
for that distance, so every strike it tried found none; and two
fighters on two levels of a terraced stage, the lower one's way blocked
by the step and the upper one's by the drop, sidling side to side for
ever. Now it commits as a player does, from what it can see:

- **A patience that runs out.** It counts the frames since anything
  happened, as it saw it (`PortCpuMind.quiet`, `port_CpuNoteQuiet`): a
  wound of either begins the count again; an attack begun, his or its
  own, halves it. Past its level's `patience` it presses, all it will
  after `ramp` frames more (`port_CpuPressure`, of 256): ahead on wounds
  in half the hurry, behind more for each wound it has more than it saw
  him to have (64 each); and more as the fight goes on, ahead or not
  (from a minute, all by two and a half: an opponent who will not come to
  it, a person who stands there, is gone to in the end; an online
  takeover's master ahead on wounds against two players who had stopped
  pressing stood off them for good before this). Pressing, the place it likes
  comes in from past his reach to within its own, and its chance to
  strike first rises by its level's `aggression` (of 1,024 a frame),
  walking in as well as standing.
- **Its reach is its attacks' reach.** It counts itself in reach where an
  attack of its list is meant for the distance (`port_CpuListReach`),
  not where the motion's numbers say.
- **Approach and pokes.** Pressing, from just past its reach (1,500) it
  may lunge: a quick step in and a blow (`lunge`). A lesser level,
  pressing, swings from as far as it misjudges (its `slack`).
- **Meeting him.** As he is seen to walk, run or step into its reach, it
  meets him with a blow (`meet`, of 256, once each time he comes in):
  the adept now and then, the master nearly always. A player who walks
  in on a master pays for it.
- **Feints**, from the veteran up: pressing but not all out, standing
  just past his reach, a step into it and straight back out
  (`feint`), to draw a blow that misses, which it then punishes (an
  opening, as before).
- **Heights.** On its way to him, where the ground ahead is a drop or a
  step (`CPU_FOUND_DROP`, `CPU_FOUND_BLOCKED_STEP`), it walks on: down
  off it, or up it with R1 at its edge (`fighter_FindLedge`), as the
  game's CPU climbs.

The levels: the novice's patience runs out soonest (40 frames, 1.3 s;
all out after 4 s more) and it swings carelessly, as a beginner does; the
adept's after 3.3 s; the veteran's after 8 s, the master's after 10 s,
each all out after 8 s more: the better player waits for an opening
longer and makes one (a feint, meeting him as he comes in) before he
forces one. The master, pressing, stays just outside his reach (200
past it, `outside`) and goes in by lunges rather than walking into a
veteran's blow (57% over the veteran of 192 fights with it, 55% of 288
without). Its reaction times are as before; nothing it does is
quicker or sees more.

Its pad is a pad's: the fighter's own directions and buttons, held,
pressed and released as a thumb does them; the stick buttons as the game's
reader turns them into R1 and the first attack; never Start or Select.

### The state

Everything it keeps (`g_port_cpu`, four `PortCpuMind`s, one a fighter: level, dice, frames,
the ring of what it saw, its script, what it learned) is in a file of the
state group (`config/port_state.txt`), so a load puts it back and a
rollback runs it again alike; the level asked for rides in the gameplay
switches (`port_PlaySwitchBits`, bits 8 to 13), which replays keep in their
setup and a session holds to the host's. The game takes the level in when
a mode begins and when a select screen loads (`PortMatch.cpu_setting`,
`src/port/match.c`), before the fight loads: a replay that keeps no
lead-in (a run begun in a fight, say) starts at the boundary before the
fight's load, where the switches are not its own, and before 2026-11-27
it set the fight's CPU up from the playing program's setting and went
otherwise from its first hash.

**Online, a player who leaves** (NETPLAY.md, section 19): his fighter is
taken over at the level of the **host's `cpu` setting**, the one the
session's welcome carries among the gameplay switches and every machine
holds (or, in a session whose Battle type page had the CPU row, the
host's choice there, which is the game's state); where it is Original,
the game's own CPU takes him, as before. That level, not the leaver's own setting nor each machine's, so
that every machine sets up the same CPU at the same frame and the
rollback and the hashes stay alike; no new word was needed in the
session. `--cpu-seed`, `--cpu-log` and
`--cpu-tune` are the run's own, outside the state. But `--cpu-log`
writes its lines from the game's step, on the game's stack, which is
console memory and hashed: a run with it hashes otherwise than one
without, and its replays play back alike only
with `--cpu-log` too (found 2026-11-27; `tools/port_duo.py`'s `levels`
records without it, `levels_log` logs).

## 3. Measuring the levels

`tools/port_cpu_bench.py A B --fights N` plays N fights headless, CPU
against CPU, each fight one program run by a plan (the play tests' kind):
on eight open stages, nine fighters with six blades, by the fight's number;
A and B change places every other fight; the fight's seed is `--seed` plus
its number. `--log` keeps each fight's `--cpu-log`; `--tune` tries numbers
of a level (`--cpu-tune`). `--check` holds determinism: one fight twice on
each size of the port, the trace and the log the same byte for byte.

288 fights a pair, 2026-11-27 (`--fights 288`; a draw is a fight that
reached the limit, 24,000 blanks, with both standing; the rate is of the
fights decided):

| A against B | A won | B won | draws | A's rate |
|---|---|---|---|---|
| Novice against the original | 111 | 175 | 2 | 39% |
| Adept against the original | 133 | 150 | 5 | 47% |
| Veteran against the original | 174 | 112 | 2 | 61% |
| Master against the original | 184 | 101 | 3 | 65% |
| Adept against Novice | 190 | 94 | 4 | 67% |
| Veteran against Adept | 164 | 116 | 8 | 59% |
| Master against Veteran | 160 | 99 | 29 | 62% |
| Veteran against Novice | 203 | 84 | 1 | 71% |
| Master against Adept | 160 | 83 | 45 | 66% |
| Master against Novice | 209 | 59 | 20 | 78% |

With some 280 fights decided a rate is good to about six points either
way (two standard errors): each level beats the one below it by more than
that (the thinnest, the veteran over the adept, 59%: 53% to 65%), each
beats every level below it by more the further down it is, and the
veteran and the master beat the game's own CPU (at its versus level,
0x800) while seeing it a reaction time late; the original stands between
the adept and the veteran.

The first measure (96 fights a pair, 2026-11-26) had the adept over the
novice at 53%, within the noise. What separated them (`--tune`, 192
fights each, the adept against the novice): the novice's reaction from 9
to 11 frames (11 to 14 with its jitter, 0.37 to 0.47 s, a beginner's
time to make out an attack and answer it), 67%; the adept not learning
which of its attacks land, 55%; the adept keeping inside its attacks'
reach (`slack` 0), 54%; stepping back from more attacks (`evade` 190),
55%: none of the adept's choices alone moved it outside the noise, the
novice's slower eye did. Before, at 96 fights: novice against the
original 34%, adept 46%, veteran 61%, master 67%; the veteran over the
adept 60%, the master over the veteran 62%, over the novice 73%.

Walking round the stage's obstacles (below, 2026-11-27) came after that
table; the same fights again with it (288 a pair, the same seeds): of
the novice against the original 17 of the first 208 fights went
otherwise, and the totals came to the same, 39%; the adept over the
novice 67% (189 to 93, 2 draws; 4 runs lost to the disc's link being
replaced under them), the veteran over the adept 59% (165 to 117), the
master over the veteran 61% (159 to 100, 29 draws), the master against
the original 64% (183 to 101): the order stands, each step outside the
noise. The bench's stages are open ground, and its CPUs stand near each
other; the detour is for the stages where a house, a rock or a fence
comes between them.

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

### Fights to the clock (2026-11-28)

`tools/port_cpu_bench.py` prints each fight's length in the fight's
frames and how many went past 5,400 (three minutes, where a tournament
judges a fight of two CPUs). Every pairing of the levels, before the CPU
committed (48 fights a pair, the limit 12,000 blanks, about 5,800
frames) and after (96 a pair; the master over the veteran 288):

| Pairing | past three minutes, before | after | A's rate, before | after |
|---|---|---|---|---|
| Novice against Novice | 1 of 48 | 0 of 96 | | |
| Adept against Novice | 0 of 48 | 0 of 96 | 75% | 59% |
| Adept against Adept | 3 of 48 | 0 of 96 | | |
| Veteran against Novice | 0 of 48 | 0 of 96 | 69% | 65% |
| Veteran against Adept | 2 of 48 | 0 of 96 | 54% | 59% |
| Veteran against Veteran | 5 of 48 | 0 of 96 | | |
| Master against Novice | 3 of 48 | 0 of 96 | 73% | 68% |
| Master against Adept | 8 of 48 | 0 of 96 | 71% | 69% |
| Master against Veteran | 8 of 48 | 0 of 288 | 65% | 56% |
| Master against Master | 23 of 48 | 0 of 96 | | |

So 53 of 480 fights (11%) went to the clock before, none of 1,152 after;
half the fights now end within 250 to 670 frames (8 to 22 s) and three
quarters within 1,330. The tournament's `cpu8` and `byes` (PORT.md, "A
local tournament") had 4 of 15 and 2 of 4 judged; now none. Each level
still beats each below it (with 96 fights a rate is good to about ten
points, with 288 to six: the master over the veteran, 162 to 126, is
the one step near the noise), and by more the further down it is; the
same levels against each other come to about half (46% to 53%). The
novice is beaten by every level and by the game's own CPU (39%). Against
the original (96 fights each): the novice 39%, the adept 40%, the
veteran 53%, the master 60% (65% before): committing costs more against
a CPU that sees an attack the frame it begins.

How it was found and tuned (`--tune`, 96 fights): with the first
pressing alone the veteran beat the novice only 54% and the master the
veteran 50%: pressing made every fight a few blind swings. What gave the
order back: the higher levels' patience made longer (the veteran over
the novice 69% with it), the novice's made shorter with more blows
(71%), and meeting him as he walks in. What did not move the master over
the veteran (each 96 fights, 50% to 57%): more feints, longer patience,
more or less aggression, meeting him always, the veteran meeting him
less or feinting not at all.

Unchanged where the setting is Original: the game's own CPU's fights
(four of the bench's fights, the game's CPU on both sides, and a person
who presses nothing against it with `--cpu original`) traced at a
checkpoint every 30 frames for 5,400 frames, positions, wounds, the
game's dice and all the plans' values, byte for byte the same before and
after (the port's CPU is never set up there: `port_CpuInit` returns at
`PORT_CPU_ORIGINAL`, and none of its code runs). The determinism check
(`--check`, the master against the veteran) and the rollback self-test
of the tournament's `rollback` case pass.

## 4. What is left

- Getting about: it walks round the stage's obstacles to him (since
  2026-11-27) and on over a drop or a step between them (2026-11-28),
  but the original's circling of an opponent at an edge, and its leaps,
  are not used.
- The master over the veteran is the thinnest step since the CPU
  commits (56%, "Fights to the clock"): none of the master's own numbers
  tried moved it (feints, patience, aggression, meeting him).
- Dirt, thrown sub-weapons and guns, which it neither uses nor answers
  better than by its guard.
- Levels learned from replays of real players (item 23's files hold every
  input of a fight).
- A person's hands on it: the bench measures CPU against CPU; how each
  level feels to fight is the owner's to say.
