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

- Every change is behind a switch of `include/rebushido.h`, and the
  original's lines stay as they are (`docs/STYLE.md`, "ReBushido").
- With the switches off the build must still match the original:
  `wsl bash tools/build.sh checks` runs ten checks here (the seven of
  `decomp`, then the ReBushido build, its tests and its modern-compiler
  run).
- `wsl bash tools/build.sh rebushido` builds it into `build/rebushido/`.
  Never edit `bb2.ld` for ReBushido.
- `wsl bash tools/build.sh playtest` runs the play tests in an emulator
  with no window (`tests/play/README.md`); run it before pushing a change
  to the game. It is not one of the ten checks.
- To try a change quickly: `wsl bash tools/build.sh quickboot-disc QB_...`
  (`docs/REBUSHIDO.md`, "Booting straight into a fight").
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
