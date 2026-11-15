# CLAUDE.md

ReBushido: a modded *Bushido Blade 2* (SLUS-00663) built on its matching
decompilation. Project facts,
toolchain, build, gates and conventions are in [AGENTS.md](AGENTS.md):

@AGENTS.md

## Where things stand

Every function is decompiled and the build reproduces the original
executable. The source has been through a readability pass, an accuracy pass
and a polish pass; the standard it is held to is
[docs/STYLE.md](docs/STYLE.md). What is left is in
[docs/STATUS.md](docs/STATUS.md): "What is still a stand-in" (code that
matches but is probably not what the authors wrote, detailed in
[docs/FAKES.md](docs/FAKES.md)), "What was tested and what was not", and
"Open questions". How the project got here:
[docs/HISTORY.md](docs/HISTORY.md).

Two branches. `decomp` is the byte-matching decompilation and stays that:
no bug fix or behaviour change goes on it. `main` is ReBushido, where the
work now happens ([docs/REBUSHIDO.md](docs/REBUSHIDO.md), and what is
planned in [docs/ROADMAP.md](docs/ROADMAP.md)).

## Working on `main` (ReBushido)

- Only `decomp` has to match the original's bytes. The owner
  (2026-11-15): on `main` "optimize, organize, and do whatever we like";
  so edit the game's code directly and cleanly here. A switch of
  `include/rebushido.h` is for what a player turns on or off, or for
  where the PlayStation and the PC need different code, not for keeping
  the original's lines.
- What guards `main` instead: the PlayStation ReBushido build must
  build, fit and pass its play tests (`same_as_original`: with nothing
  switched on it plays as the retail disc), and the port must pass its
  own. `wsl bash tools/build.sh checks` runs the checks; the ones that
  hold the switches-off build to the original's bytes are retired here
  as changes make them fail.
- `wsl bash tools/build.sh rebushido` builds it into `build/rebushido/`.
  Never edit `bb2.ld` for ReBushido.
- A fix of an original bug is `RB_FIX_<NAME>` plus
  `if (g_rb_settings.bugfixes)`; the table is in `docs/REBUSHIDO.md`,
  "The fixes".
- `wsl bash tools/build.sh playtest` runs the play tests in an emulator
  with no window (`tests/play/README.md`); run it before pushing a change
  to the game. It is not one of the eleven checks.
- To try a change quickly: `wsl bash tools/build.sh quickboot-disc QB_...`
  (`docs/REBUSHIDO.md`, "Booting straight into a fight").
- `wsl bash tools/build.sh rebushido-patch` makes a trial patch in
  `build/release/`; `RB_RELEASE=1` makes a release's and needs a clean
  tree. Tagging and publishing are the owner's. Nothing in
  `build/release/` is ever committed.
- The native port ([docs/PORT.md](docs/PORT.md)) is behind `RB_PORT`: C
  beside each `INCLUDE_ASM` in the `#if`, the original line in the
  `#else`; port-only code in `src/port/` (`port_` names), which no
  PlayStation build compiles.
- Port stage one is done: every function that is assembly has C beside
  it under `RB_PORT`. After a change to that C, to `src/port/gte.c` or to
  `tests/play/probe/*.h`: `wsl bash tools/build.sh playtest gte_model`,
  then copy the new numbers into `AGREED` of
  `tests/port/{gte,asm,render}_vectors.py`.
- `wsl bash tools/build.sh port` builds the native port into
  `build/port/`; it runs as `build/port/rebushido --image <.cue>` and is
  held to the emulator by the play test `port_game`.
- `make port` also runs `python3 tools/port_order.py --check`: a place
  where the order an expression is evaluated in may matter is fixed under
  `RB_PORT` (the call first) or added to `config/port_order.txt` with its
  reason (`--update`).
- Port against emulator: five play tests (`port_game`,
  `port_other_fight`, `port_bugfixes`, `port_picture`, `port_sound`); to
  find a divergence,
  `tests/play/README.md`, "Finding where two runs part"
  (`tools/port_compare.py`). In a fight, time presses and checkpoints in
  game frames (`72f`, `frames =`), not blanks. The play tests take about
  nine minutes: run `playtest` in the background or with a timeout over
  ten minutes, and only one run at a time on the machine.
- The port's GPU is `src/port/raster.c`; change it with
  `tests/port/raster.c` and the play tests `port_picture` and
  `port_other_fight`, which hold video memory to the emulator's at a
  number of pixels per checkpoint (`vram_differ`).
  `python tools/port_compare.py port_picture --vram` shows what differs.
  A test of the port's picture says `port_gpu = "emulator"`.
- The port is a 64-bit program; `make port` builds and checks both
  sizes (`build/port/`, `build/port32/`). New code that makes a pointer
  of a number uses `ADDR()`; a pointer member of anything laid over a
  file, the scratchpad or the arena is a `PTR32` slot (`docs/PORT.md`,
  "64 bits"; `docs/STYLE.md`, 29).
- The port's window build is run by agents only with
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`: no visible window, sound
  or dialog. Never install `gcc-multilib` in the build's WSL: it removes
  the MIPS cross compiler.
- New agents are Opus, in parallel, each in its own worktree; only one
  agent at a time may run the emulator (the play tests have no lock).
- The owner's choices for the port: 64 bits, SDL 2, the
  player's disc image for the data, a software renderer that matches the
  PlayStation first, movies decoded in the port.
- The port's sound: Sony's libsnd and libspu are compiled in; the chip
  is `src/port/spu.c`. `build/port/rebushido ... --wav FILE` keeps a
  run's sound and `python tools/port_wav.py FOLDER` reads it by numbers;
  `port_sound` holds the library's state and the chip's memory and
  registers to the emulator's.
- `wsl bash tools/build.sh patcher` builds the players' patcher into
  `build/release/` (`ReBushidoPatcher.exe` needs `mingw-w64` in WSL).
  Never run the `.exe` without an image argument from an agent: it opens
  a dialog.
- Merge `decomp` into `main`, never the reverse.

## Working on `decomp`

- The first gate is the SHA1: `wsl bash tools/build.sh check`, and
  `wsl bash tools/build.sh movovl` as well when `src/main/psxsdk/` or
  `movovl/` changes.
- `python tools/count.py --check` and `python tools/lint.py --check` must
  pass, and `make shift`, `make modern` and `make test` too.
  `wsl bash tools/build.sh checks` runs all seven gates, as GitHub Actions
  does on each push, and prints a summary.
- Identifiers are English; proper nouns from the game stay.
- Work one subsystem at a time; the helpers are in `tools/`
  ([tools/README.md](tools/README.md)).
- Commit on `decomp` when the build matches. Plain commit messages; no special
  conventions.

## Standing warnings

- `bb2.ld` is hand-maintained and all data is defined in C: nothing
  regenerates the linker script or the listings.
- The emulator runs only through `tools/playtest.py`, which runs it with
  no window, one at a time, with a time limit, on the owner's machine; it
  and the disc images are never committed.
- Build files must be LF (see AGENTS.md).
- Scratch goes in `tmp/<task>/` and is removed when the task is done.
