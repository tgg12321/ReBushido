# tests/play/

Play tests: the game itself, run in an emulator with no window, pressed by
script and checked by reading its memory. They are for after a change to
ReBushido: does the disc still boot, does a build with nothing turned on
still play as the original, does a fight still run to its end.

They run on the developer's machine only. They need your own image of the
retail disc and a copy of the emulator, [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux),
and neither is ever in the repository, so GitHub Actions does not run them.
Where one is missing the tool says so and exits 0:

```
playtest: skipped: no C:\...\pcsx-redux.main (the emulator, PCSX-Redux; REBUSHIDO_EMULATOR names it)
```

## Running them

```
wsl bash tools/build.sh playtest            # from Windows: builds the discs, runs every test
wsl bash tools/build.sh playtest boot       # one test
make playtest                               # inside WSL or on Linux
make playtest PLAYTEST="boot quickboot"     # some of them
python tools/playtest.py [name...]          # the tests alone, with the discs already built
python tools/playtest.py --list
```

About eight minutes for the fourteen tests, after up to two minutes of
building discs; the tool holds a run to five minutes and the
suite to fourteen. It prints a table and exits 1 if a test failed:

```
  test             result   time
  boot             ok        10s
  bugfixes_fight   ok        31s
  fight_end        ok        28s
  first_frame_sweep ok        13s
  gte_model        ok        76s
  options_no_card  ok         9s
  options_page     ok        15s
  port_bugfixes    ok        35s
  port_game        ok        35s
  port_other_fight ok        61s
  port_picture     ok        63s
  quickboot        ok         7s
  same_as_original ok        60s
  vanish           ok        17s
  emulator processes left running: 0
  pictures and logs: build/playtest/<test>/<disc>/   (459s in all)
OK: play tests: 14 passed, 0 skipped
```

What it needs, and where it looks:

| | Default | Named by |
|---|---|---|
| The emulator | `tmp/pcsx-redux/pcsx-redux.main` in the checkout | `REBUSHIDO_EMULATOR` |
| The retail disc | `Bushido Blade 2 (USA).cue` in the checkout, its `.bin` beside it | `REBUSHIDO_RETAIL` |
| A BIOS | none: the emulator's own open one (`openbios.bin`, beside its program) | |
| Python | 3.11 or later, nothing installed | |

The emulator here is a Windows program, so the tests are run by Windows'
Python. Started inside WSL (`make playtest`), the tool starts itself again
with `python.exe`. On Linux proper it runs the emulator named by
`REBUSHIDO_EMULATOR` directly; that way has not been tried.

`make playtest` builds four discs into `build/play/` before it runs the
tests, each with a table of its names (`.sym`) beside it:

- `ReBushido.cue`, the ReBushido disc (`make rebushido-disc`);
- `QuickBoot-playtest.cue`, a quick-boot disc of the tests' own
  ([`docs/REBUSHIDO.md`](../../docs/REBUSHIDO.md), "Booting straight into a
  fight"): Mikado with a naginata against Utamaru, played by the CPU, at
  the lighthouse (`PLAYTEST_QB` in the `Makefile`);
- `QuickBoot-bugs.cue` and `QuickBoot-bugfixes.cue`, one fight that goes
  through two of the original's bugs, built with the "Bugfixes" setting
  off and with it on (`QB_BUGFIXES`): the vanisher with a naginata
  against Utamaru, played by the CPU, at the lighthouse (`PLAYTEST_BUGS`).

And two programs that are no disc: `GteProbe.exe` (`make gte-probe`), which
needs no retail image ("The GTE probe"), and the native port,
`build/port/rebushido` (`make port`), which reads the retail image itself
("The port as a disc").

A test whose disc is not there is skipped, saying which.

## The tests

| Test | Disc | What it shows |
|---|---|---|
| `boot` | retail | The disc boots, the opening is skipped, and the top menu is up |
| `same_as_original` | retail, then ReBushido | The same presses on both, after Start on ReBushido's options page with nothing changed: the opening, the menus, a versus fight, a kill, the replay, the menu after. At each of eleven checkpoints every value of `state.toml` that both programs have, the vertical blanks since the first of them and the picture are the same on both discs. This is the test that says "ReBushido with nothing turned on is the original" |
| `options_page` | ReBushido, twice, the second time with the first's cards | The options page comes up with the original's settings and a card it can save to; right and left turn Bugfixes on and off, down and left make Graphics "Remastered"; Start leaves it, the settings' file is written, and the game reaches its top menu. The second boot comes up with "Remastered" read from the card, and leaving it writes nothing |
| `options_no_card` | ReBushido, no card in either slot | The page comes up without waiting and says there is no card; the choice holds; leaving it takes no time and asks nothing; the game reaches its top menu |

What `same_as_original` had to give up when the options page came: the
ReBushido disc reaches every checkpoint 109 vertical blanks later than the
retail disc (the page, read from the card and left by a press, is 108 of
them; while the larger program was at the end of the disc the drive
added 161 more). So the blank a checkpoint is
reached at is compared as blanks since the checkpoint `movie`
(`frames_from`), and `movie` itself, the first, by its values and picture
alone. Everything else is compared as before, the game's own frame count
(`frames`) outright. The values that begin `rb_` are not compared: the
retail program has none.
| `quickboot` | quick boot | With no input the disc is in the fight it was built for: the mode, both fighters, their weapons, the stage, who plays the second fighter; within thirty seconds of the console's time |
| `fight_end` | quick boot | The first fighter walks in and strikes until one of the two is cut down; the replay and the winner's view pass; the menu after the fight comes up; Rematch starts the fight again. No crash on the way |
| `bugfixes_fight` | ReBushido | Right on the options page turns Bugfixes on, Start leaves it and the setting is written to the card; then the presses of `same_as_original`: the opening, the menus, a versus fight to a kill, the replay, the menu after. The setting is still on at the end and nothing crashed |
| `first_frame_sweep` | the bugs' fight, off and on | On the first fighter's first frame the hit tests' scratch has the sweep of its naginata's shaft: with the setting off the shaft's far end was swept from nowhere (0, 0, 0: the first fight since power on left nothing else), with it on from where it is |
| `vanish` | the bugs' fight, off and on | The CPU strikes the vanisher. With the setting off it comes back at (4000, -12000), another stage's start position, and two seconds later has fallen out of the stage; with it on it comes back at (3400, 7400), where it started, and stands |
| `port_game` | ReBushido, then the native port | The port is the game: the same presses on the emulator and on the port, from power on through the options page, the top menu and character select, a versus fight to a kill, the replay, the menu after and back to the main menu. At each of 27 checkpoints, one for every second of the fight, every value of `state.toml` but the count of frames shown is the same on both, the random numbers among them; from the fight's first frame on so are both fighters' whole structures, and on that frame the memory the game loaded from the disc: the effects' tables and 256 KB of the stage ("The port as a disc") |
| `port_other_fight` | ReBushido, then the native port | The same in another fight: each pad takes another weapon (a nodachi against a broadsword) and the first another stage. 31 checkpoints. Through the fight video memory is compared as well, at 24 of them ("The picture") |
| `port_picture` | ReBushido, then the native port | The port's picture is the emulator's: all of video memory, pixel for pixel, at 28 checkpoints from the options page through the boot image, the top menu, the four steps of character select, sixteen seconds of a fight, the kill, the replay and the menu after, and back in the main menu. Not a pixel differs on the options page, the boot image and the menus; up to three in a thousand in the fight, each checkpoint held to the number that was measured ("The picture") |
| `port_bugfixes` | ReBushido, then the native port | The same with Bugfixes on: `port_game`'s presses after right on the options page. The setting is on at every checkpoint |
| `port_sound` | ReBushido, then the native port | The port's sound is the emulator's: `port_game`'s run, comparing what Sony's sound libraries keep in memory (the common bank's header, where each bank is in sound memory, each voice's registers as last set, the library's record of each voice with its envelope's level, the keys, the reverb, the disc's volume) and, at twenty checkpoints, the sound chip itself: all of sound memory but the chip's own first 4 KB, and every register a program writes ("The sound") |
| `gte_model` | the GTE probe, no disc | The native port's C does what the PlayStation's coprocessor and assembly do: 125,800 made-up cases come out the same on the emulator and on the host ("The GTE probe") |

## The GTE probe

The native port ([`docs/PORT.md`](../../docs/PORT.md)) has the GTE, the
console's geometry coprocessor, written in C (`src/port/gte.c`), and C
beside the functions that are assembly. The probe is how that C is held
to the real thing. It is a PlayStation program of the tests' own, no part
of the game (`tests/play/probe/`), built by the game's compiler and
loaded by the emulator with no disc:

- **The operations.** For each of 42 instruction words (every one the
  game has, then the operations and the variants it does not use), 2,000
  cases: all 64 of the GTE's registers are loaded, the operation runs,
  all 64 are read back.
- **The routines.** For each of 64 routines that are assembly (47 of
  Sony's libgte, 17 of the game's), 500 cases: the registers are loaded,
  the routine is called with made-up arguments, and what it wrote is
  taken with the 64 registers after it. The routines are linked into the
  probe from the matching build's own objects: they are the game's
  listings.

- **The renderer.** For each of 49 things, 200 cases: the routines of
  the handler tables on a group of records, the projections, a part's
  groups, a fighter's body, a cloth, stage tiles, and the walk over a
  whole queue of nodes. A case makes up a mesh, the scratchpad and what
  else the routine reads, loads the registers and runs it; what is taken
  is all it could have written, word for word: the ordering tables, the
  primitives, the mesh, the scratchpad, the cursors, and the 64
  registers. The probe and the host test keep all of that at the same
  addresses, so links and pointers are compared as they are. The
  renderer's listings are linked from the matching build's objects with
  the game's own `render.c`.

Every number of a case is made up by a generator from the case's place in
the list (`cases.h`, `routines.h`, `render_cases.h`); nothing is the
game's data. The same files are compiled into three host tests,
`tests/port/gte_vectors.c`, `asm_vectors.c` and `render_vectors.c`, which
run the same cases on the port's C. When the
probe is built those write down a number for each case, which is compiled
into the probe; the probe compares as it goes and leaves the count of
cases that differed where the test reads it.

```
wsl bash tools/build.sh playtest gte_model   # build the probe, run it
wsl python3 tools/gte_probe.py               # the cases that differed, register by register;
                                             # for the renderer, word by word of memory as well
```

The emulator's GTE is itself somebody's reading of the hardware: the test
shows that the port's C and the emulator agree, not that either is the
console. What the port took from the emulator where the two might differ
is in `docs/PORT.md`, "How it is proved".

After a change to the port's C or to the cases, the numbers the host
tests hold on every push (`AGREED` in `tests/port/gte_vectors.py`,
`asm_vectors.py` and `render_vectors.py`) are no longer the C's: run this test, and when it
passes copy the new numbers in, as those files say.

## The port as a disc

The native port ([`docs/PORT.md`](../../docs/PORT.md)) is the game's C
built for a PC. It has no window, sound or pads yet, but it follows a
plan: the same file the emulator's script is given, and it writes the same
log, and the same pictures. So a test names it as a disc, `port`, and the tool runs
`build/port/rebushido --image <the retail .cue> --plan <plan> --out
<folder>` in the emulator's place (through `wsl` when the tool runs on
Windows). No emulator starts for that run.

`port_game`, `port_other_fight` and `port_bugfixes` run the ReBushido
disc on the emulator and then the port, and `compare` holds the second
to the first. What such a test has to do differently from one on two
discs:

- `cards = "none"`: the port has no memory card, so the emulator is given
  none either.
- No `frame`: the port's vertical blanks are not the console's. It
  shows no movie and reads the disc in no time, so it reaches every
  checkpoint sooner (the fight's first frame at blank 2,340 where the
  emulator is at 4,278). The game's own `frames` differs for the same
  reason and is not compared.
- The port draws, so a checkpoint has a picture on it too. It is
  compared as video memory, not as `picture` ("The picture"): the three
  tests that are about the game's values say `shot = false`.
- `compare` may name `dump`: each piece of memory the test's `[dump]`
  names is then compared between the two runs, byte for byte, at the end
  of the run. The two files are in the runs' folders and nowhere else.
- In the fight, presses and checkpoints are counted in **the game's
  frames**, not in blanks (`"fight+72f CIRCLE"`, `frames = 30`,
  `at_frame = true`: "Writing a test"). The game takes a frame of two
  blanks, and a blank falls while the emulator is still working a frame
  out or waiting it out, but on the port, where that work takes no
  time, always after it: a press an odd number of blanks from a
  checkpoint fell into the next frame on one and not on the other, and
  a checkpoint's values were of half a frame on one. A frame of the game
  begins at the same moment of the game on both. Up to the fight the
  presses are in blanks: the menu runs a frame a blank, and the two get
  to its pages at different blanks and frames anyway, since the port
  loads in no time.
- `compare` names `snapshot`: the memory `[snapshot]` names, written at
  the checkpoints that say so and compared between the two runs ("Memory
  at a checkpoint"). The port's variables are not where the
  PlayStation's are, so a piece is named by a symbol, and an address
  inside it is the same if it is the same place of the same name.

What is left out of the fighters' structures there (`except`), and why,
is in each test's file and in `docs/PORT.md`, "What the comparison
found": words copied off the stack, which is not the same stack on a
PC.

The port's own output is `port.log` in the run's folder; the lines of
`run.log` that begin `stub` are what the game asked for that the port
does not do yet, with how often.

## What a run is

One run is one disc in the emulator from power on. The tool counts
**vertical blanks** (sixty a second; the fight shows a frame every two),
presses buttons at the blanks the test names, and at each **checkpoint**
writes down the values of the game's memory that `state.toml` names and
takes a picture. Afterwards it checks what the test claims about those
values.

A test with several discs is a run on each, in order; a disc named twice
is run twice (the second run's folder is `<disc>-2`).

A run ends when its last checkpoint is reached. It fails if a checkpoint
is not reached in time, if what the test claims does not hold, or if the
game **crashes**, which here means one of:

- an exception that is neither an interrupt nor a system call (the
  processor's exception vector is watched): a bad address, a bad
  instruction, a `break`;
- the program counter outside the program's code, the movie overlay's
  place, the kernel's memory and the BIOS, at a vertical blank;
- the game's frame count (`g_disp_frame_count`) not moving for `stall`
  vertical blanks (thirty seconds if the test does not say), other than
  during the opening movie, whose player counts no frames.

Two more things stop a run from outside: its time limit (three minutes if
the test does not say; never more than 285 seconds), and the emulator
writing nothing for a minute.

Everything a run wrote is in `build/playtest/<test>/<disc>/` (not in git;
`make clean` removes it): a PNG for each checkpoint, `failure.png` and the
state at the moment a run went wrong, `run.log`, `plan.txt` and the
emulator's own output, `emulator.log`.

## Writing a test

A test is one file, `tests/play/NAME.toml` (every `.toml` here but
`state.toml` is a test). This is `boot.toml`, whole:

```toml
about = "The retail disc boots to the top menu."
discs = ["retail"]
limit = 6000

# Start skips the opening movie, cross the title, Start the memory card's
# message.
input = [
    "movie+300 START",
    "movie+600 CROSS",
    "movie+900 START",
]

# The program is up: its first screen is the movie's.
[[checkpoint]]
name = "movie"
when = "screen == SCREEN_MOVIE"
within = 2000
shot = false

# The top menu, two seconds after it opened.
[[checkpoint]]
name = "menu"
when = "screen == SCREEN_MENU_UPDATE && menu_page == MENU_PAGE_TOP && screen_frames >= 120"
within = 3000
expect = [
    "frame_period == 1",   # the menu runs at 60 frames a second
    "loaded_stage == -1",  # and no stage is in memory
]
```

**At the top**

| Key | |
|---|---|
| `about` | One line, shown by `--list` |
| `discs` | The discs to run on, in order: `retail`, or the name of an image in `build/play/` without `.cue` and in any case (`rebushido`, `quickboot`, `quickboot-playtest`). A name that is a program there and no image (`gteprobe`: `GteProbe.exe`) is loaded with no disc |
| `limit` | The vertical blank the run ends at, at the latest |
| `input` | The presses (below) |
| `compare` | With two discs or more: what must be the same on every disc as on the first, at every checkpoint both runs have. Names of `state.toml`, `frame` (the blank the checkpoint was reached at), `picture` (the screenshot, pixel for pixel), `vram` (video memory at the checkpoints that say `vram`: "The picture"), `spu` (the sound chip at the checkpoints that say `spu`: "The sound"); `["*"]` is the values, `frame` and `picture`. A value that one of the two programs does not have is not compared |
| `frames_from` | A checkpoint's name: `frame` is compared as the blanks since that checkpoint, at the checkpoints after it, and not at all up to it. For discs that reach the game's first screen at different times |
| `cards` | The memory cards: `"new"` (two new, empty, formatted cards in every run; this if not given), `"keep"` (every run after the first starts with the cards the run before left), `"none"` (no card in either slot) |
| `stall` | Vertical blanks without a frame that count as a crash (1800) |
| `seconds` | The run's time limit (180; 285 at most) |
| `[state]` | More values for this test alone, written as in `state.toml` |
| `common_state` | `false`: the test has only its own `[state]`, none of `state.toml`'s. For a program that is not the game |
| `[dump]` | Memory to write to a file when the run ends, for looking into a failure: `NAME = "symbol+offset:bytes"` writes `NAME.bin` into the run's folder |
| `[snapshot]` | Memory to write at the checkpoints that say `snapshot`, and to compare if `compare` names `snapshot` ("Memory at a checkpoint") |
| `port_gpu` | `"emulator"`: on the native port, the GPU draws as the emulator's software GPU in the two things where that is not as the console ("The picture"). `"console"` if not given |

**A press** is `"WHEN BUTTON"`: `"5200 START"` presses Start at vertical
blank 5200 from power on, `"menu+30 DOWN"` thirty blanks after the
checkpoint `menu` was reached. `P2` before the button is the second pad
(`"select+320 P2 CROSS"`); `hold N` after it holds the button for N blanks
in place of six (`"fight+100 RIGHT hold 40"`). The buttons are `SELECT
START UP RIGHT DOWN LEFT L1 R1 L2 R2 TRIANGLE CIRCLE CROSS SQUARE`. Time a
press from a checkpoint wherever you can: a build that boots or loads a
little later is then still pressed at the same moments of the game.

An `f` after the number counts **frames of the game** in place of
blanks: `"fight+72f CIRCLE"` goes down as the 72nd frame after the
checkpoint `fight` begins, and the game reads it in the frame after
that; `hold 20f` holds it twenty frames (three if not given). A frame
of the game begins where the game resets its counter of scan lines,
once a frame (`ResetRCnt`, `main.c`): the emulator's script stops
there. Two programs that take different numbers of blanks to work a
frame out, as the emulator and the native port do, are then pressed in
the same frame.

**A checkpoint** (`[[checkpoint]]`, as many as wanted) is reached in the
order of the file, each one looked for only after the one before it:

| Key | |
|---|---|
| `name` | Its name: the picture's file, and what a press is timed from |
| `when` | Reached when this holds: `VALUE == NUMBER`, several joined by `&&`. A value is a name of `state.toml`; the comparisons are `== != < <= > >=` |
| `at` | Or: reached at this vertical blank from power on |
| `after` | Or: this many blanks after the checkpoint before it |
| `frames` | Or: this many frames of the game after the checkpoint before it, at the moment the last of them begins: its values are those of whole frames |
| `call` | Or: when the function of this name is next called; `count = N` waits for the Nth call. For looking inside a frame ("Finding where two runs part") |
| `at_frame` | With `when`: `true` looks for it only as a frame of the game begins, not at a blank, which can fall in the middle of one |
| `repeat` | With `after`, `frames` or `call`: this many checkpoints one after another, `NAME_1` to `NAME_N` |
| `snapshot` | `true`: the memory of `[snapshot]` is written here; or a list of its names, for only those |
| `vram` | `true`: all of video memory is written here, to `NAME.vram` ("The picture") |
| `vram_differ` | With `vram`: how many of its pixels may differ from the first run's (none, if not given) |
| `spu` | `true`: the sound chip is written here, to `NAME.spu` ("The sound") |
| `calls` | `true`: on the native port, every function called from here to the next checkpoint is a line of `NAME.calls` in the run's folder, indented by how deep the call is |
| `within` | Give up, and fail, this many blanks after the checkpoint before it (if not given, at `limit`) |
| `expect` | What must hold there: expressions of values, numbers and names, with `+ - * & \| == != < <= > >= && \|\|` and brackets; `frame` is the blank it was reached at |
| `shot` | `false`: no picture |
| `runs` | The runs that have this checkpoint, counted from 1 in the order of `discs` (`runs = [2]`); every run if not given. A press timed from a checkpoint is made only in the runs that have it |

**Values** are the names of [`state.toml`](state.toml): the screen, the
menu's page, the mode and the stage, the random numbers, the frame counts,
and for each fighter who it is, where it stands, whether it has fallen, its
wounds, how many frames it has been in the fight and how often it has
vanished, and ReBushido's (`rb_page`, `rb_row`, `rb_card`, `rb_save`,
`rb_bugfixes`, `rb_graphics`). Each is a name of the source and an offset,
so it is read at the right place on any build. A disc whose program does
not have the name (the retail disc has no `g_rb_...`) has no such value:
it is not written down there, and a checkpoint of that run that asks it
is an error. Add to that file what a new test needs (and, for a member of
a structure, to `tests/rebushido/play_state.py`, which holds the offsets
to the headers in `make rebushido-test`).

**Names**: any member of an enum, or `#define` of a plain number, in
`include/sys.h`, `fight.h`, `fighter.h`, `stage.h`, `weapon.h`, `menu.h`,
`rb_settings.h`, `rb_save.h`, `rb_options.h` and `src/main/menu_screen.c`
can be written for its number (`SCREEN_FIGHT_UPDATE`, `FIGHT_NOBODY`,
`ROSTER_MIKADO`, `RB_PAGE_UP`, `RB_SAVE_WRITTEN`). On a quick-boot
disc `QB_MODE`, `QB_P1`, `QB_P2`, `QB_WEAPON1`, `QB_WEAPON2`, `QB_STAGE`,
`QB_CPU2` and `QB_BUGFIXES` are what the disc was built to start.

To see what a new test does before it claims anything, give it checkpoints
with `at` or `after` and no `expect`, run it, and look at the pictures and
at `run.log` (each `cp` line is a checkpoint's blank and every value, in
the order of `plan.txt`'s `var` lines).

## Memory at a checkpoint

`[snapshot]` names pieces of the game's memory, as `[dump]` does, and a
checkpoint that says `snapshot = true` (or names some of them) writes
them as they are at that moment, to `CHECKPOINT.NAME.snap` in the run's
folder:

```toml
compare = ["rng", "p1_x", "snapshot"]

[snapshot]
fighters = "g_fighters:0x898 except 0xC4:4 0xD4:4"   # both fighters, but two words
scratch = "0x1F800000:1024"                          # the scratchpad
variables = "variables"                              # every variable both programs have

[[checkpoint]]
name = "f"
frames = 30
repeat = 16
snapshot = ["fighters"]
```

With `snapshot` in `compare`, each piece of a run is held to the same
piece of the first run, at every checkpoint both wrote it:

- A piece named by a symbol is the same structure in both programs,
  wherever each has it, and is compared from its start, byte for byte.
  `except OFFSET:BYTES ...` leaves bytes of it out: say why beside it.
- A word that differs is still the same if both words are addresses of
  the same place of the same name: a pointer to a variable or a
  function of the program, which the ReBushido disc and the native port
  do not have at one address.
- `variables` is every variable the test's discs all have, each
  compared with the one of its name. It needs the variables' sizes,
  which only the port's table of names has, so one of the discs is
  `port`. It is for finding a difference, not for a test that is kept:
  the ordering tables, the sound's and the disc's state are never the
  same on the port.

A test that fails names the first places; the rest is
`tools/port_compare.py`'s.

## The picture

A checkpoint that says `vram = true` writes all of the PlayStation's
video memory as it is at that moment to `CHECKPOINT.vram` in the run's
folder: 1024 by 512 pixels of 16 bits, the two buffers of the picture
with the textures and the colour tables beside and below them. The
emulator's script takes it from the emulator's software GPU, which is
the one it draws with when it has no window (`PCSX.GPU.getVRAM()`; the
megabyte the emulator writes after it is not video memory and is not
read). The native port writes its own
([`docs/PORT.md`](../../docs/PORT.md), "The picture").

With `vram` in `compare`, each run's video memory is held to the first
run's at every checkpoint both wrote it, pixel for pixel
(`tools/play/picture.py`). A checkpoint fails if more pixels differ than
its `vram_differ` says, which is none unless it says. What differed at
each checkpoint is a line of `vram.txt` in the second run's folder:

```
options          the same
select_stage     58 pixels differ: buffer 0 29 (364,103)-(570,207); buffer 1 29 (364,343)-(570,447); 48 of them by 1 of 31 or less, the most by 3
f_3              148 pixels differ: buffer 0 65 (14,10)-(635,223); buffer 1 83 (14,252)-(619,478); 46 of them by 1 of 31 or less, the most by 20
```

how many, in which part of video memory and inside which rectangle of
it, and how far apart the two pixels' colours are. To look at them:

```
python tools/port_compare.py port_picture --vram          # the same lines, and for each checkpoint that
                                                          # differs three pictures in the port's folder
python tools/port_compare.py port_picture f_3 --vram      # one checkpoint
```

`f_3.vram-first.png` and `f_3.vram-second.png` are the two video
memories whole, and `f_3.vram-differ.png` is the first, dimmed, with
every pixel that differs in red.

Three things to know when a test compares pictures:

- **Take the checkpoint as a frame of the game begins** (`at_frame`,
  `frames`), or where the picture stands still. What is in video memory
  then is the frame before, drawn whole: the game draws a frame's
  ordering table just after that moment. Both buffers are compared; in
  a fight they hold two frames.
- **What the game's clock moves is not at the same place on both.** The
  port loads in no time, so a screen's own count of frames, and
  `rand()`, which the frame's wait stirs, are elsewhere: a cursor that
  pulses, the loading screen's noise, a fight's sparks. Compare where
  they are not on show, or say how many pixels they are.
- **`port_gpu = "emulator"`** for a test of the port against the
  emulator. The emulator's GPU does two things otherwise than the
  console is known to (`docs/PORT.md` has them), and without this the
  port's dithered floor alone is a third of a fight's pixels.

The picture on show, which every checkpoint takes unless it says `shot
= false`, is the displayed rectangle of the same memory, as a PNG; the
port's is written by the same rule as the emulator's. `picture` in
`compare` holds two runs' to each other with nothing allowed to differ.

## The sound

A checkpoint that says `spu = true` writes the sound chip as it is at
that moment to `CHECKPOINT.spu` in the run's folder: sound memory, 512
KB, and then the chip's 256 registers of 16 bits. The native port
writes its own chip's (`src/port/spu.c`). The emulator gives a script
no way to its sound chip but one, its whole saved state, so its script
writes that, 22 MB, as `CHECKPOINT.state`; the tool cuts the chip's two
pieces out of it and removes it (`tools/play/spu.py`).

With `spu` in `compare`, each run's chip is held to the first run's at
those checkpoints, and nothing may differ of what is compared:

- sound memory from `0x1000` on, byte for byte: every bank's waveforms
  and the reverb's work area. The first `0x1000` bytes are the chip's
  own buffers of the last samples of the disc's sound and of two
  voices, which depend on the very sample the checkpoint fell on;
- the registers a program writes. Not a voice's envelope level and loop
  address, the keys on and off, the voices that reached an end and the
  port for data written by hand: those are the chip's own or are taken
  away by it, and the emulator's state has them only as last written.

What differed at each checkpoint is a line of `spu.txt` in the second
run's folder. `port_sound` is the test that does this.

## Finding where two runs part

When the port (or a changed disc) stops doing what the other run does,
find the first frame and the first value that differ, then the function
that made them. This is how the two faults of the port's first fight
were found (`docs/PORT.md`, "What the comparison found").

1. **The frame.** Copy the test to a scratch file (`tmp/x.toml`; a
   test can be anywhere), give it `[snapshot]` with what could matter
   (`fighters`, `scratch`, then `variables` and the mapped memory,
   `ram = "0x8010DB00:0xF2500"`), and in place of its later checkpoints
   one `frames = 1` with `repeat` and `snapshot = true`. Run it, then:

   ```
   python tools/port_compare.py tmp/x.toml --new      # what differs at each checkpoint
                                                      # and did not at the one before
   python tools/port_compare.py tmp/x.toml f_167 --only fighters
   ```

   Every line is a run of bytes: the piece, the variable and the offset
   into it, the bytes on the first run and on the second. Much of
   `variables` differs from the first frame and for good (the frame
   count, the ordering tables, sound); `--new` leaves that out after
   the first checkpoint.
2. **The function.** Give the checkpoint before the frame that differs
   `calls = true`: the port then lists every function it calls in that
   frame (`NAME.calls`). Put checkpoints at calls of them, in the order
   they are called (`call = "fighter_Update"`, `count = 2` for the
   second call, `snapshot = true`), and compare again: the difference
   is made between the last checkpoint that agrees and the first that
   does not. Narrow it with the calls in between. A checkpoint at a call
   is reached on the emulator by a breakpoint at the function and on the
   port by the function itself, which is compiled to say when it begins.
3. **Why.** Both programs then run the same C from the same memory to a
   different result. The causes so far: the order in which a call's
   arguments were worked out (`tools/port_order.py` finds every such
   place); what a function that returns nothing left where a result is
   read; memory nothing set, which is whatever was there.

`tools/port_compare.py` reads what a run of `tools/playtest.py` left and
runs nothing: `--first` stops at the first checkpoint that differs,
`--lines N` shows more of one, `--runs A B` takes two other runs of the
test, `--holes` shows what `except` leaves out as well, `--vram`
compares video memory too ("The picture").

## How the emulator is run

`tools/playtest.py` starts one emulator at a time, never a second while
one is alive, as:

```
pcsx-redux.main -portable <run's folder> -safe -noupdate -no-ui -interpreter -debugger
                -fastboot -iso <disc.cue> -memcard1 <new card> -memcard2 <new card>
                -dofile <run's folder>/start.lua -run
```

(For a program with no disc, `-loadexe <program>` in place of
`-iso <disc.cue>`; nothing then watches the game's frame count, which
such a program does not have.)

- `-no-ui` runs it with no window and with a silent sound driver; `-safe`
  keeps it from reading anybody's settings; the memory cards are files in
  the run's folder, new and empty unless the test says `cards = "keep"`,
  and with `cards = "none"` the emulator is told that both slots are
  empty.
- On Windows it is started on a desktop of its own, with the system's
  error boxes off, in a job that is ended as a whole
  (`tools/play/launch.py`): a window or a dialog, if one were ever
  opened, would be made where nobody sees it; the tool also looks for a
  visible window of the emulator's on your desktop and fails the suite if
  it finds one. After each run it checks that no process of the emulator
  is left, and prints the count under the table.
- The interpreter and the debugger are what the check of the exception
  vector needs. It runs about three times the console's speed.
- The emulator's script is `tools/play/runner.lua`, the same file for
  every run. What a run does is data it reads (`plan.txt`), not Lua the
  tool writes: a script that fails to load, as one with too many
  constants does, is an error the emulator reports for itself. Anything
  the emulator says about a Lua error fails the run and stops the suite.
- A run is the same every time: the same presses give the same values at
  the same blanks, to the pixel.
