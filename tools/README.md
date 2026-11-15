# tools/

What each thing here is for. Unless it says otherwise a script runs from the
repository root; the ones that read the build's output run inside WSL after a
build.

## Building

| | |
|---|---|
| `setup_wsl.sh` | One-time set-up on Ubuntu: packages, the Python virtualenv, the compiler |
| `build_cc1.sh` | Builds the compiler, GCC 2.7.2's `cc1`, into `gcc-2.7.2/` ([`docs/COMPILER.md`](../docs/COMPILER.md)) |
| `cc1-reorg-negate-rtx-decl.patch` | The one change made to the compiler's source: a missing declaration that crashes it on a 64-bit host |
| `build.sh` | `bash tools/build.sh [check\|clean-check\|movovl\|shift\|modern\|rebushido\|rebushido-disc\|quickboot\|quickboot-disc]`: activates the virtualenv and runs `make`, hiding the assembler's noise and exiting with `make`'s status; `bash tools/build.sh test` runs the tests, `bash tools/build.sh playtest [name...]` builds the play tests' discs and runs them |
| `checks.sh` | `make checks` (`bash tools/build.sh checks`): every check GitHub Actions runs, in its order (the counts, the lint, `modern`, `test`, the game's bytes, the overlay's, `shift`, then ReBushido's build, tests and `modern`), each one's output in `build/checks/<name>.log`, and a summary; exit 1 if any failed. The list is the workflow's: change both together |
| `rebushido.py` | The ReBushido build's link ([`docs/REBUSHIDO.md`](../docs/REBUSHIDO.md), "How it links"): `ld` writes `build/rebushido/rebushido.ld` from `bb2.ld` and the objects, adding what `bb2.ld` does not place; `fit` reports the program's size and the room left, and fails if it does not fit. `make rebushido` runs both |
| `quickboot.py` | The fight a quick-boot build starts ([`docs/REBUSHIDO.md`](../docs/REBUSHIDO.md), "Booting straight into a fight"): `--list` prints what each `QB_` variable of the `Makefile` can be, by the game's own names; `check` is what `make quickboot` runs before it compiles, `write` what `make quickboot-disc` leaves beside the disc image |
| `maspsx/` | The stand-in for Sony's assembler ASPSX 2.34: a modified copy of [mkst/maspsx](https://github.com/mkst/maspsx) |
| `make_psexe.py`, `elf32.py` | Put the PS-X EXE header in front of the linked image; the small ELF reader it uses |
| `gcc-2.7.2/` | The compiler's source and binary (not in git; made by `build_cc1.sh`) |

## Checking the source

| | |
|---|---|
| `count.py` | Counts what `docs/STATUS.md` reports: functions in C and in assembly, listings, `FAKE` notes by kind, `goto`s by verdict, casts, the linker scripts' lines and gaps, files, `unkNN` and `D_` names. It checks every `FAKE` note and `goto` of the source against the tables of `docs/FAKES.md`; `--check` fails if they disagree (a gate), `--notes` lists each note with its kind. Needs no build. It counts the source as the matching build compiles it (what is inside an `#if` of ReBushido's switches left out) and lists ReBushido's files, switches and `#if` blocks apart |
| `casts.py` | Counts the casts of the game's files and the overlay's own, by kind; `count.py` runs it |
| `lint.py` | Counts the checklist of `docs/STYLE.md` in the game's own files, per file and in total: contentless names, bare numbers, `extern` and prototypes in `.c` files, declarations in the wrong header, `unkNN`, missing comments and the rest (its docstring lists them). `--file` lists a file's findings, `--list <check>` one check's; `--check` fails if a count is over `lint_baseline.tsv` (a gate), `--update` rewrites that. Needs no build. It reads `src/rebushido/` too and both sides of every `#if` of ReBushido's switches, and `rb-flag` counts the switches asked wrongly |
| `shift_test.py` | `make shift`: relinks with padding, fails if anything depends on where code or data is, and reports the room left below the fixed buffers |
| `modern/check.py` | `make modern`: gives the game's C (`src/main/*.c`, `movovl/src/*.c`) to the host's modern `gcc` as 32-bit code, with `-Wall -Wextra`, more warnings and `-fanalyzer`, only for what it says: nothing it makes is used. `modern/shim.h` and the headers it rewrites into `build/modern/include/` stand in for what only the old compiler or the PlayStation has (assembly functions, GTE macros, a cast that is assigned to). Writes `build/modern/report.txt`; `--check` fails on a finding of a serious kind (something read before it is set, an index past an array, a wrong format, a missing return) that `modern/baseline.tsv` does not have, `--update` rewrites that, `--raw <file>` shows the compiler's own text. `modern/verdicts.tsv` holds what was decided about each finding read: the tool is wrong, the original's bug, or how the bytes need it. Needs WSL's `gcc`, no build |
| `../tests/run.py` | `make test` (`bash tools/build.sh test [-v] [name...]`): builds the game's pure functions for the host, from the game's own `.c` files, and compares what they compute with real maths, hand-made cases and the disc's data; a line a test, exit 1 if one fails. The tests, how to add one and what they stand on: [`tests/README.md`](../tests/README.md). Needs WSL's `gcc` and `python3`, no build; the disc's tests are skipped without the disc |

## Playing the game by script

On the developer's machine only: these need your own retail image and
PCSX-Redux. How to run them and how a test is written:
[`tests/play/README.md`](../tests/play/README.md).

| | |
|---|---|
| `playtest.py` | `make playtest` (`bash tools/build.sh playtest [name...]`; `python tools/playtest.py [name...]` with the discs already built): runs the tests of `tests/play/` in the emulator with no window, one run at a time, each ended by a time limit; a table of test, result and time, exit 1 if one failed; "skipped", and exit 0, where the emulator or a disc is missing. Pictures and logs go to `build/playtest/`. A test's disc `port` is the native port's program, run in the emulator's place with the same plan (through `wsl` from Windows). `--symbols ELF OUT` writes the table of a built disc's names, which the disc targets run |
| `play/runner.lua` | The emulator's side: the one script it is given. It reads the run's plan (data, not generated Lua), presses the pads, writes down the watched values at each checkpoint, takes the pictures, and ends the run on a crash (an exception, the program counter astray, no frame for too long) |
| `play/launch.py` | Starts the emulator so that nothing of it is seen or left: on Windows on a desktop of its own, with the system's error boxes off, in a job that is ended as a whole; reports any visible window and any process left |
| `gte_probe.py` | After the play test `gte_model` has failed: the cases the emulator and the native port's C made different things of, register by register, and for a case of the renderer's word by word of memory (it reads what the probe left, and runs the host tests' programs for the port's side). `tests/play/README.md`, "The GTE probe" |

## Working on the source

| | |
|---|---|
| `format.py` | `python tools/format.py <files>`: clang-format with the repository's `.clang-format`; `--check --all` says whether every C file is as the formatter leaves it |
| `normalize_lf.py` | Makes a file's line endings LF |
| `rename.py` | Whole-word rename, from a map file (`old new` a line), across everything the build reads; renames a function's listing in `asm/funcs/` too |
| `objdiff.py` | Per-function comparison of any two object files: which functions changed, and how ([`docs/MATCHING.md`](../docs/MATCHING.md), "The loop") |
| `listings.py` | Checks the names in `asm/funcs/` against the build; `--write <function>...` writes the disassembly of any function of the build into `build/reference/` |
| `symbol_addrs.py` | Writes `config/symbol_addrs.txt`, every symbol and its address with the functions marked, from the built ELF; `--check` says whether it is current |

## The disc and its files

Formats and usage: [`docs/formats/`](../docs/formats/README.md). These need
your own copy of the disc.

| | |
|---|---|
| `extract_iso.py` | Extracts the files of a disc image to `disc/` |
| `rebushido_patch.py`, `rebushido_release.py` | A ReBushido release: the BPS patch against the retail image, made and applied with the standard library alone, and what `make rebushido-patch` puts beside it (`docs/REBUSHIDO.md`, "Making a release") |
| `build_disc.py` | Builds a disc image from the original plus a rebuilt executable and replaced files |
| `unpack_all.py` | An inventory of every asset file under `disc/` |
| `inspect_ndata.py`, `repack_ndata.py` | The NDATA archive (models, stages, weapons, cutscenes) |
| `inspect_bbm.py`, `inspect_windat.py` | Motion files and the win-pose table |
| `inspect_stage.py`, `repack_stage.py` | `STAGE*.BIN` and the other container files |
| `inspect_bnk.py`, `repack_bnk.py`, `repack_vab.py` | The `.BNK` texture blocks and the `.SE` sound banks |
| `inspect_tim.py`, `repack_tim.py` | TIM pictures, to and from PNG |
| `inspect_str.py`, `inspect_movovl.py` | The opening movie's stream and the movie overlay's executable |

Each `repack_*` has a `--roundtrip` mode that unpacks and repacks the disc's
own files and checks the result is identical.

## The native port

[`docs/PORT.md`](../docs/PORT.md).

| | |
|---|---|
| `port_build.py` | `make port` (`bash tools/build.sh port`): builds the native port, `build/port/rebushido`, from the game's C with the port's switch on, Sony's library code where it is plain arithmetic, and `src/port/`. A 32-bit x86 Linux program with no C library, as the host tests are: it needs the host's `gcc` with `-m32` and nothing else, and says so where that `gcc` cannot make one (`--host` asks only that). The game's files are compiled as `tests/run.py` compiles them; the port's own with `make rebushido-modern`'s warnings as errors. The game's variables are linked in the PlayStation's order, by a script it writes; it prints how many neighbours are as far apart as there and writes the others to `build/port/layout.txt`. Beside the program it writes `rebushido.sym`, the program's names and the values of `tests/play/state.toml`, which the program reads for `--trace`, for a plan and to say where it crashed. `--list` names the files |
| `port_order.py` | Run by `make port` after the build, with `--check`: finds every place in the port where the order in which an expression's operands and arguments are evaluated is the compiler's choice **and** matters, because one of them is a call that may write what another reads. The PlayStation's compiler makes an expression's calls in the order written and reads variables after them; a PC's works the arguments out from the last and reads as it goes ([`docs/PORT.md`](../docs/PORT.md), "What the PlayStation's compiler decided"). It compiles each file as `port_build.py` does, reads the statements the compiler made (`-fdump-tree-gimple-lineno`) and what every function may write, and holds what it finds to `config/port_order.txt`, the places that were looked at with why each makes no difference; a place not there fails it. `--update` writes that list again and keeps the reasons; `--all` also shows the places where the order differs and cannot matter |
| `port_compare.py` | `python tools/port_compare.py TEST [CHECKPOINT...]`: where two runs of a play test differ, from what `playtest.py` left in `build/playtest/TEST/`: at each checkpoint the values that differ and each run of bytes of the `[snapshot]` memory that does, by variable and offset, with the bytes on each side. An address of the same place of the same name in both programs is not a difference. `--new` shows only what did not differ at the checkpoint before, `--first` stops at the first that differs, `--only NAME` takes one piece. For finding where the native port leaves the emulator ([`tests/play/README.md`](../tests/play/README.md), "Finding where two runs part"). `play/snapshot.py` is the comparison itself, which `playtest.py` uses too |
| `port_gte_macros.py` | Writes Sony's GTE macros (`include/psxsdk/inline_c.h`, `inline_o.h`) again as C on the port's GTE (`src/port/gte.c`), an instruction a statement, into `build/port/include/`. The builds with the port's switch on put that folder first: `tests/run.py --rebushido` and `tools/modern/check.py --rebushido` call it. `--list` names the macros left out (those with no instruction word in Sony's header). An instruction it has no C for stops it |
