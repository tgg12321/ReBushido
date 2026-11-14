# AGENTS.md

Tool-agnostic project facts for any AI coding agent ([agents.md](https://agents.md/) standard).
Claude-Code-specific notes are in `CLAUDE.md`.

## Project

Matching decompilation of **Bushido Blade 2** (SLUS-00663, PS1, SquareSoft/Lightweight 1998, the
"Marionation" engine): C source that rebuilds a byte-identical executable. Every function is
decompiled and the build reproduces the original. A single-person project. State:
`docs/STATUS.md`; how it got here: `docs/HISTORY.md`; disc layout, asset formats and inspectors:
`docs/formats/`.

`main` is the canonical byte-matching source: no bug fix, mod or behaviour change goes here. A
modded line, ReBushido, lives on the branch `rebushido`.

## Toolchain (confirmed from binary analysis)

- **PsyQ libraries: 4.0-era.** All 175 retained verbatim library modules match PsyQ 4.0 byte-for-byte (.text,
  relocations, exports); 79 match no 3.3 / 3.5 / 3.6 library, including the CVS-tagged libgpu SYS (`sys.c v1.129`),
  libcd BIOS (`bios.c v1.86`) and libetc INTR (`intr.c v1.76`). This dates
  the libraries, not the compiler.
- **Compiler:** GCC 2.7.2 (SN cc1psx); the build uses the open-source port `decompals/mips-gcc-2.7.2`, built by
  `tools/build_cc1.sh` (`docs/COMPILER.md`).
- **Assembler:** ASPSX 2.34 via [maspsx](https://github.com/mkst/maspsx) (`--aspsx-version=2.34`).
- **Section order:** `.rodata → .text → .data → .bss` (PsyQ standard).
- Per C file: `mipsel-linux-gnu-cpp | cc1 | maspsx | mipsel-linux-gnu-as → .o`; link with
  `mipsel-linux-gnu-ld`, `objcopy`, then `make_psexe.py` prepends the 0x800-byte header
  (`asm/header.s`, with entry, load address and size computed from the ELF).
- **`-mel` and `-msoft-float` in `CC_FLAGS` are load-bearing: do not remove.** The prebuilt cc1's
  `mips-mips-gnu` triple defaults to big-endian and hard float; the PS1 is little-endian with no
  FPU, and PsyQ's cc1psx reports `-msoft-float`.
- **Small data:** every file of the game is compiled with a small-data threshold of 8 (`-G8`);
  Sony's library code, the data-only files and the three files of hand-written assembly are not
  (the lists are in the `Makefile`). A string of eight bytes or fewer is small data, so a short
  string is written as a literal where it is used.

## Executable

- **Main EXE** `disc/SLUS_006.63` (606,208 bytes), **SHA1 `62efab4f73f992798c43e8c730aa43baa10bb4fa`**
  (the oracle); "done" means the full build reproduces it.
- PS-X EXE, load `0x80010000`, entry `0x800836EC`, stack `0x801FFFF0`, GP `0x800A30CC`;
  text+data `0x93800` bytes from file offset `0x800`; code ends ~`0x8008D070`.
- Overlay `disc/STR/MOVOVL.EXE` (FMV/MDEC): loads at `0x801D8800`, no overlap with the main EXE.
  Its own source, build and SHA1 are in `movovl/` (`wsl bash tools/build.sh movovl`). It is a
  separate program with its own check, but 43 of its library modules are built from
  `src/main/psxsdk/`: a change there or in `movovl/` needs both checks.

## Build

Builds run inside WSL (Ubuntu 24.04); toolchain setup: `bash tools/setup_wsl.sh`. The build needs no disc file.

```bash
cd /mnt/c/<path to the checkout> && source .venv/bin/activate
make          # build and verify SHA1
make clean
```

- **`bb2.ld` is hand-maintained**, and all data is defined in C. No tool writes the linker script
  or the listings in `asm/funcs/`; do not try to regenerate either with a splitter.
- **Build files** (`src/**/*.c`, `*.h`, `*.s`, `Makefile`, `*.ld`, `config/*`, `*.sh`) **must be LF.**
  Windows-side editors and Windows Python text-mode writes produce CRLF and silently break the
  GNU toolchain. `python3 tools/normalize_lf.py <file>` repairs one.

## The gates

GitHub Actions runs seven checks on every push (`.github/workflows/build.yml`), and
`wsl bash tools/build.sh checks` (`make checks`) runs the same seven locally and prints a summary
(`BUILD.md`, "What is checked"). `tools/checks.sh` and the workflow hold the same list: change both
together.

- **The bytes:** `wsl bash tools/build.sh check` must print `OK: bb2 matches!`, and
  `wsl bash tools/build.sh movovl` as well when `src/main/psxsdk/` or `movovl/` changes.
- **`python tools/count.py --check`:** recounts what `docs/STATUS.md` reports and checks every
  `FAKE` note and `goto` of the source against its row in `docs/FAKES.md`.
- **`python tools/lint.py --check`:** counts the checklist of `docs/STYLE.md` per file against
  `tools/lint_baseline.tsv`; nothing may go up (`--update` in the commit that brings a count down).
- **`make shift`:** relinks with padding; nothing may depend on where code or data is.
- **`make modern`** and **`make test`:** the game's C through a modern compiler's analyser, and
  host tests of what its pure functions compute (`tests/README.md`).

Not a gate in CI, but kept clean: `python tools/format.py --check --all`.

### PowerShell-first scripting

Calling WSL from a Windows-side agent nests shells (Git Bash/PowerShell → wsl → bash); every `$`,
quote and backslash is re-parsed per layer, silently breaking inline awk/sed, heredocs and
hand-escaped quotes.
- Build: `wsl bash tools/build.sh check`, and `wsl bash tools/build.sh movovl` for the overlay
  (one script file, no inline quoting).
- Anything beyond one simple command → write a `.py`/`.sh`/`.ps1` under `tmp/` and run the file.
- Multi-line commit messages → `git commit -F <file>`.

## Conventions

- Source style and naming: [`docs/STYLE.md`](docs/STYLE.md). What each tool is for: [`tools/README.md`](tools/README.md).
  What is still a stand-in for the authors' code, and the measured facts about the compiler:
  [`docs/FAKES.md`](docs/FAKES.md). Changing code and keeping the bytes: [`docs/MATCHING.md`](docs/MATCHING.md).
- Identifiers are English; a Japanese word from the game's data goes in a comment. Proper nouns
  (fighters, places, weapons, `chanbara`, `kaishaku`) stay.
- GTE (cop2) ops and BIOS/syscall trampolines have no C form: inline `__asm__` for those, and
  `INCLUDE_ASM` for the functions whose original was hand-written assembly, are the final form.
- **Source layout:** game code in `src/main/`, one subsystem per file or small group of files
  (`memcard.c`, `memcard_save.c`; data-only files `<subsystem>_data.c`). Sony library code in
  `src/main/psxsdk/<lib>/<module>.c`, one file per PsyQ module. Headers: Sony's in
  `include/psxsdk/`, one per game subsystem (`include/memcard.h`); `include/common.h` has the
  basic types and `include/bb2.h` includes every header, declaring nothing itself.
- A file's object is `build/src/<path>.o`; `bb2.ld` lists every object once per section, in
  link order. Adding, renaming or splitting a file means editing `bb2.ld` (and the per-file
  flag lists in the `Makefile` if the file is in one).
- **Uninitialised globals** that are not `static` (`s32 g_x;`) are placed one by one: each has a
  line `*(.gnu.linkonce.b.g_x)` in `bb2.ld`, in address order. Adding or renaming one means editing
  that list (`tools/rename.py` does it for a rename). One of eight bytes or fewer that
  its users reach by full address is defined in `src/main/globals.c`, not in a file that uses it.
  A `static` one needs no line: its file's `.sbss` or `.bss` places it in the order the file
  declares it, so that order is part of the match.
- **`config/`** holds what the build is told and what it is checked against: `bb2.sha1`,
  `named_syms.txt` (the three addresses the linker is given), the three lists of functions maspsx
  treats specially, and `symbol_addrs.txt`, every symbol and its address with the functions marked
  (`tools/count.py` counts the functions from it).
- **Listings:** `asm/funcs/<name>.s` are the listings the source includes with `INCLUDE_ASM`.
  Every other function is C; `python3 tools/listings.py --write <name>` writes its disassembly
  into `build/reference/`. After renaming anything, in WSL after a build:
  `python3 tools/listings.py` (checks `asm/funcs/` against the ELF) and
  `python3 tools/symbol_addrs.py` (writes the table again), and rename the listing's file if it
  was a function of `asm/funcs/` (`tools/rename.py` does).
- Addresses are KSEG0 (`0x80000000`+); `0x1F800000`–`0x1F8003FF` is scratchpad RAM.
- **C layout:** `.clang-format`, applied by `python tools/format.py <files>`.
- Scratch goes in `tmp/` (gitignored); don't add files at the repo root.
