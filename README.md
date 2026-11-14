# Bushido Blade 2 (SLUS-00663): matching decompilation

C source for *Bushido Blade 2* (SquareSoft / Lightweight, 1998, PlayStation,
North American release) that builds the original executable byte for byte:
`SLUS_006.63`, SHA1 `62efab4f73f992798c43e8c730aa43baa10bb4fa`. The small
separate program that plays the opening movie, `MOVOVL.EXE`, is rebuilt the
same way (SHA1 `a1307dbebefca0b057e02509207d00f6225e13e4`).

A matching decompilation is source written by reading the machine code, checked
by compiling it with the compiler the game was built with and comparing the
result with the original. The game's own source was never published.

## Status

- **Every function is decompiled and the build matches.** 1,484 functions:
  1,304 in C, and 180 that were assembly in the original and stay assembly
  (71 of the game's own, the renderer's inner loops and its maths; 109 of
  Sony's: BIOS calls, the GTE's matrix routines, start-up code).
- **The source has been reworked to be read.** Each subsystem has its own
  files and header, and functions, types and constants are named for what
  they do in the game. No original name of the game's was recovered: the
  names are ours. Many are a reading of the code, not something confirmed in
  the running game; where a name is a guess the source says so.
- **Matching bytes is not the same as being the authors' code.** The source
  was gone through for which lines are shown to be what the authors wrote and
  which only compile to the same thing. What is still a stand-in is marked
  `FAKE` in the source (137 places) and listed, with what was tried, in
  [`docs/FAKES.md`](docs/FAKES.md).
- **About seventy places were noted where the original's code or data looks
  like a mistake.** They are listed in [`docs/STATUS.md`](docs/STATUS.md) and
  none was changed.

[`docs/STATUS.md`](docs/STATUS.md) has the state per subsystem, the counts,
what was tested and what was not, and the open questions.

## This branch and ReBushido

`main` is the canonical byte-matching source, and stays that: nothing is
fixed or changed here that would change the executable. A modded line of the
game, ReBushido, lives on a branch of its own, `rebushido`, built on this
source.

## What you need

- Linux, or Windows with WSL 2 (Ubuntu 24.04 is what is tested).
- **No game files to build.** The build compiles the source and compares the
  result's SHA1 with the known one.
- Your own copy of the North American disc only for the disc tools: to unpack
  and repack the game's files, to build a disc image with a changed
  executable, and for the two tests that read the disc.

## Building

```bash
bash tools/setup_wsl.sh      # once: MIPS binutils, Python venv, the compiler
bash tools/build.sh check    # build; ends with "OK: bb2 matches!"
bash tools/build.sh movovl   # the movie overlay; "OK: movovl matches!"
bash tools/build.sh checks   # everything CI checks; "OK: all 7 checks pass"
```

From a Windows shell the same commands are `wsl bash tools/build.sh check`.

GitHub Actions runs seven checks on every push: the two byte checks, the
counts and the lint of the source (`tools/count.py`, `tools/lint.py`),
`make shift` (nothing depends on where code or data is), `make modern` (the
game's C through a modern compiler's warnings and analyser) and `make test`
(host tests of what the game's C computes, [`tests/README.md`](tests/README.md)).
`checks` runs the same seven locally. [`BUILD.md`](BUILD.md) has the details,
the disc extraction for the asset tools, and what to do when something fails.

## The program

| | |
|---|---|
| Platform | Sony PlayStation (R3000A, 2 MB RAM) |
| Developer / publisher | Lightweight / SquareSoft |
| Engine | "Marionation" (its name in the game's error strings) |
| Compiler | GCC 2.7.2 (SN Systems' `cc1psx`) |
| Assembler | ASPSX 2.34 |
| Libraries | Sony PsyQ, 4.0-era |
| Date string in the executable | `Fri Aug 7 22:26:32 1998` |

| | Main executable | Movie overlay |
|---|---|---|
| File | `SLUS_006.63`, 606,208 bytes | `STR/MOVOVL.EXE`, 124,928 bytes |
| Load address | `0x80010000` | `0x801D8800` |
| Entry point | `0x800836EC` | `0x801DA084` |
| Loaded size | `0x93800` (604,160 bytes) | `0x1E000` (122,880 bytes) |
| Stack / `$gp` | `0x801FFFF0` / `0x800A30CC` | given by the game / set by its entry code |

## Repository layout

```
src/main/               the game, one subsystem per file or small group of files
src/main/psxsdk/<lib>/  Sony's library code, one file per PsyQ module
include/                one header per subsystem; bb2.h includes them all
include/psxsdk/         Sony's headers
asm/funcs/              listings of the functions that were assembly in the original (built)
asm/header.s            the PS-X EXE header
movovl/                 the movie overlay: its own source, Makefile and README
config/                 what the build is told: the SHA1, the few addresses the linker
                        is given, three lists of functions where the original assembler
                        behaved differently, and the table of every symbol's address
tests/                  host tests of the game's pure functions
tools/                  build scripts, source checks, disc and asset tools (tools/README.md)
docs/                   documentation (below)
Makefile, bb2.ld        the build and the linker script (hand-maintained)
```

## Where to read next

| File | What |
|---|---|
| [`BUILD.md`](BUILD.md) | Setting up, building, the build pipeline, the checks, troubleshooting |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | How the game works: the main loop, the subsystems, where the data lives, the words the source uses |
| [`docs/GLOSSARY.md`](docs/GLOSSARY.md) | PlayStation, MIPS and toolchain terms; the Japanese words in the game's files |
| [`docs/STYLE.md`](docs/STYLE.md) | How the source is written: the rules and the checklist a file is held to |
| [`docs/STATUS.md`](docs/STATUS.md) | Where things stand, what is still a stand-in for the authors' code, the suspected bugs of the original, what was tested, the open questions |
| [`docs/FAKES.md`](docs/FAKES.md) | Every place shaped to give the bytes (`FAKE` notes, `goto`s, casts) with what was tried; how the original laid out its variables and files; what was measured about the compiler |
| [`docs/formats/`](docs/formats/README.md) | The disc's file formats, with tools to inspect and repack them |
| [`docs/MATCHING.md`](docs/MATCHING.md) | Changing code and keeping the bytes: what the compiler does with what |
| [`docs/COMPILER.md`](docs/COMPILER.md) | The compiler: which one, how it is built, what is known about the original |
| [`docs/HISTORY.md`](docs/HISTORY.md) | How the project got here, and where the removed working material is |
| [`movovl/README.md`](movovl/README.md) | The movie overlay |
| [`AGENTS.md`](AGENTS.md), [`CLAUDE.md`](CLAUDE.md) | Project facts and instructions for AI coding agents, which did most of the work |

## Modding

The source is meant to be a base for changes: translations, gameplay changes,
research into the engine. The asset tools unpack and repack the disc's files,
and `tools/build_disc.py` builds a disc image from the original image plus a
rebuilt executable and replaced files (see
[`docs/formats/README.md`](docs/formats/README.md)).

Code that changes size moves everything after it. `make shift` relinks the
build with padding inserted and checks that nothing depends on where the
code and data are; it passes. `$gp` and the section lengths Sony's start-up
code reads are worked out by the link, and `tools/make_psexe.py` writes the
header from the link. What is fixed is the memory above the program: the
buffers from `0x8010DB00` up (`include/sys.h`,
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)), which leave a changed build
0x6220 bytes (about 25 KB) to grow by before they have to move too. A padded
build has also been run beside the original in an emulator (the opening movie,
a versus fight on every stage, a story stage, a memory card save and load) and
plays the same to the pixel but for one bug of the original's that reads
leftover memory; the movie overlay was not moved, and most modes were not
played ([`docs/STATUS.md`](docs/STATUS.md), "What was tested and what was
not").

## Credits

- **Square Co., Ltd.** and **Lightweight Co., Ltd.** for the game and its engine.
- **Sony Computer Entertainment** for the PsyQ SDK it was built with.
- **[mips-gcc-2.7.2](https://github.com/decompals/mips-gcc-2.7.2)**
  ([decompals](https://github.com/decompals)): the compiler source this build uses.
- **[maspsx](https://github.com/mkst/maspsx)** (Matt "mkst" Stevenson): the
  ASPSX stand-in; `tools/maspsx/` is a modified copy.
- **[splat](https://github.com/ethteck/splat)** (Ethan Roseman and
  contributors): the splitter the project started from.
- **[decomp-permuter](https://github.com/simonlindholm/decomp-permuter)** and
  **[m2c](https://github.com/matt-kempster/m2c)**, used throughout the
  decompilation.
- The PlayStation decompilation community and its projects, whose conventions
  and Sony library source this one leans on:
  [sotn-decomp](https://github.com/Xeeynamo/sotn-decomp),
  [psy-q-decomp](https://github.com/sozud/psy-q-decomp),
  [rood-reverse](https://github.com/ser-pounce/rood-reverse),
  [ff7-decomp](https://github.com/Xeeynamo/ff7-decomp),
  [silent-hill-decomp](https://github.com/Vatuu/silent-hill-decomp),
  [chrono-cross-decomp](https://github.com/jdperos/chrono-cross-decomp).

## Licence and legal

The work made for this project (the tools, the tests, the build files, the
documentation, the names and comments in the source, and the changes on
the `rebushido` branch) is licensed under the **GNU Affero General Public
License, version 3 or later**: [`LICENSE`](LICENSE). What that does and
does not cover, and the credits it owes, are in [`NOTICE`](NOTICE). How to
contribute: [`CONTRIBUTING.md`](CONTRIBUTING.md).

*Bushido Blade 2* is a copyrighted work, © 1997–1998 SquareSoft / Lightweight.
**This repository contains no original game assets**: no executable, audio,
textures, models or scripts. The disc image is not here; the disc and asset
tools need your own copy of the North American disc.

This project is decompilation research. The C source was written by reading
the published game's machine code and behaviour, not copied from leaked or
otherwise non-public source code, and is offered as research and educational
reference. Contributions must be made the same way.

If you hold rights in *Bushido Blade 2* and have a concern about anything in
this repository, please open an issue.
