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

About four and a half minutes for the ten tests, after up to two minutes
of building discs; the tool holds a run to five minutes and the suite to
ten. It prints a table and exits 1 if a test failed:

```
  test             result   time
  boot             ok         9s
  bugfixes_fight   ok        29s
  fight_end        ok        26s
  first_frame_sweep ok        13s
  gte_model        ok        79s
  options_no_card  ok         9s
  options_page     ok        14s
  quickboot        ok         7s
  same_as_original ok        58s
  vanish           ok        19s
  emulator processes left running: 0
  pictures and logs: build/playtest/<test>/<disc>/   (263s in all)
OK: play tests: 10 passed, 0 skipped
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

And one program that is no disc: `GteProbe.exe` (`make gte-probe`), which
needs no retail image ("The GTE probe").

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
| `compare` | With two discs or more: what must be the same on every disc as on the first, at every checkpoint both runs have. Names of `state.toml`, `frame` (the blank the checkpoint was reached at), `picture` (the screenshot, pixel for pixel); `["*"]` is all of them. A value that one of the two programs does not have is not compared |
| `frames_from` | A checkpoint's name: `frame` is compared as the blanks since that checkpoint, at the checkpoints after it, and not at all up to it. For discs that reach the game's first screen at different times |
| `cards` | The memory cards: `"new"` (two new, empty, formatted cards in every run; this if not given), `"keep"` (every run after the first starts with the cards the run before left), `"none"` (no card in either slot) |
| `stall` | Vertical blanks without a frame that count as a crash (1800) |
| `seconds` | The run's time limit (180; 285 at most) |
| `[state]` | More values for this test alone, written as in `state.toml` |
| `common_state` | `false`: the test has only its own `[state]`, none of `state.toml`'s. For a program that is not the game |
| `[dump]` | Memory to write to a file when the run ends, for looking into a failure: `NAME = "symbol+offset:bytes"` writes `NAME.bin` into the run's folder |

**A press** is `"WHEN BUTTON"`: `"5200 START"` presses Start at vertical
blank 5200 from power on, `"menu+30 DOWN"` thirty blanks after the
checkpoint `menu` was reached. `P2` before the button is the second pad
(`"select+320 P2 CROSS"`); `hold N` after it holds the button for N blanks
in place of six (`"fight+100 RIGHT hold 40"`). The buttons are `SELECT
START UP RIGHT DOWN LEFT L1 R1 L2 R2 TRIANGLE CIRCLE CROSS SQUARE`. Time a
press from a checkpoint wherever you can: a build that boots or loads a
little later is then still pressed at the same moments of the game.

**A checkpoint** (`[[checkpoint]]`, as many as wanted) is reached in the
order of the file, each one looked for only after the one before it:

| Key | |
|---|---|
| `name` | Its name: the picture's file, and what a press is timed from |
| `when` | Reached when this holds: `VALUE == NUMBER`, several joined by `&&`. A value is a name of `state.toml`; the comparisons are `== != < <= > >=` |
| `at` | Or: reached at this vertical blank from power on |
| `after` | Or: this many blanks after the checkpoint before it |
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
